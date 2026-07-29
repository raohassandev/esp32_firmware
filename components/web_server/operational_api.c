#include "operational_api.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "alarm_journal.h"
#include "cJSON.h"
#include "control_engine.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "engineering_auth.h"
#include "http_json.h"
#include "inverter_manager.h"
#include "meter_manager.h"
#include "network_manager.h"
#include "safety_manager.h"

#define FAST_SAMPLE_COUNT 180
#define MINUTE_SAMPLE_COUNT 1440
#define EVENT_COUNT 96
#define SAMPLE_INTERVAL_MS 5000U
#define MINUTE_INTERVAL_MS 60000U
#define METER_FRESH_MS 5000U

/* --- A4: nuisance-alarm suppression -------------------------------------
 * ISA-18.2 and EEMUA 191 both require time delays on alarm conditions so a
 * signal sitting at the edge of its threshold cannot chatter the alarm list.
 * Both also warn that an on-delay is subtracted directly from the operator's
 * response time, so it has to be bounded by process dynamics rather than
 * chosen for convenience, and its value has to be published rather than
 * buried in firmware - hence on_delay_ms / off_delay_ms in the alarm payload.
 *
 * The control loop runs on a 250 ms interval, so 1000 ms is four control
 * intervals: long enough that a single late Modbus reply cannot raise an
 * alarm, short enough to be irrelevant next to any human response time
 * (EEMUA puts an operator at roughly one alarm per minute). The off-delay is
 * deliberately longer than the on-delay: re-raising a condition costs an
 * operator nothing, whereas a premature "all clear" on a fault that is still
 * flapping is how a chattering signal disappears from view.
 *
 * Note the observation cadence is SAMPLE_INTERVAL_MS, so in practice these
 * delays mean "the condition must still hold at the next observation". That
 * is the intended effect: a state that does not survive one further look is
 * fleeting, and fleeting conditions belong in the event log, not the alarm
 * list. Conditions found already true at the first sample after boot bypass
 * the on-delay - they have no transition to debounce and have already
 * persisted across the boot - so a controller that boots into a fault still
 * reports it immediately. */
#define ALARM_ON_DELAY_MS 1000U
#define ALARM_OFF_DELAY_MS 2000U

/* --- A7: stale alarm detection ------------------------------------------
 * An alarm standing for more than 24 h with nobody acting on it is the
 * conventional definition of a stale alarm, and it almost always means the
 * alarm is wrong rather than the plant is: either it should never have been
 * an alarm, or it needs rationalising away. Surfacing it is the point; the
 * threshold is published alongside the flag so the judgement is auditable.
 * uint32 milliseconds wrap at ~49.7 days, far beyond this threshold. */
#define ALARM_STALE_THRESHOLD_MS 86400000U

/* --- A3: shelving ---------------------------------------------------------
 * ISA-18.2 keeps three suppression states apart and they must not collapse
 * into one "disabled" flag: Shelved is the operator's own decision, is
 * time-limited and expires by itself; Suppressed by design is the system's
 * decision; Out of service is a maintenance action. Only shelving is
 * implemented here, and it is implemented as the standard defines it, because
 * the reason operators reach for "disable" is that shelving was not offered.
 *
 * The expiry is REQUIRED and BOUNDED, and those two properties are the whole
 * safety argument. An indefinite shelf is a disabled alarm wearing a different
 * name: it survives the shift that created it, nobody remembers it, and the
 * alarm system quietly decays until an incident finds the gap. A shelf that
 * expires by itself cannot outlive the operator's attention.
 *
 * Eight hours is one shift. It is long enough to work through a known nuisance
 * without re-shelving every few minutes, and short enough that the shelf cannot
 * be inherited by someone who never agreed to it. One minute is the floor -
 * below that the shelf is not a decision, it is a mis-click.
 *
 * Shelving changes prominence only. Condition detection, the on/off delays,
 * occurrence counting, root-cause attribution and the journal all continue
 * exactly as before while an alarm is shelved: the operator has asked not to be
 * pressed by it, not to stop the controller from noticing it. */
#define ALARM_SHELF_MIN_MS 60000U
#define ALARM_SHELF_MAX_MS 28800000U

/* Sentinel for "this alarm has no upstream cause" (see alarm_cause_of). */
#define ALARM_NO_CAUSE 0xFFU

/* Staging depth for journal records produced inside a critical section. Alarm
 * transitions are rare - a flood is single figures - and the ring is drained on
 * every observation tick and after every operator action. */
#define ALARM_JOURNAL_STAGE_DEPTH 32U

typedef struct {
    uint32_t timestamp_ms;
    float grid_kw;
    float solar_kw;
    uint16_t alarm_flags;
    uint8_t meter_online;
    uint8_t inverter_online;
    uint8_t inverter_enabled;
    uint8_t control_enabled;
} operational_sample_t;

typedef enum {
    EVENT_CONTROLLER_STARTED = 0,
    EVENT_NETWORK_STATE,
    EVENT_METER_STATE,
    EVENT_INVERTER_FLEET_STATE,
    EVENT_CONTROL_STATE,
    EVENT_METER_OFFLINE_ALARM,
    EVENT_METER_STALE_ALARM
} operational_event_code_t;

typedef struct {
    uint32_t timestamp_ms;
    uint32_t sequence;
    int32_t value;
    uint8_t code;
    uint8_t active;
    uint8_t severity;
} operational_event_t;

typedef struct {
    bool initialized;
    bool network_online;
    bool meter_online;
    bool control_enabled;
    uint8_t inverter_online;
    uint8_t inverter_enabled;
    uint32_t alarm_flags;
} observed_state_t;

static operational_sample_t s_fast[FAST_SAMPLE_COUNT];
static operational_sample_t s_minute[MINUTE_SAMPLE_COUNT];
static operational_event_t s_events[EVENT_COUNT];
static uint16_t s_fast_head;
static uint16_t s_fast_count;
static uint16_t s_minute_head;
static uint16_t s_minute_count;
static uint16_t s_event_head;
static uint16_t s_event_count;
static uint32_t s_event_sequence;
static uint32_t s_last_minute_ms;
static observed_state_t s_observed;
static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* Defined below, next to the code-to-text mapping they belong with. */
static bool event_is_alarm_condition(uint8_t code);
static bool event_condition_present(uint8_t code, uint8_t raw_active);

/* An alarm is a condition with a lifecycle; an event is a record that something
 * happened. The ring above is the record. This table is the condition state, one
 * row per alarm code, which is what an operator actually works from: what is
 * wrong now, since when, how often it has recurred, and whether anyone has taken
 * responsibility for it.
 *
 * Acknowledgement is tracked separately from clearing on purpose. A condition
 * that clears itself was never acknowledged by anyone, and an acknowledged
 * condition that is still present must not disappear from view. */
typedef struct {
    bool present;               /* confirmed state, after the A4 delays */
    bool candidate;             /* raw state as last observed */
    bool acknowledged;
    /* A3. Shelving is a separate axis from presence and acknowledgement on
     * purpose: an alarm can be present, unacknowledged and shelved all at once,
     * and each of those three facts is something different an operator needs. */
    bool shelved;
    uint8_t severity;
    uint16_t occurrences;
    uint16_t suppressed_transitions;
    uint16_t shelf_count;
    uint32_t candidate_since_ms;
    uint32_t first_raised_ms;
    uint32_t last_raised_ms;
    uint32_t cleared_ms;
    uint32_t acknowledged_ms;
    uint32_t shelved_ms;
    uint32_t shelf_expires_ms;
    uint32_t shelf_duration_ms;
} operational_alarm_t;

static operational_alarm_t s_alarms[EVENT_METER_STALE_ALARM + 1U];

/* --- A6: priority rationalisation ----------------------------------------
 * EEMUA 191's target distribution is roughly 5% high, 15% medium, 80% low.
 * With four alarm codes that percentage cannot be met and pretending
 * otherwise would be dishonest, so each code is instead rationalised
 * individually against one test: does this stop the plant being controlled
 * safely (high), does it degrade control or need attention soon (medium), or
 * is it informational (low)?
 *
 * The 80% low band is not empty on this controller - it is carried by the
 * event log rather than the alarm list. Controller start, control mode
 * changes and solar fleet availability are recorded as events and
 * deliberately never enter the alarm table (see event_is_alarm_condition),
 * which is itself the outcome of rationalisation: an operator does not
 * acknowledge them, so they are not alarms.
 *
 * Per code:
 *   NET-001 controller network offline -> HIGH. The grid meter is reached
 *     over this network, so losing it removes the measurement the control
 *     loop depends on. It is not merely a remote-access nuisance; the plant
 *     stops being controllable on measured grid power.
 *   MTR-002 meter offline -> HIGH. Without a grid measurement, export
 *     protection cannot be verified and automatic control must inhibit.
 *   MTR-003 meter data stale -> MEDIUM. Data is arriving but late: control
 *     degrades before it fails, and the operator has time to act.
 *   MTR-001 grid measurement unavailable -> MEDIUM. This is the derived view
 *     of whatever NET-001/MTR-002/MTR-003 already says. It is real and stays
 *     inspectable, but it must not compete for attention at the same priority
 *     as its own cause.
 * Nothing here is rated low, because nothing that reaches the alarm table is
 * informational; informational records are events. */
static uint8_t alarm_priority(uint8_t code)
{
    switch ((operational_event_code_t)code) {
    case EVENT_NETWORK_STATE:        return 2;   /* high */
    case EVENT_METER_OFFLINE_ALARM:  return 2;   /* high */
    case EVENT_METER_STALE_ALARM:    return 1;   /* medium */
    case EVENT_METER_STATE:          return 1;   /* medium */
    default:                         return 0;   /* low */
    }
}

static const char *alarm_priority_rationale(uint8_t code)
{
    switch ((operational_event_code_t)code) {
    case EVENT_NETWORK_STATE:
        return "High: the grid meter is reached over this network, so losing it removes the measurement automatic control depends on.";
    case EVENT_METER_OFFLINE_ALARM:
        return "High: without a grid measurement, export protection cannot be verified and automatic control must inhibit.";
    case EVENT_METER_STALE_ALARM:
        return "Medium: measurements are still arriving but are late, so control degrades before it fails.";
    case EVENT_METER_STATE:
        return "Medium: this is the derived consequence of a meter or network fault already raised at its own priority.";
    default:
        return "Low: informational, recorded as an event rather than acknowledged as an alarm.";
    }
}

static const char *priority_label(uint8_t priority)
{
    return priority >= 2 ? "high" : priority == 1 ? "medium" : "low";
}

/* --- A2: staging between the alarm table and the persistent journal ---------
 * The alarm table is updated inside a critical section, which is where the
 * lifecycle transitions worth journalling become known. Interrupts are disabled
 * there, so a flash write in that window would stall the control loop for as
 * long as the filesystem felt like taking - and the control path must never
 * wait on storage.
 *
 * So the two halves are split. Under the lock the transition is copied into
 * this small RAM ring, which costs a handful of stores and cannot fail. Outside
 * the lock, journal_flush() drains it one record at a time and writes each to
 * flash. alarm_journal_append() is therefore never reachable from inside a
 * critical section, and the contract test asserts exactly that.
 *
 * If the ring overflows - the journal is stalled, or an implausible burst of
 * transitions - the oldest staged record is discarded and counted rather than
 * silently lost, and rather than blocking the caller. Losing the oldest few
 * records of a flood is survivable; stalling the control loop is not. */
typedef struct {
    uint32_t uptime_ms;
    uint16_t detail;
    uint8_t code;
    uint8_t transition;
} journal_stage_t;

static journal_stage_t s_stage[ALARM_JOURNAL_STAGE_DEPTH];
static uint8_t s_stage_head;
static uint8_t s_stage_tail;
static uint8_t s_stage_fill;
static uint32_t s_stage_dropped;

/* Caller holds s_lock. No allocation, no logging, no file access: this is the
 * only journal-related code permitted to run with interrupts disabled. */
static void journal_stage_locked(uint8_t code, uint8_t transition, uint32_t timestamp,
                                 uint16_t detail)
{
    if (s_stage_fill >= ALARM_JOURNAL_STAGE_DEPTH) {
        s_stage_tail = (uint8_t)((s_stage_tail + 1U) % ALARM_JOURNAL_STAGE_DEPTH);
        s_stage_fill--;
        s_stage_dropped++;
    }
    journal_stage_t *slot = &s_stage[s_stage_head];
    slot->uptime_ms = timestamp;
    slot->detail = detail;
    slot->code = code;
    slot->transition = transition;
    s_stage_head = (uint8_t)((s_stage_head + 1U) % ALARM_JOURNAL_STAGE_DEPTH);
    s_stage_fill++;
}

/* Caller holds NO lock. Each record is lifted out under a very short critical
 * section and written to flash with the lock released, so the write can take as
 * long as it needs without interrupts being off for any of it. */
static void journal_flush(void)
{
    if (!alarm_journal_ready()) return;
    for (;;) {
        journal_stage_t staged = {0};
        bool pending = false;
        portENTER_CRITICAL(&s_lock);
        if (s_stage_fill > 0U) {
            staged = s_stage[s_stage_tail];
            s_stage_tail = (uint8_t)((s_stage_tail + 1U) % ALARM_JOURNAL_STAGE_DEPTH);
            s_stage_fill--;
            pending = true;
        }
        portEXIT_CRITICAL(&s_lock);
        if (!pending) return;

        const alarm_journal_entry_t entry = {
            .sequence = 0U,     /* assigned by the journal itself */
            .uptime_ms = staged.uptime_ms,
            .detail = staged.detail,
            .code = staged.code,
            .transition = staged.transition,
        };
        (void)alarm_journal_append(&entry);
    }
}

/* Caller holds s_lock. Promotes the pending candidate into the confirmed
 * state. The raise/clear instant recorded is candidate_since_ms - when the
 * condition actually appeared - not when the delay expired, so durations stay
 * truthful and the delay does not quietly shorten a reported outage. */
static void commit_alarm_locked(uint8_t code, operational_alarm_t *alarm)
{
    if (alarm->candidate == alarm->present) return;
    if (alarm->candidate) {
        if (alarm->first_raised_ms == 0U) alarm->first_raised_ms = alarm->candidate_since_ms;
        alarm->last_raised_ms = alarm->candidate_since_ms;
        if (alarm->occurrences < UINT16_MAX) alarm->occurrences++;
        /* A fresh occurrence demands fresh attention: a previous
         * acknowledgement does not carry over to a condition that went away
         * and came back. */
        alarm->acknowledged = false;
        alarm->acknowledged_ms = 0U;
        alarm->present = true;
        alarm->cleared_ms = 0U;
        /* A2: staged, not written. The flash write happens outside this lock.
         * Note this runs whether or not the alarm is shelved - shelving hides
         * an alarm from the operator's attention, never from the record. */
        journal_stage_locked(code, (uint8_t)ALARM_JOURNAL_RAISED,
                             alarm->last_raised_ms, 0U);
    } else {
        alarm->present = false;
        alarm->cleared_ms = alarm->candidate_since_ms;
        journal_stage_locked(code, (uint8_t)ALARM_JOURNAL_CLEARED,
                             alarm->cleared_ms, 0U);
        /* ISA-18.2 keeps a condition that returned to normal without ever being
         * acknowledged in its own state, "RTN Unacknowledged", rather than
         * treating it as resolved. A fault that appears and clears itself
         * overnight would otherwise leave nothing an operator sees in the
         * morning - which on an unattended site is the fault pattern that
         * matters most. The acknowledged flag is left untouched here, so the
         * reader can distinguish "cleared and accepted" from "cleared, and
         * nobody ever knew". */
    }
}

/* Caller holds s_lock. Promotes a candidate whose on/off delay has elapsed. */
static void service_alarm_locked(uint8_t code, operational_alarm_t *alarm, uint32_t timestamp)
{
    if (alarm->candidate == alarm->present) return;
    const uint32_t delay = alarm->candidate ? ALARM_ON_DELAY_MS : ALARM_OFF_DELAY_MS;
    if ((uint32_t)(timestamp - alarm->candidate_since_ms) >= delay) {
        commit_alarm_locked(code, alarm);
    }
}

/* Caller holds s_lock. A3: a shelf is time-limited, so something has to end it
 * without an operator being present. That is the entire difference between
 * shelving and disabling, so the expiry runs on the observation tick and again
 * whenever the alarm list is read - an expired shelf can never be observed as
 * still shelved. The auto-unshelve is journalled like any other transition,
 * because "the suppression ended and nobody was told" is exactly the hole that
 * audited shelving exists to close. */
static void service_shelf_locked(uint8_t code, operational_alarm_t *alarm, uint32_t timestamp)
{
    if (!alarm->shelved) return;
    /* Signed difference: uptime milliseconds wrap at ~49.7 days and a shelf
     * must not become permanent because the counter rolled over. */
    if ((int32_t)(timestamp - alarm->shelf_expires_ms) < 0) return;
    alarm->shelved = false;
    alarm->shelf_expires_ms = 0U;
    alarm->shelf_duration_ms = 0U;
    journal_stage_locked(code, (uint8_t)ALARM_JOURNAL_SHELF_EXPIRED, timestamp, 0U);
}

/* Caller holds s_lock. Run once per observation so a condition that simply
 * persists - producing no further transition, therefore no further event -
 * still gets its pending delay evaluated. */
static void service_alarms_locked(uint32_t timestamp)
{
    for (size_t code = 0; code < sizeof(s_alarms) / sizeof(s_alarms[0]); ++code) {
        if (!event_is_alarm_condition((uint8_t)code)) continue;
        service_alarm_locked((uint8_t)code, &s_alarms[code], timestamp);
        service_shelf_locked((uint8_t)code, &s_alarms[code], timestamp);
    }
}

/* Caller holds s_lock. */
static void update_alarm_locked(operational_event_code_t code, bool present,
                                uint8_t severity, uint32_t timestamp,
                                bool bypass_on_delay)
{
    /* The severity carried on the event record is the severity as logged at the
     * time. The alarm table reports the rationalised priority instead, so an
     * operator triaging alarms cannot be shown two different urgencies for the
     * same condition depending on which view they opened. */
    (void)severity;
    if ((size_t)code >= sizeof(s_alarms) / sizeof(s_alarms[0])) return;
    operational_alarm_t *alarm = &s_alarms[code];
    alarm->severity = alarm_priority((uint8_t)code);

    if (present != alarm->candidate) {
        /* A chattering signal reverts before its delay has expired. That
         * transition is deliberately not raised, but it is counted rather than
         * silently swallowed: a signal that flaps is itself a fault worth
         * seeing, and suppressed_transitions is the diagnostic that shows it. */
        if (alarm->candidate != alarm->present &&
            alarm->suppressed_transitions < UINT16_MAX) {
            alarm->suppressed_transitions++;
        }
        alarm->candidate = present;
        alarm->candidate_since_ms = timestamp;
    }
    if (bypass_on_delay) commit_alarm_locked((uint8_t)code, alarm);
    else service_alarm_locked((uint8_t)code, alarm, timestamp);
}

static void append_event_ex(operational_event_code_t code, bool active, uint8_t severity,
                            int32_t value, uint32_t timestamp, bool bypass_on_delay)
{
    portENTER_CRITICAL(&s_lock);
    operational_event_t *event = &s_events[s_event_head];
    event->timestamp_ms = timestamp;
    event->sequence = ++s_event_sequence;
    event->value = value;
    event->code = (uint8_t)code;
    event->active = active ? 1U : 0U;
    event->severity = severity;
    s_event_head = (uint16_t)((s_event_head + 1U) % EVENT_COUNT);
    if (s_event_count < EVENT_COUNT) s_event_count++;
    /* Same lock, same instant: the condition table can never disagree with the
     * record that produced it. */
    if (event_is_alarm_condition((uint8_t)code)) {
        update_alarm_locked(code, event_condition_present((uint8_t)code, active ? 1U : 0U),
                            severity, timestamp, bypass_on_delay);
    }
    portEXIT_CRITICAL(&s_lock);
}

static void append_event(operational_event_code_t code, bool active, uint8_t severity,
                         int32_t value, uint32_t timestamp)
{
    append_event_ex(code, active, severity, value, timestamp, false);
}

static void append_sample(operational_sample_t *ring, uint16_t capacity,
                          uint16_t *head, uint16_t *count,
                          const operational_sample_t *sample)
{
    ring[*head] = *sample;
    *head = (uint16_t)((*head + 1U) % capacity);
    if (*count < capacity) (*count)++;
}

static void collect_sample(operational_sample_t *sample, observed_state_t *state)
{
    memset(sample, 0, sizeof(*sample));
    memset(state, 0, sizeof(*state));
    sample->timestamp_ms = now_ms();

    network_status_t network = {0};
    meter_data_t meter = {0};
    control_status_t control = {0};
    network_manager_get_status(&network);
    meter_manager_get_data(0, &meter);
    control_engine_get_status(&control);

    bool meter_has_data = meter.last_update_ms != 0;
    uint32_t meter_age = meter_has_data ? sample->timestamp_ms - meter.last_update_ms : UINT32_MAX;
    state->network_online = network.network_ready;
    state->meter_online = meter.online && meter_has_data && meter_age <= METER_FRESH_MS &&
                          isfinite(meter.active_power_kw);
    state->control_enabled = control.enabled;
    state->alarm_flags = safety_manager_get_alarm_flags();

    sample->grid_kw = state->meter_online ? meter.active_power_kw : NAN;
    sample->control_enabled = control.enabled ? 1U : 0U;
    sample->meter_online = state->meter_online ? 1U : 0U;
    sample->alarm_flags = (uint16_t)(state->alarm_flags & 0xFFFFU);

    float solar_kw = 0.0f;
    uint8_t inverter_online = 0;
    uint8_t inverter_enabled = 0;
    uint8_t count = inverter_manager_get_count();
    for (uint8_t i = 0; i < count; ++i) {
        inverter_data_t data = {0};
        if (!inverter_manager_get_data(i, &data)) continue;
        if (data.rated_power_kw > 0.0f) inverter_enabled++;
        if (data.online) inverter_online++;
        if (data.telemetry_valid && !data.telemetry_stale && isfinite(data.measured_power_kw)) {
            solar_kw += data.measured_power_kw;
        }
    }
    state->inverter_online = inverter_online;
    state->inverter_enabled = inverter_enabled;
    sample->solar_kw = inverter_online > 0 ? solar_kw : NAN;
    sample->inverter_online = inverter_online;
    sample->inverter_enabled = inverter_enabled;
}

static void detect_events(const observed_state_t *next, uint32_t timestamp)
{
    if (!s_observed.initialized) {
        append_event(EVENT_CONTROLLER_STARTED, true, 0, 0, timestamp);
        /* A condition already present at the first sample never produced a
         * transition, so recording only changes meant a controller that booted
         * straight into a fault reported no alarm at all - the plant was
         * offline and the alarm list was empty. Anything already wrong at
         * startup is raised here, once.
         *
         * These raises bypass the A4 on-delay: there is no transition here to
         * debounce, the condition has already persisted across the boot, and
         * deferring it would leave a controller that came up into a fault
         * showing an empty alarm list for a further observation interval.
         */
        if (!next->network_online) {
            append_event_ex(EVENT_NETWORK_STATE, false, 2, 0, timestamp, true);
        }
        if (!next->meter_online) {
            append_event_ex(EVENT_METER_STATE, false, 2, 0, timestamp, true);
        }
        if ((next->alarm_flags & SAFETY_ALARM_METER_OFFLINE) != 0U) {
            append_event_ex(EVENT_METER_OFFLINE_ALARM, true, 2, 0, timestamp, true);
        }
        if ((next->alarm_flags & SAFETY_ALARM_METER_STALE) != 0U) {
            append_event_ex(EVENT_METER_STALE_ALARM, true, 1, 0, timestamp, true);
        }
        s_observed = *next;
        s_observed.initialized = true;
        return;
    }
    if (next->network_online != s_observed.network_online) {
        append_event(EVENT_NETWORK_STATE, next->network_online, next->network_online ? 0 : 2, 0, timestamp);
    }
    if (next->meter_online != s_observed.meter_online) {
        append_event(EVENT_METER_STATE, next->meter_online, next->meter_online ? 0 : 2, 0, timestamp);
    }
    if (next->inverter_online != s_observed.inverter_online || next->inverter_enabled != s_observed.inverter_enabled) {
        uint8_t severity = next->inverter_enabled == 0 ? 0 : next->inverter_online == next->inverter_enabled ? 0 : next->inverter_online > 0 ? 1 : 2;
        int32_t packed = ((int32_t)next->inverter_online << 16) | next->inverter_enabled;
        append_event(EVENT_INVERTER_FLEET_STATE, next->inverter_online == next->inverter_enabled, severity, packed, timestamp);
    }
    if (next->control_enabled != s_observed.control_enabled) {
        append_event(EVENT_CONTROL_STATE, next->control_enabled, next->control_enabled ? 1 : 0, 0, timestamp);
    }
    bool previous_offline = (s_observed.alarm_flags & SAFETY_ALARM_METER_OFFLINE) != 0;
    bool next_offline = (next->alarm_flags & SAFETY_ALARM_METER_OFFLINE) != 0;
    if (previous_offline != next_offline) append_event(EVENT_METER_OFFLINE_ALARM, next_offline, next_offline ? 2 : 0, 0, timestamp);
    bool previous_stale = (s_observed.alarm_flags & SAFETY_ALARM_METER_STALE) != 0;
    bool next_stale = (next->alarm_flags & SAFETY_ALARM_METER_STALE) != 0;
    if (previous_stale != next_stale) append_event(EVENT_METER_STALE_ALARM, next_stale, next_stale ? 1 : 0, 0, timestamp);
    s_observed = *next;
    s_observed.initialized = true;
}

static void operational_task(void *argument)
{
    (void)argument;
    /* Mounting and scanning the journal reads a quarter of a megabyte, so it is
     * done here rather than during HTTP registration: a diagnostic feature must
     * not delay the controller coming up, and if it fails it must not stop it. */
    alarm_journal_open();
    for (;;) {
        operational_sample_t sample;
        observed_state_t observed;
        collect_sample(&sample, &observed);
        detect_events(&observed, sample.timestamp_ms);

        portENTER_CRITICAL(&s_lock);
        /* No logging, no allocation, no cJSON in here: interrupts are off. */
        service_alarms_locked(sample.timestamp_ms);
        append_sample(s_fast, FAST_SAMPLE_COUNT, &s_fast_head, &s_fast_count, &sample);
        if (s_last_minute_ms == 0 || sample.timestamp_ms - s_last_minute_ms >= MINUTE_INTERVAL_MS) {
            append_sample(s_minute, MINUTE_SAMPLE_COUNT, &s_minute_head, &s_minute_count, &sample);
            s_last_minute_ms = sample.timestamp_ms;
        }
        portEXIT_CRITICAL(&s_lock);
        /* Interrupts are back on before a single byte reaches flash. */
        journal_flush();
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }
}

/* Same envelope as send_json, with an explicit status line for error replies. */
static esp_err_t send_json_status(httpd_req_t *request, const char *status, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return httpd_resp_send_500(request);
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

static esp_err_t send_json(httpd_req_t *request, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

static void add_sample_json(cJSON *array, const operational_sample_t *sample, uint32_t current)
{
    cJSON *item = cJSON_CreateObject();
    if (!item) return;
    cJSON_AddNumberToObject(item, "age_ms", current - sample->timestamp_ms);
    if (isfinite(sample->grid_kw)) cJSON_AddNumberToObject(item, "grid_kw", sample->grid_kw);
    else cJSON_AddNullToObject(item, "grid_kw");
    if (isfinite(sample->solar_kw)) cJSON_AddNumberToObject(item, "solar_kw", sample->solar_kw);
    else cJSON_AddNullToObject(item, "solar_kw");
    cJSON_AddBoolToObject(item, "meter_online", sample->meter_online != 0);
    cJSON_AddNumberToObject(item, "inverter_online", sample->inverter_online);
    cJSON_AddNumberToObject(item, "inverter_enabled", sample->inverter_enabled);
    cJSON_AddBoolToObject(item, "control_enabled", sample->control_enabled != 0);
    cJSON_AddNumberToObject(item, "alarms", sample->alarm_flags);
    cJSON_AddItemToArray(array, item);
}

static void add_summary(cJSON *root, const operational_sample_t *samples, uint16_t count)
{
    float grid_min = INFINITY, grid_max = -INFINITY, grid_sum = 0.0f;
    float solar_min = INFINITY, solar_max = -INFINITY, solar_sum = 0.0f;
    uint16_t grid_count = 0, solar_count = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const operational_sample_t *sample = &samples[i];
        if (isfinite(sample->grid_kw)) {
            grid_min = fminf(grid_min, sample->grid_kw);
            grid_max = fmaxf(grid_max, sample->grid_kw);
            grid_sum += sample->grid_kw;
            grid_count++;
        }
        if (isfinite(sample->solar_kw)) {
            solar_min = fminf(solar_min, sample->solar_kw);
            solar_max = fmaxf(solar_max, sample->solar_kw);
            solar_sum += sample->solar_kw;
            solar_count++;
        }
    }
    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    if (grid_count) {
        cJSON_AddNumberToObject(summary, "grid_min_kw", grid_min);
        cJSON_AddNumberToObject(summary, "grid_max_kw", grid_max);
        cJSON_AddNumberToObject(summary, "grid_average_kw", grid_sum / grid_count);
    } else {
        cJSON_AddNullToObject(summary, "grid_min_kw");
        cJSON_AddNullToObject(summary, "grid_max_kw");
        cJSON_AddNullToObject(summary, "grid_average_kw");
    }
    if (solar_count) {
        cJSON_AddNumberToObject(summary, "solar_min_kw", solar_min);
        cJSON_AddNumberToObject(summary, "solar_max_kw", solar_max);
        cJSON_AddNumberToObject(summary, "solar_average_kw", solar_sum / solar_count);
    } else {
        cJSON_AddNullToObject(summary, "solar_min_kw");
        cJSON_AddNullToObject(summary, "solar_max_kw");
        cJSON_AddNullToObject(summary, "solar_average_kw");
    }
}

static esp_err_t history_get(httpd_req_t *request)
{
    char query[48] = {0};
    char range[12] = "15m";
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
        (void)httpd_query_key_value(query, "range", range, sizeof(range));
    }
    bool use_minute = strcmp(range, "1h") == 0 || strcmp(range, "24h") == 0;
    uint16_t limit = strcmp(range, "1h") == 0 ? 60U : strcmp(range, "24h") == 0 ? MINUTE_SAMPLE_COUNT : FAST_SAMPLE_COUNT;
    operational_sample_t *snapshot = calloc(limit ? limit : 1U, sizeof(*snapshot));
    if (!snapshot) return httpd_resp_send_500(request);

    uint16_t count;
    portENTER_CRITICAL(&s_lock);
    const operational_sample_t *ring = use_minute ? s_minute : s_fast;
    uint16_t capacity = use_minute ? MINUTE_SAMPLE_COUNT : FAST_SAMPLE_COUNT;
    uint16_t head = use_minute ? s_minute_head : s_fast_head;
    count = use_minute ? s_minute_count : s_fast_count;
    if (count > limit) count = limit;
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t index = (uint16_t)((head + capacity - count + i) % capacity);
        snapshot[i] = ring[index];
    }
    portEXIT_CRITICAL(&s_lock);

    uint32_t current = now_ms();
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(snapshot);
        return httpd_resp_send_500(request);
    }
    cJSON_AddStringToObject(root, "range", use_minute ? (limit == 60U ? "1h" : "24h") : "15m");
    cJSON_AddNumberToObject(root, "generated_ms", current);
    cJSON_AddBoolToObject(root, "controller_resident", true);
    cJSON_AddNumberToObject(root, "sample_interval_ms", use_minute ? MINUTE_INTERVAL_MS : SAMPLE_INTERVAL_MS);
    cJSON *items = cJSON_AddArrayToObject(root, "samples");
    for (uint16_t i = 0; i < count; ++i) add_sample_json(items, &snapshot[i], current);
    add_summary(root, snapshot, count);
    free(snapshot);
    return send_json(request, root);
}

/* The stored `active` byte is a transition polarity, not a condition state, and
 * its sense is not the same for every code: for the network and grid-measurement
 * records a 1 means "restored", while for the meter alarms a 1 means "the alarm
 * is present". Reporting that byte directly as "active" therefore told an
 * operator that "Grid measurement restored" and "Network restored" were active
 * conditions, which reads as an unresolved fault.
 *
 * These two helpers translate the stored polarity into the two things a caller
 * actually needs: whether this record is an alarm condition or a plain event,
 * and, for an alarm, whether the condition is present right now. */
static bool event_is_alarm_condition(uint8_t code)
{
    switch ((operational_event_code_t)code) {
    case EVENT_NETWORK_STATE:
    case EVENT_METER_STATE:
    case EVENT_METER_OFFLINE_ALARM:
    case EVENT_METER_STALE_ALARM:
        return true;
    case EVENT_CONTROLLER_STARTED:
    case EVENT_INVERTER_FLEET_STATE:
    case EVENT_CONTROL_STATE:
    default:
        /* A controller start or a deliberate mode change is something that
         * happened, not a condition that persists. */
        return false;
    }
}

static bool event_condition_present(uint8_t code, uint8_t raw_active)
{
    switch ((operational_event_code_t)code) {
    /* Here a stored 1 means the good state was reached, so the condition that
     * would concern an operator is present when the byte is 0. */
    case EVENT_NETWORK_STATE:
    case EVENT_METER_STATE:
        return raw_active == 0U;
    /* Here a stored 1 means the alarm itself is present. */
    case EVENT_METER_OFFLINE_ALARM:
    case EVENT_METER_STALE_ALARM:
        return raw_active != 0U;
    default:
        return false;   /* events do not persist as a condition */
    }
}

static const char *event_state_label(uint8_t code, uint8_t raw_active)
{
    if (!event_is_alarm_condition(code)) return "recorded";
    return event_condition_present(code, raw_active) ? "active" : "cleared";
}

static const char *severity_label(uint8_t severity)
{
    return severity >= 2 ? "critical" : severity == 1 ? "warning" : "information";
}

static void event_text(const operational_event_t *event, const char **title, const char **detail, const char **action)
{
    switch ((operational_event_code_t)event->code) {
        case EVENT_CONTROLLER_STARTED:
            *title = "Controller started"; *detail = "The controller completed startup and began operational monitoring."; *action = "No action required."; break;
        case EVENT_NETWORK_STATE:
            *title = event->active ? "Network restored" : "Controller network offline";
            *detail = event->active ? "The controller reconnected to the configured network." : "The controller lost its primary network connection.";
            *action = event->active ? "Confirm remote access is stable." : "Check Wi-Fi coverage, router power, and site network availability."; break;
        case EVENT_METER_STATE:
            *title = event->active ? "Grid measurement restored" : "Grid measurement unavailable";
            *detail = event->active ? "Fresh utility-grid measurements are available again." : "Fresh grid power data is not available for monitoring or control.";
            *action = event->active ? "No action required." : "Check meter power, communication wiring, gateway, and network path."; break;
        case EVENT_INVERTER_FLEET_STATE: {
            uint8_t online = (uint8_t)((event->value >> 16) & 0xFF);
            uint8_t enabled = (uint8_t)(event->value & 0xFF);
            *title = online == enabled ? "Solar fleet available" : "Solar fleet attention required";
            *detail = online == enabled ? "All enabled solar inverters are available." : "One or more enabled solar inverters are unavailable.";
            *action = online == enabled ? "No action required." : "Review the Solar Inverters page and inspect unavailable units.";
            break;
        }
        case EVENT_CONTROL_STATE:
            *title = event->active ? "Automatic control enabled" : "Automatic control disabled";
            *detail = event->active ? "The qualified PV-DG control path is active." : "The controller is operating in monitoring-only mode.";
            *action = event->active ? "Observe power flow and alarms." : "Engineering authorization is required before enabling control."; break;
        case EVENT_METER_OFFLINE_ALARM:
            *title = event->active ? "Meter offline alarm" : "Meter offline alarm cleared";
            *detail = event->active ? "The primary grid meter is not communicating." : "Meter communication has recovered.";
            *action = event->active ? "Inspect meter and communication equipment." : "Confirm measurements remain stable."; break;
        case EVENT_METER_STALE_ALARM:
            *title = event->active ? "Meter data stale" : "Meter data freshness restored";
            *detail = event->active ? "The latest grid measurement exceeded the allowed freshness window." : "Current grid measurements are fresh again.";
            *action = event->active ? "Check polling latency and communication reliability." : "No action required."; break;
        default:
            *title = "Controller event"; *detail = "An operational state changed."; *action = "Review controller status."; break;
    }
}

static esp_err_t events_get(httpd_req_t *request)
{
    operational_event_t *snapshot = calloc(EVENT_COUNT, sizeof(*snapshot));
    if (!snapshot) return httpd_resp_send_500(request);

    uint16_t count;
    portENTER_CRITICAL(&s_lock);
    count = s_event_count;
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t index = (uint16_t)((s_event_head + EVENT_COUNT - 1U - i) % EVENT_COUNT);
        snapshot[i] = s_events[index];
    }
    portEXIT_CRITICAL(&s_lock);

    uint32_t current = now_ms();
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(snapshot);
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(root, "operator_view", true);
    cJSON_AddBoolToObject(root, "engineering_details_hidden", true);
    cJSON_AddNumberToObject(root, "generated_ms", current);
    cJSON *items = cJSON_AddArrayToObject(root, "events");

    uint16_t active_critical = 0, active_warning = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const operational_event_t *event = &snapshot[i];
        const char *title, *detail, *action;
        event_text(event, &title, &detail, &action);
        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddNumberToObject(item, "sequence", event->sequence);
        cJSON_AddNumberToObject(item, "age_ms", current - event->timestamp_ms);
        const bool is_condition = event_is_alarm_condition(event->code);
        const bool present = event_condition_present(event->code, event->active);
        cJSON_AddStringToObject(item, "severity", severity_label(event->severity));
        cJSON_AddStringToObject(item, "kind", is_condition ? "alarm" : "event");
        cJSON_AddStringToObject(item, "state", event_state_label(event->code, event->active));
        /* "active" now means the condition is present right now, which is what a
         * caller reasonably assumes. A restored or informational record is not
         * active. */
        cJSON_AddBoolToObject(item, "active", present);
        cJSON_AddStringToObject(item, "title", title);
        cJSON_AddStringToObject(item, "detail", detail);
        cJSON_AddStringToObject(item, "recommended_action", action);
        cJSON_AddItemToArray(items, item);
        if (present && event->severity >= 2) active_critical++;
        else if (present && event->severity == 1) active_warning++;
    }
    free(snapshot);
    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "active_critical", active_critical);
    cJSON_AddNumberToObject(summary, "active_warning", active_warning);
    cJSON_AddNumberToObject(summary, "stored_events", count);
    return send_json(request, root);
}

static const char *alarm_code_id(uint8_t code)
{
    switch ((operational_event_code_t)code) {
    case EVENT_NETWORK_STATE:        return "NET-001";
    case EVENT_METER_STATE:          return "MTR-001";
    case EVENT_METER_OFFLINE_ALARM:  return "MTR-002";
    case EVENT_METER_STALE_ALARM:    return "MTR-003";
    default:                         return "GEN-000";
    }
}

/* --- A5: root-cause grouping ---------------------------------------------
 * One physical event - losing the site network - currently raises four
 * conditions: network offline, meter offline, meter data stale and grid
 * measurement unavailable. EEMUA 191 targets no more than ten alarms in the
 * first ten minutes of a major upset and puts an operator's absorption rate
 * at roughly one alarm per minute, so four alarms for one cause is a flood in
 * miniature: 40% of the ten-minute budget spent on a single fault.
 *
 * The remedy is attribution, not deletion. Every condition still exists, is
 * still returned, and is still individually acknowledgeable - suppressing a
 * real condition is how alarm systems decay. What changes is that a condition
 * with a live upstream cause is marked consequential and names the alarm that
 * explains it, and the summary reports primary counts separately so the
 * number an operator triages from is the number of distinct faults.
 *
 * The primary is chosen by physical causality, deepest cause first. The grid
 * meter is reached over the site network, so if the network is down the meter
 * being unreachable is a consequence of that, not an independent fault;
 * likewise a meter that is not answering at all explains data that has gone
 * stale, and any of the three explains the derived "grid measurement
 * unavailable". Attribution is evaluated against the live table at read time,
 * so a meter that fails on its own - with the network healthy - is correctly
 * reported as primary. */
static uint8_t alarm_cause_of(uint8_t code, const operational_alarm_t *table)
{
    static const uint8_t upstream_of_grid_measurement[] = {
        (uint8_t)EVENT_NETWORK_STATE,
        (uint8_t)EVENT_METER_OFFLINE_ALARM,
        (uint8_t)EVENT_METER_STALE_ALARM,
    };
    static const uint8_t upstream_of_meter_stale[] = {
        (uint8_t)EVENT_NETWORK_STATE,
        (uint8_t)EVENT_METER_OFFLINE_ALARM,
    };
    static const uint8_t upstream_of_meter_offline[] = {
        (uint8_t)EVENT_NETWORK_STATE,
    };

    const uint8_t *chain = NULL;
    size_t length = 0;
    switch ((operational_event_code_t)code) {
    case EVENT_METER_STATE:
        chain = upstream_of_grid_measurement;
        length = sizeof(upstream_of_grid_measurement);
        break;
    case EVENT_METER_STALE_ALARM:
        chain = upstream_of_meter_stale;
        length = sizeof(upstream_of_meter_stale);
        break;
    case EVENT_METER_OFFLINE_ALARM:
        chain = upstream_of_meter_offline;
        length = sizeof(upstream_of_meter_offline);
        break;
    default:
        /* Network loss is the deepest cause this controller can observe: it
         * has nothing upstream of it to blame, so it is always primary. */
        return ALARM_NO_CAUSE;
    }
    for (size_t i = 0; i < length; ++i) {
        if (table[chain[i]].present) return chain[i];
    }
    return ALARM_NO_CAUSE;
}

/* The alarm condition table: what is wrong now, since when, how often, and
 * whether anyone has taken responsibility. Cleared-but-unacknowledged rows are
 * still returned, because an operator needs to see that something happened
 * while they were not looking. */
static esp_err_t alarms_get(httpd_req_t *request)
{
    operational_alarm_t snapshot[sizeof(s_alarms) / sizeof(s_alarms[0])];
    const uint32_t current = now_ms();
    portENTER_CRITICAL(&s_lock);
    /* A3: expire shelves before the snapshot is taken, so a shelf whose time
     * ran out can never be reported as still in force. Reading the alarm list
     * is the moment it matters most. */
    for (size_t code = 0; code < sizeof(s_alarms) / sizeof(s_alarms[0]); ++code) {
        service_shelf_locked((uint8_t)code, &s_alarms[code], current);
    }
    memcpy(snapshot, s_alarms, sizeof(snapshot));
    portEXIT_CRITICAL(&s_lock);
    /* The auto-unshelve above may have staged an audit record; write it now
     * that interrupts are enabled again, before anything is serialized. */
    journal_flush();

    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddNumberToObject(root, "generated_ms", current);
    /* Published, not buried: an on-delay is taken out of the operator's own
     * response time, so the value in force has to be inspectable. */
    cJSON_AddNumberToObject(root, "on_delay_ms", ALARM_ON_DELAY_MS);
    cJSON_AddNumberToObject(root, "off_delay_ms", ALARM_OFF_DELAY_MS);
    cJSON_AddNumberToObject(root, "stale_threshold_ms", ALARM_STALE_THRESHOLD_MS);
    /* A3: the bounds on a shelf are published for the same reason the delays
     * are. An operator is entitled to know how long suppression can last before
     * being asked to accept it, and a reviewer is entitled to check that the
     * bound exists at all. */
    cJSON_AddNumberToObject(root, "shelf_minimum_ms", ALARM_SHELF_MIN_MS);
    cJSON_AddNumberToObject(root, "shelf_maximum_ms", ALARM_SHELF_MAX_MS);
    cJSON_AddBoolToObject(root, "shelf_expiry_required", true);
    cJSON *items = cJSON_AddArrayToObject(root, "alarms");
    uint16_t active = 0, unacknowledged = 0;
    uint16_t primary_active = 0, consequential_active = 0, primary_unacknowledged = 0;
    uint16_t stale_count = 0, suppressed_total = 0;
    uint16_t shelved_count = 0, shelved_active = 0;

    for (size_t code = 0; code < sizeof(snapshot) / sizeof(snapshot[0]); ++code) {
        const operational_alarm_t *a = &snapshot[code];
        if (!event_is_alarm_condition((uint8_t)code)) continue;
        if (a->occurrences == 0U) continue;          /* never seen: nothing to report */

        const operational_event_t probe = {
            .code = (uint8_t)code,
            /* Ask for the wording of whichever side the condition is on now. */
            .active = event_condition_present((uint8_t)code, 1U) == a->present ? 1U : 0U,
        };
        const char *title, *detail, *action;
        event_text(&probe, &title, &detail, &action);

        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddStringToObject(item, "id", alarm_code_id((uint8_t)code));
        cJSON_AddNumberToObject(item, "code", (double)code);
        cJSON_AddStringToObject(item, "title", title);
        cJSON_AddStringToObject(item, "detail", detail);
        cJSON_AddStringToObject(item, "recommended_action", action);
        cJSON_AddStringToObject(item, "severity", severity_label(a->severity));
        /* A6: the rationalised priority and the reason it was assigned travel
         * with the alarm, so the judgement can be reviewed rather than
         * rediscovered from the source. */
        cJSON_AddStringToObject(item, "priority", priority_label(alarm_priority((uint8_t)code)));
        cJSON_AddStringToObject(item, "priority_rationale", alarm_priority_rationale((uint8_t)code));
        /* ISA-18.2 state names. "rtn_unacknowledged" is a condition that
         * cleared itself while nobody had accepted it: still outstanding work
         * even though nothing is wrong right now. */
        cJSON_AddStringToObject(item, "state",
                                a->present ? (a->acknowledged ? "acknowledged" : "unacknowledged")
                                : a->acknowledged ? "normal"
                                                  : "rtn_unacknowledged");
        cJSON_AddBoolToObject(item, "present", a->present);
        cJSON_AddBoolToObject(item, "acknowledged", a->acknowledged);
        cJSON_AddNumberToObject(item, "occurrences", a->occurrences);
        cJSON_AddNumberToObject(item, "first_raised_age_ms",
                                (double)(current - a->first_raised_ms));
        cJSON_AddNumberToObject(item, "last_raised_age_ms",
                                (double)(current - a->last_raised_ms));
        /* Duration is measured to the clear for a finished condition and to now
         * for one still standing, so a live alarm's age keeps growing. */
        cJSON_AddNumberToObject(item, "duration_ms",
                                (double)((a->present ? current : a->cleared_ms) - a->last_raised_ms));
        if (a->acknowledged) {
            cJSON_AddNumberToObject(item, "acknowledged_age_ms",
                                    (double)(current - a->acknowledged_ms));
        } else {
            cJSON_AddNullToObject(item, "acknowledged_age_ms");
        }
        /* A5: attribution. Consequential rows stay in the list and stay
         * acknowledgeable; they simply say which fault explains them. */
        const uint8_t cause = a->present ? alarm_cause_of((uint8_t)code, snapshot)
                                         : ALARM_NO_CAUSE;
        const bool consequential = cause != ALARM_NO_CAUSE;
        cJSON_AddStringToObject(item, "role", consequential ? "consequential" : "primary");
        if (consequential) cJSON_AddStringToObject(item, "caused_by", alarm_code_id(cause));
        else cJSON_AddNullToObject(item, "caused_by");

        /* A7: an alarm standing this long with nobody acting on it is a design
         * defect to surface, not a live problem to chase. */
        const bool stale = a->present &&
                           (uint32_t)(current - a->last_raised_ms) >= ALARM_STALE_THRESHOLD_MS;
        cJSON_AddBoolToObject(item, "stale", stale);

        /* A4: transitions that never survived their delay. Zero is the healthy
         * value; a rising number is a chattering signal. */
        cJSON_AddNumberToObject(item, "suppressed_transitions", a->suppressed_transitions);

        /* A3: a shelved alarm stays in the list, keeps its state, its duration,
         * its cause attribution and its acknowledgement - everything except its
         * claim on the operator's attention. The three ISA-18.2 suppression
         * states are reported by name rather than as one boolean, so "an
         * operator shelved this until 14:00" can never be mistaken for "someone
         * turned this off". Only "shelved" is implemented; the other two are
         * named here as not-implemented rather than silently absorbed. */
        cJSON_AddBoolToObject(item, "shelved", a->shelved);
        cJSON_AddStringToObject(item, "suppression", a->shelved ? "shelved" : "none");
        if (a->shelved) {
            cJSON_AddNumberToObject(item, "shelf_remaining_ms",
                                    (double)(a->shelf_expires_ms - current));
            cJSON_AddNumberToObject(item, "shelf_duration_ms", (double)a->shelf_duration_ms);
            cJSON_AddNumberToObject(item, "shelved_age_ms", (double)(current - a->shelved_ms));
        } else {
            cJSON_AddNullToObject(item, "shelf_remaining_ms");
            cJSON_AddNullToObject(item, "shelf_duration_ms");
            cJSON_AddNullToObject(item, "shelved_age_ms");
        }
        cJSON_AddNumberToObject(item, "shelf_count", a->shelf_count);
        cJSON_AddItemToArray(items, item);

        /* Shelving changes the counts an operator triages from, and nothing
         * else. The row above was emitted in full either way: prominence is the
         * only thing an operator asked to give up. */
        if (a->shelved) {
            shelved_count++;
            if (a->present) shelved_active++;
        }
        if (a->present && !a->shelved) {
            active++;
            if (consequential) consequential_active++;
            else primary_active++;
        }
        /* Counts work outstanding, not just live conditions: a fault that came
         * and went unnoticed still needs someone to see it. */
        if (!a->acknowledged) {
            if (!a->shelved) {
                unacknowledged++;
                /* Consequential rows must not inflate the number an operator
                 * triages from - that is the whole point of the grouping. */
                if (!consequential) primary_unacknowledged++;
            }
        }
        if (stale) stale_count++;
        if (a->suppressed_transitions > 0U &&
            suppressed_total <= (uint16_t)(UINT16_MAX - a->suppressed_transitions)) {
            suppressed_total = (uint16_t)(suppressed_total + a->suppressed_transitions);
        }
    }

    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "active", active);
    cJSON_AddNumberToObject(summary, "unacknowledged", unacknowledged);
    /* Primary counts are the triage figures; the consequential count says how
     * much of the list is explained detail rather than distinct faults. */
    cJSON_AddNumberToObject(summary, "primary_active", primary_active);
    cJSON_AddNumberToObject(summary, "consequential_active", consequential_active);
    cJSON_AddNumberToObject(summary, "primary_unacknowledged", primary_unacknowledged);
    cJSON_AddNumberToObject(summary, "stale", stale_count);
    cJSON_AddNumberToObject(summary, "suppressed_transitions", suppressed_total);
    /* Shelved work is reported, never hidden. The counts above are what an
     * operator triages from; these two say how much was deliberately taken out
     * of that view and is still waiting underneath it. */
    cJSON_AddNumberToObject(summary, "shelved", shelved_count);
    cJSON_AddNumberToObject(summary, "shelved_active", shelved_active);
    cJSON_AddStringToObject(summary, "state_model", "ISA-18.2");
    cJSON_AddStringToObject(summary, "priority_model", "EEMUA-191");
    /* Named so the distinction ISA-18.2 draws cannot quietly collapse: this
     * controller implements operator shelving only. Suppressed-by-design and
     * out-of-service are absent, and absent is not the same as merged in. */
    cJSON_AddStringToObject(summary, "suppression_model",
                            "ISA-18.2 shelving only; suppressed-by-design and "
                            "out-of-service are not implemented and are not "
                            "collapsed into shelving");
    return send_json(request, root);
}

/* Acknowledgement is a deliberate act, so it names the condition rather than
 * offering a blanket "clear all": acknowledging something an operator has not
 * looked at is exactly what this is meant to prevent. It never clears the
 * condition - only the plant can do that. */
static esp_err_t alarms_ack_post(httpd_req_t *request)
{
    /* This translation unit is deliberately outside the authorization gateway so
     * operator history and events stay readable without a session. That makes
     * this POST the one mutating endpoint here, and it must not be anonymous:
     * an unattributable acknowledgement is a way to make an active condition
     * look attended to. The check is therefore explicit rather than inherited.
     *
     * Note the controller has no operator identity model, so an acknowledgement
     * records that an authenticated engineering session did it, not which
     * person. Reporting a name would be inventing one. */
    if (!engineering_auth_is_authorized(request)) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "engineering_authentication_required");
        cJSON_AddStringToObject(err, "message",
                                "Acknowledging an alarm requires an authenticated engineering session.");
        return send_json_status(request, "401 Unauthorized", err);
    }

    cJSON *root = NULL;
    const esp_err_t read_error = http_json_parse_bounded(request, 256U, 3000ULL, 4U, &root);
    if (read_error != ESP_OK) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "Acknowledgement body must be valid bounded JSON");
        return send_json_status(request, "400 Bad Request", err);
    }

    const cJSON *code_item = cJSON_GetObjectItemCaseSensitive(root, "code");
    const bool have_code = cJSON_IsNumber(code_item);
    const int code = have_code ? code_item->valueint : -1;
    cJSON_Delete(root);

    if (!have_code || code < 0 ||
        (size_t)code >= sizeof(s_alarms) / sizeof(s_alarms[0]) ||
        !event_is_alarm_condition((uint8_t)code)) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "A known alarm code is required");
        return send_json_status(request, "400 Bad Request", err);
    }

    const uint32_t timestamp = now_ms();
    bool present = false;
    bool was_outstanding = false;
    portENTER_CRITICAL(&s_lock);
    operational_alarm_t *alarm = &s_alarms[code];
    present = alarm->present;
    /* Acknowledgement applies whether or not the condition is still present.
     *
     * Restricting it to present conditions made an RTN-unacknowledged alarm
     * impossible to clear: a fault that appeared and went away while nobody was
     * watching would stay outstanding for ever, and the operator had no way to
     * discharge it. That defeats the point of retaining the state at all.
     * ISA-18.2 has the operator acknowledging exactly this case to move it from
     * RTN Unacknowledged to Normal, and it is the state that matters most on an
     * unattended site. */
    was_outstanding = !alarm->acknowledged && alarm->occurrences > 0U;
    if (was_outstanding) {
        alarm->acknowledged = true;
        alarm->acknowledged_ms = timestamp;
        journal_stage_locked((uint8_t)code, (uint8_t)ALARM_JOURNAL_ACKNOWLEDGED, timestamp, 0U);
    }
    portEXIT_CRITICAL(&s_lock);
    /* Written to flash with interrupts back on. An acknowledgement that only
     * ever existed in RAM is not evidence of anything after a restart. */
    journal_flush();

    cJSON *reply = cJSON_CreateObject();
    if (!reply) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(reply, "acknowledged", was_outstanding);
    cJSON_AddNumberToObject(reply, "code", code);
    cJSON_AddBoolToObject(reply, "present", present);
    /* Say which of the two acknowledgements this was. They mean different
     * things: one accepts a live condition, the other discharges a fault that
     * already came and went. */
    cJSON_AddStringToObject(reply, "note",
        !was_outstanding ? "Nothing outstanding for this alarm; it was already acknowledged."
        : present        ? "Condition acknowledged; it remains active until the plant clears it."
                         : "Returned-to-normal condition acknowledged; it is now cleared from the outstanding list.");
    return send_json(request, reply);
}

/* --- A3: shelving endpoints ----------------------------------------------
 * Both of these mutate suppression state, so both demand the same
 * authenticated engineering session that acknowledgement demands. This
 * translation unit sits outside the authorization gateway - operator history
 * has to stay readable without a session - so the check is written out here
 * rather than inherited. An anonymous shelf would be worse than an anonymous
 * acknowledgement: it removes a live condition from the operator's view and
 * leaves no one accountable for having done it. */
static esp_err_t alarm_shelf_code(httpd_req_t *request, const cJSON *root, int *out_code)
{
    const cJSON *code_item = cJSON_GetObjectItemCaseSensitive(root, "code");
    const bool have_code = cJSON_IsNumber(code_item);
    const int code = have_code ? code_item->valueint : -1;
    if (!have_code || code < 0 ||
        (size_t)code >= sizeof(s_alarms) / sizeof(s_alarms[0]) ||
        !event_is_alarm_condition((uint8_t)code)) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)request;
    *out_code = code;
    return ESP_OK;
}

static esp_err_t alarms_shelve_post(httpd_req_t *request)
{
    if (!engineering_auth_is_authorized(request)) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "engineering_authentication_required");
        cJSON_AddStringToObject(err, "message",
                                "Shelving an alarm requires an authenticated engineering session.");
        return send_json_status(request, "401 Unauthorized", err);
    }

    cJSON *root = NULL;
    if (http_json_parse_bounded(request, 256U, 3000ULL, 4U, &root) != ESP_OK) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "Shelve request must be valid bounded JSON");
        return send_json_status(request, "400 Bad Request", err);
    }

    int code = -1;
    const esp_err_t code_error = alarm_shelf_code(request, root, &code);
    /* The expiry is required, not defaulted. Defaulting it would let a caller
     * that never thought about duration create a shelf anyway, and an
     * unconsidered shelf is how suppression becomes permanent. */
    const cJSON *duration_item = cJSON_GetObjectItemCaseSensitive(root, "duration_ms");
    const bool have_duration = cJSON_IsNumber(duration_item);
    const double requested = have_duration ? duration_item->valuedouble : 0.0;
    cJSON_Delete(root);

    if (code_error != ESP_OK) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "A known alarm code is required");
        return send_json_status(request, "400 Bad Request", err);
    }
    if (!have_duration || !(requested >= (double)ALARM_SHELF_MIN_MS) ||
        !(requested <= (double)ALARM_SHELF_MAX_MS)) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "shelf_duration_out_of_range");
        cJSON_AddStringToObject(err, "message",
            "A shelf must carry an explicit expiry within the published bounds. "
            "An indefinite shelf is a disabled alarm under another name.");
        cJSON_AddNumberToObject(err, "shelf_minimum_ms", ALARM_SHELF_MIN_MS);
        cJSON_AddNumberToObject(err, "shelf_maximum_ms", ALARM_SHELF_MAX_MS);
        return send_json_status(request, "400 Bad Request", err);
    }

    const uint32_t duration_ms = (uint32_t)requested;
    const uint32_t timestamp = now_ms();
    /* Seconds in the journal record: 8 h fits a uint16 and the audit trail does
     * not need millisecond precision on a shift-length decision. */
    const uint16_t duration_s = (uint16_t)(duration_ms / 1000U);
    bool present = false;
    uint16_t shelf_count = 0;
    portENTER_CRITICAL(&s_lock);
    operational_alarm_t *alarm = &s_alarms[code];
    present = alarm->present;
    alarm->shelved = true;
    alarm->shelved_ms = timestamp;
    alarm->shelf_duration_ms = duration_ms;
    alarm->shelf_expires_ms = timestamp + duration_ms;
    if (alarm->shelf_count < UINT16_MAX) alarm->shelf_count++;
    shelf_count = alarm->shelf_count;
    journal_stage_locked((uint8_t)code, (uint8_t)ALARM_JOURNAL_SHELVED, timestamp, duration_s);
    portEXIT_CRITICAL(&s_lock);
    /* The audit record is what makes suppression safe, so it is written before
     * the operator is told the shelf took effect. */
    journal_flush();

    cJSON *reply = cJSON_CreateObject();
    if (!reply) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(reply, "shelved", true);
    cJSON_AddNumberToObject(reply, "code", code);
    cJSON_AddBoolToObject(reply, "present", present);
    cJSON_AddNumberToObject(reply, "shelf_duration_ms", (double)duration_ms);
    cJSON_AddNumberToObject(reply, "shelf_expires_in_ms", (double)duration_ms);
    cJSON_AddNumberToObject(reply, "shelf_count", shelf_count);
    cJSON_AddStringToObject(reply, "note",
        "Shelved: the condition is still detected, still recorded and still listed, "
        "and it leaves the triage counts until the shelf expires by itself.");
    return send_json(request, reply);
}

static esp_err_t alarms_unshelve_post(httpd_req_t *request)
{
    if (!engineering_auth_is_authorized(request)) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "engineering_authentication_required");
        cJSON_AddStringToObject(err, "message",
                                "Unshelving an alarm requires an authenticated engineering session.");
        return send_json_status(request, "401 Unauthorized", err);
    }

    cJSON *root = NULL;
    if (http_json_parse_bounded(request, 256U, 3000ULL, 4U, &root) != ESP_OK) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "Unshelve request must be valid bounded JSON");
        return send_json_status(request, "400 Bad Request", err);
    }
    int code = -1;
    const esp_err_t code_error = alarm_shelf_code(request, root, &code);
    cJSON_Delete(root);
    if (code_error != ESP_OK) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "A known alarm code is required");
        return send_json_status(request, "400 Bad Request", err);
    }

    const uint32_t timestamp = now_ms();
    bool was_shelved = false;
    portENTER_CRITICAL(&s_lock);
    operational_alarm_t *alarm = &s_alarms[code];
    was_shelved = alarm->shelved;
    if (was_shelved) {
        alarm->shelved = false;
        alarm->shelf_expires_ms = 0U;
        alarm->shelf_duration_ms = 0U;
        journal_stage_locked((uint8_t)code, (uint8_t)ALARM_JOURNAL_UNSHELVED, timestamp, 0U);
    }
    portEXIT_CRITICAL(&s_lock);
    journal_flush();

    cJSON *reply = cJSON_CreateObject();
    if (!reply) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(reply, "unshelved", was_shelved);
    cJSON_AddNumberToObject(reply, "code", code);
    cJSON_AddStringToObject(reply, "note",
        was_shelved ? "Shelf ended early; the alarm is back in the triage counts."
                    : "This alarm was not shelved.");
    return send_json(request, reply);
}

/* --- A2: the journal endpoint ---------------------------------------------
 * Paged, and paged for a physical reason rather than a stylistic one. The
 * journal holds far more than this controller can serialize: its minimum free
 * internal heap has been measured close to its own critical threshold, and one
 * response carrying the whole history would exhaust it. So the caller asks for
 * a window and is told, honestly, whether more remains behind it. */
#define ALARM_JOURNAL_PAGE_DEFAULT 50U
#define ALARM_JOURNAL_PAGE_MAX 100U

static esp_err_t alarms_journal_get(httpd_req_t *request)
{
    char query[64] = {0};
    char value[16] = {0};
    uint32_t offset = 0U;
    uint32_t limit = ALARM_JOURNAL_PAGE_DEFAULT;
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "offset", value, sizeof(value)) == ESP_OK) {
            offset = (uint32_t)strtoul(value, NULL, 10);
        }
        if (httpd_query_key_value(query, "limit", value, sizeof(value)) == ESP_OK) {
            const unsigned long parsed = strtoul(value, NULL, 10);
            limit = parsed == 0UL ? ALARM_JOURNAL_PAGE_DEFAULT : (uint32_t)parsed;
        }
    }
    if (limit > ALARM_JOURNAL_PAGE_MAX) limit = ALARM_JOURNAL_PAGE_MAX;

    alarm_journal_entry_t *page = calloc(limit, sizeof(*page));
    if (!page) return httpd_resp_send_500(request);
    bool has_more = false;
    const size_t returned = alarm_journal_read_page(offset, limit, page, &has_more);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(page);
        return httpd_resp_send_500(request);
    }
    const uint32_t current = now_ms();
    cJSON_AddNumberToObject(root, "generated_ms", current);
    /* This controller has no wall clock. Every time in this payload is
     * milliseconds since the controller last started, and saying so is the
     * difference between evidence and a fabricated date. Sequence numbers do
     * survive a restart, so ordering across reboots is still answerable even
     * though "when" is not. */
    cJSON_AddStringToObject(root, "time_base", "uptime_ms");
    cJSON_AddStringToObject(root, "time_note",
        "Times are milliseconds since the controller started, not calendar times: "
        "this controller has no real-time clock. A restart resets the time base; "
        "the sequence number does not, so records remain ordered across reboots.");
    cJSON_AddBoolToObject(root, "storage_ready", alarm_journal_ready());
    cJSON_AddStringToObject(root, "storage_status", alarm_journal_status());
    cJSON_AddBoolToObject(root, "persistent", true);
    cJSON_AddNumberToObject(root, "capacity", ALARM_JOURNAL_CAPACITY);
    cJSON_AddNumberToObject(root, "stored", alarm_journal_stored());
    cJSON_AddNumberToObject(root, "next_sequence", alarm_journal_next_sequence());
    /* Losses are reported rather than hidden. A history that quietly drops
     * records is worse than one that admits it dropped them. */
    cJSON_AddNumberToObject(root, "unreadable_skipped", alarm_journal_invalid_skipped());
    cJSON_AddNumberToObject(root, "write_failures", alarm_journal_write_failures());
    cJSON_AddNumberToObject(root, "staging_dropped", s_stage_dropped);
    cJSON_AddNumberToObject(root, "offset", offset);
    cJSON_AddNumberToObject(root, "limit", limit);
    cJSON_AddNumberToObject(root, "returned", (double)returned);
    cJSON_AddBoolToObject(root, "has_more", has_more);
    if (has_more) cJSON_AddNumberToObject(root, "next_offset", offset + (uint32_t)returned);
    else cJSON_AddNullToObject(root, "next_offset");

    cJSON *entries = cJSON_AddArrayToObject(root, "entries");
    for (size_t index = 0; index < returned; ++index) {
        const alarm_journal_entry_t *entry = &page[index];
        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        const operational_event_t probe = {
            .code = entry->code,
            .active = event_condition_present(entry->code, 1U) ? 1U : 0U,
        };
        const char *title, *detail, *action;
        event_text(&probe, &title, &detail, &action);
        cJSON_AddNumberToObject(item, "sequence", entry->sequence);
        cJSON_AddNumberToObject(item, "code", entry->code);
        cJSON_AddStringToObject(item, "id", alarm_code_id(entry->code));
        cJSON_AddStringToObject(item, "title", title);
        cJSON_AddStringToObject(item, "transition",
                                alarm_journal_transition_name(entry->transition));
        cJSON_AddNumberToObject(item, "uptime_ms", entry->uptime_ms);
        cJSON_AddNumberToObject(item, "age_ms", (double)(current - entry->uptime_ms));
        if (entry->transition == (uint8_t)ALARM_JOURNAL_SHELVED) {
            cJSON_AddNumberToObject(item, "shelf_duration_ms",
                                    (double)entry->detail * 1000.0);
        } else {
            cJSON_AddNullToObject(item, "shelf_duration_ms");
        }
        cJSON_AddItemToArray(entries, item);
    }
    free(page);
    return send_json(request, root);
}

esp_err_t operational_api_register(httpd_handle_t server)
{
    alarm_journal_init();
    if (!s_task) {
        BaseType_t created = xTaskCreate(operational_task, "op_history", 5120, NULL, 4, &s_task);
        if (created != pdPASS) return ESP_ERR_NO_MEM;
    }
    const httpd_uri_t handlers[] = {
        {.uri = "/api/operator/history", .method = HTTP_GET, .handler = history_get},
        {.uri = "/api/operator/events", .method = HTTP_GET, .handler = events_get},
        {.uri = "/api/operator/alarms", .method = HTTP_GET, .handler = alarms_get},
        {.uri = "/api/operator/alarms/ack", .method = HTTP_POST, .handler = alarms_ack_post},
        {.uri = "/api/operator/alarms/journal", .method = HTTP_GET, .handler = alarms_journal_get},
        {.uri = "/api/operator/alarms/shelve", .method = HTTP_POST, .handler = alarms_shelve_post},
        {.uri = "/api/operator/alarms/unshelve", .method = HTTP_POST, .handler = alarms_unshelve_post},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[i]), "operational_api", "handler registration failed");
    }
    return ESP_OK;
}
