#include "operational_api.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "alarm_journal.h"
#include "alarm_metrics.h"
#include "alarm_suppression.h"
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
/* Staleness is NOT defined here. It comes from safety_manager, which owns the one
 * configured definition -- see safety_manager_meter_stale_timeout_ms(). A local
 * constant here is what produced a window where control was inhibited for staleness
 * while this alarm path still considered the sample fresh. */

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
 * decision; Out of service is a maintenance action. All three are implemented -
 * shelving here, the other two under A9 below - and they are kept apart as three
 * independent facts rather than one flag with three labels. Shelving came first
 * because the reason operators reach for "disable" is that shelving was not
 * offered.
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
    /* True once a poll has COMPLETED, either way, since the controller last had
     * a network to reach the meter over. Until then nothing has been learned
     * about the meter and no verdict on it can be honest. */
    bool meter_verdict_ready;
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
/* When the controller last acquired a network, and 0 while it has none. The
 * meter can only be judged on polls attempted after this instant; see
 * meter_verdict_unavailable(). */
static uint32_t s_network_online_since_ms;
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
    /* A9. Two more suppression axes, kept as separate booleans rather than folded
     * into one "disabled" flag or one enum. ISA-18.2 is explicit that the three
     * states must stay distinguishable, because months later "an operator shelved
     * this for an hour", "the controller stopped raising it while the network it
     * depends on was down" and "a technician took this instrument out of service,
     * authorised, reason recorded" are three different findings and only one of
     * them is a defect. An alarm can be in more than one at once, so one field
     * cannot carry them. */
    bool suppressed_by_design;
    bool out_of_service;
    uint8_t out_of_service_reason;
    uint8_t severity;
    uint16_t occurrences;
    uint16_t suppressed_transitions;
    uint16_t shelf_count;
    uint16_t design_suppression_count;
    uint16_t out_of_service_count;
    uint32_t design_suppressed_ms;
    uint32_t out_of_service_ms;
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

/* --- A10: alarm-rate measurement -----------------------------------------
 * EEMUA 191 gives two testable numbers - fewer than one alarm per operator per
 * ten minutes in steady state, and no more than ten in the first ten minutes of
 * an upset - and until something measures them there is no evidence this alarm
 * system is usable, only a claim that it is.
 *
 * Every confirmed raise is counted here, including raises that were suppressed
 * at the time. That is deliberate. The metric is meant to expose an unusable
 * alarm system, so it must not be possible to improve it by suppressing more
 * alarms: what it reports is an upper bound on the load an operator can see, and
 * an upper bound that meets the target is proof, whereas a figure that shrinks
 * every time somebody shelves something is not evidence of anything.
 *
 * The window arithmetic lives in alarm_metrics.c because it is pure and can
 * therefore be executed on a host rather than read and hoped about; the wrap and
 * saturation cases in particular cannot be provoked on hardware in a useful
 * time. See tests/alarm_metrics_test.c. */
static alarm_rate_t s_rate;

/* Defined below, with the causality table it encodes. Declared here because A9's
 * suppressed-by-design rule is driven by exactly that table: the system's own
 * suppression decision and the root-cause attribution an operator reads must come
 * from one place, or the alarm list will say "consequence of NET-001" while the
 * suppression state disagrees. */
static uint8_t alarm_cause_of(uint8_t code, const operational_alarm_t *table);

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
 * informational; informational records are events.
 *
 * These assignments were re-audited condition by condition against the 5% high /
 * 15% medium / 80% low target, and the resulting distribution is computed from
 * this same table and published - see priority_census() and the "rationalisation"
 * object in the alarm payload. It does not meet the target and the payload says
 * so. Nothing here was moved to make the numbers look better. The one candidate
 * for change was MTR-001, which is arguably high whenever it stands alone with no
 * live cause, since the operator's action is then the same as for MTR-002. It was
 * deliberately left at medium: it remains the derived view of a fault normally
 * raised at its own priority, and promoting it would move the distribution
 * further from the target rather than closer - which is the clearest possible
 * sign that the distribution is the wrong thing to optimise on a controller with
 * four alarms. */
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

/* --- A6: the resulting distribution, measured rather than asserted ----------
 * The assignments above were rationalised one condition at a time against what
 * each means for the plant. That is half the work; the other half is checking
 * what the assignments add up to, because EEMUA's 5/15/80 is a property of the
 * whole alarm system and cannot be seen from any single row.
 *
 * So the census is computed from the same alarm_priority() table the alarm list
 * is served from - there is no second, hand-maintained copy of the numbers to
 * drift out of date - and published with the target next to it and a plain
 * statement of whether it is met. It is not met on this controller, and the
 * payload says so rather than rounding towards the answer somebody wants.
 *
 * Two populations are reported because two different questions are being asked.
 * The alarm population is the four conditions an operator has to acknowledge,
 * which is the population EEMUA's distribution is about. The condition
 * population adds the three records that were rationalised OUT of the alarm
 * table and into the event log - controller start, control mode change, solar
 * fleet availability - which is where this controller's low-priority band
 * actually lives. Reporting only the first would hide the low band entirely;
 * reporting only the second would count things an operator never acknowledges as
 * if they were alarms. Both are stated, and neither meets the target.
 *
 * The honest reason it cannot be met is arithmetic, not assignment: with a
 * population this small the smallest non-zero share is far above 5%, so the high
 * band cannot be represented at all. That is published too, precisely so that
 * nobody reads the miss as an invitation to demote a condition that genuinely
 * stops the plant being controlled safely. */
static alarm_priority_census_t priority_census(bool alarms_only)
{
    alarm_priority_census_t census = {0};
    for (size_t code = 0; code < sizeof(s_alarms) / sizeof(s_alarms[0]); ++code) {
        if (alarms_only && !event_is_alarm_condition((uint8_t)code)) continue;
        switch (alarm_priority((uint8_t)code)) {
        case 2:  census.high++;   break;
        case 1:  census.medium++; break;
        default: census.low++;    break;
        }
    }
    return census;
}

static void add_priority_census(cJSON *parent, const char *name,
                                const alarm_priority_census_t *census)
{
    cJSON *object = cJSON_AddObjectToObject(parent, name);
    if (!object) return;
    const uint16_t total = alarm_priority_total(census);
    cJSON_AddNumberToObject(object, "high", census->high);
    cJSON_AddNumberToObject(object, "medium", census->medium);
    cJSON_AddNumberToObject(object, "low", census->low);
    cJSON_AddNumberToObject(object, "total", total);
    cJSON_AddNumberToObject(object, "high_percent",
                            alarm_priority_percent(census->high, total));
    cJSON_AddNumberToObject(object, "medium_percent",
                            alarm_priority_percent(census->medium, total));
    cJSON_AddNumberToObject(object, "low_percent",
                            alarm_priority_percent(census->low, total));
    cJSON_AddBoolToObject(object, "meets_target", alarm_priority_meets_target(census));
    /* Whether the target is even reachable for a population this size. A miss
     * that is arithmetically unavoidable is a different finding from a miss
     * caused by careless assignment, and only the second is worth acting on. */
    cJSON_AddBoolToObject(object, "target_representable",
                          alarm_priority_target_representable(total));
    cJSON_AddNumberToObject(object, "smallest_representable_percent",
                            alarm_priority_min_representable_percent(total));
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
        /* A10: this is the one place a confirmed raise becomes real, so it is the
         * only honest place to count one. Pure arithmetic on a fixed ring - no
         * allocation, no logging - so it is safe with interrupts disabled. */
        alarm_rate_record(&s_rate, alarm->last_raised_ms);
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

/* Caller holds s_lock. A9: suppressed by design - the system's own suppression
 * decision, as distinct from an operator's shelf and from a maintenance
 * out-of-service.
 *
 * The rule is the causality table the root-cause work already established: a
 * condition with a live upstream cause is a consequence of that cause, not an
 * independent fault. Losing the site network takes the grid meter with it, so
 * "meter offline" while the network is down carries no information the operator
 * does not already have from "network offline" - and EEMUA's ceiling of ten
 * alarms in the first ten minutes of an upset is spent four times over by that
 * one physical event unless something says so.
 *
 * Three properties make this a suppression state rather than a mute:
 *
 *  - It is evaluated from plant state on every observation, never chosen for an
 *    alarm individually, which is what makes it the system's decision. No
 *    endpoint can set it and no operator can lift it while the cause stands.
 *  - It releases the instant the cause clears, so a suppression can never outlive
 *    the plant state that justified it.
 *  - Both edges are journalled, which is the difference between a suppression
 *    state and a quiet omission.
 *
 * It runs after the presence delays for the whole table have settled, because a
 * cause that is itself still pending must not suppress anything yet. */
static void service_design_suppression_locked(uint32_t timestamp)
{
    for (size_t code = 0; code < sizeof(s_alarms) / sizeof(s_alarms[0]); ++code) {
        if (!event_is_alarm_condition((uint8_t)code)) continue;
        operational_alarm_t *alarm = &s_alarms[code];
        /* A condition that is not present has nothing to suppress: reporting it
         * as suppressed by design would claim the controller decided to hide
         * something that was never there. */
        const uint8_t cause = alarm_cause_of((uint8_t)code, s_alarms);
        const bool cause_present = alarm->present && cause != ALARM_NO_CAUSE;
        switch (alarm_design_suppression_step(alarm->suppressed_by_design, cause_present)) {
        case ALARM_DESIGN_STEP_ENGAGE:
            alarm->suppressed_by_design = true;
            alarm->design_suppressed_ms = timestamp;
            if (alarm->design_suppression_count < UINT16_MAX) alarm->design_suppression_count++;
            /* The cause travels in the journal record, so the controller's own
             * decision can be checked afterwards rather than taken on trust. */
            journal_stage_locked((uint8_t)code, (uint8_t)ALARM_JOURNAL_DESIGN_SUPPRESSED,
                                 timestamp, cause);
            break;
        case ALARM_DESIGN_STEP_RELEASE:
            alarm->suppressed_by_design = false;
            alarm->design_suppressed_ms = 0U;
            journal_stage_locked((uint8_t)code, (uint8_t)ALARM_JOURNAL_DESIGN_RELEASED,
                                 timestamp, 0U);
            break;
        case ALARM_DESIGN_STEP_NONE:
        default:
            break;
        }
    }
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
    /* Second pass, after every presence delay in the table has been resolved:
     * design suppression reads the whole table and must not see a half-updated
     * one. */
    service_design_suppression_locked(timestamp);
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
    state->meter_online = meter.online && meter_has_data && meter_age <= safety_manager_meter_stale_timeout_ms() &&
                          isfinite(meter.active_power_kw);
    /*
     * WHEN THE METER CAN BE JUDGED: ONE COMPLETED POLL SINCE THE LINK EXISTED.
     *
     * Counting attempts alone was not enough, and the plant proved it. While the
     * controller's Wi-Fi was still connecting, its polls failed and the failure
     * counters filled -- so at the moment the network came back the meter looked
     * like an instrument with a history of failures, and both alarms were raised
     * in the same observation as "Network restored", a full ten seconds before
     * the meter had any chance to answer. Judging it then was judging it on
     * evidence gathered while the controller could not reach it.
     *
     * So the clock starts when the network does. meter_manager stamps
     * last_attempt_ms before it branches on the outcome, so this is true after
     * exactly one completed poll, whether it succeeded or failed -- a dead meter
     * is still reported one poll after the link comes up.
     */
    if (!state->network_online) {
        s_network_online_since_ms = 0U;
    } else if (s_network_online_since_ms == 0U) {
        s_network_online_since_ms = sample->timestamp_ms;
    }
    state->meter_verdict_ready =
        state->network_online && s_network_online_since_ms != 0U &&
        meter.last_attempt_ms != 0U &&
        (int32_t)(meter.last_attempt_ms - s_network_online_since_ms) >= 0;
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

/*
 * WHEN THERE IS NO HONEST VERDICT TO GIVE ABOUT THE METER.
 *
 * Established from the plant's own event journal, not reasoned about here. A
 * restart produced, in order: "Controller started" and "Controller network
 * offline" at 55 s, "Network restored" at 35 s, "Meter offline alarm" and "Grid
 * measurement unavailable" in that SAME observation, and both cleared by 25 s --
 * against an EM500 that was answering every second before and after.
 *
 * The meter is reached over the controller's own network. While that network is
 * down every poll fails from the near end and the meter looks dead from here,
 * but the failure is the controller's and is already reported by its own
 * critical alarm with the right action. The meter alarms added nothing except an
 * instruction to inspect intact wiring at the far end of a link the controller
 * had not yet joined. Reporting a consequence as an independent fault is how an
 * alarm list becomes something operators scroll past.
 *
 * The condition ends at the first completed poll after the link exists, so a
 * meter that is genuinely absent still alarms -- one poll later, for a reason
 * that is then true.
 *
 * This governs REPORTING only. safety_manager raises both meter flags in every
 * one of these states and holds PV at zero, because a meter that has not been
 * read and a meter that cannot be reached are equally unfit to regulate
 * against. See tests/meter_startup_alarm_source_contract.py.
 */
static bool meter_verdict_unavailable(const observed_state_t *state)
{
    return !state->meter_verdict_ready;
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
        /*
         * A METER THAT HAS NOT BEEN READ YET HAS NOT FAILED.
         *
         * The three raises below fire before the first poll has completed, so
         * every restart produced a critical "the primary grid meter is not
         * communicating -- check meter power, communication wiring, gateway and
         * network path", plus a stale-data warning and a measurement-unavailable
         * critical. They cleared themselves about twenty seconds later when the
         * first sample landed. Observed on the plant: three criticals standing
         * on the overview immediately after a flash, against a meter that was
         * answering every second.
         *
         * An alarm that always fires at startup and always clears is worse than
         * no alarm. It sends an engineer to inspect wiring that is intact, and
         * it teaches everyone to discount the one class of alarm that means the
         * controller has lost sight of the plant.
         *
         * The condition is not suppressed, only its VERDICT is deferred until
         * there is evidence for one. Control is untouched: safety_manager still
         * raises both flags and still holds PV at zero, because an unread meter
         * and a dead meter are equally unfit to regulate against.
         */
        const bool meter_unknown = meter_verdict_unavailable(next);
        if (!meter_unknown && !next->meter_online) {
            append_event_ex(EVENT_METER_STATE, false, 2, 0, timestamp, true);
        }
        if (!meter_unknown && (next->alarm_flags & SAFETY_ALARM_METER_OFFLINE) != 0U) {
            append_event_ex(EVENT_METER_OFFLINE_ALARM, true, 2, 0, timestamp, true);
        }
        if (!meter_unknown && (next->alarm_flags & SAFETY_ALARM_METER_STALE) != 0U) {
            append_event_ex(EVENT_METER_STALE_ALARM, true, 1, 0, timestamp, true);
        }
        s_observed = *next;
        if (meter_unknown) {
            /*
             * Recorded as healthy DELIBERATELY, and this is the load-bearing
             * half of the change.
             *
             * Latching the fault here instead would mean a meter that never
             * answers produces no transition and therefore no event at all --
             * the silent-on-a-dead-meter bug the raises above exist to prevent.
             * Recording the optimistic state means a condition that survives
             * the first completed attempt transitions 0 -> 1 and is raised by
             * the normal path below, with its on-delay, one observation
             * interval later. A meter that is genuinely absent still alarms; it
             * alarms a few seconds later and for a reason that is now true.
             */
            s_observed.meter_online = true;
            s_observed.alarm_flags &= ~(uint32_t)(SAFETY_ALARM_METER_OFFLINE |
                                                  SAFETY_ALARM_METER_STALE);
        }
        s_observed.initialized = true;
        return;
    }
    if (next->network_online != s_observed.network_online) {
        append_event(EVENT_NETWORK_STATE, next->network_online, next->network_online ? 0 : 2, 0, timestamp);
    }
    /*
     * The same deferral as at the first sample, for as long as it is true.
     *
     * Suppressing only the first sample would have moved the false alarm rather
     * than removed it: the first observation is taken seconds after boot, and if
     * the meter task has not completed an attempt by the SECOND one the state
     * recorded as healthy transitions to faulty and raises exactly the alarm
     * this change exists to stop. The verdict waits for one completed attempt,
     * however many observations that takes -- and a completed attempt is what
     * ends it, so a meter that never answers cannot defer its alarm for ever:
     * the first failed poll ends the unknown state and raises it.
     */
    const bool meter_unknown = meter_verdict_unavailable(next);
    if (!meter_unknown && next->meter_online != s_observed.meter_online) {
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
    bool next_offline = !meter_unknown && (next->alarm_flags & SAFETY_ALARM_METER_OFFLINE) != 0;
    if (previous_offline != next_offline) append_event(EVENT_METER_OFFLINE_ALARM, next_offline, next_offline ? 2 : 0, 0, timestamp);
    bool previous_stale = (s_observed.alarm_flags & SAFETY_ALARM_METER_STALE) != 0;
    bool next_stale = !meter_unknown && (next->alarm_flags & SAFETY_ALARM_METER_STALE) != 0;
    if (previous_stale != next_stale) append_event(EVENT_METER_STALE_ALARM, next_stale, next_stale ? 1 : 0, 0, timestamp);
    s_observed = *next;
    if (meter_unknown) {
        /* Hold the recorded state optimistic for the same reason as at the first
         * sample, so the raise happens once, on the transition that follows the
         * first completed attempt, rather than on every observation until then. */
        s_observed.meter_online = true;
        s_observed.alarm_flags &= ~(uint32_t)(SAFETY_ALARM_METER_OFFLINE |
                                              SAFETY_ALARM_METER_STALE);
    }
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

/*
 * ONE NAME PER FIELD, NOT ONE PER SAMPLE.
 *
 * This built 180 objects, each repeating all eight field names and every value.
 * Measured on the plant: 31,500 bytes for a 15-minute window, fetched every ten
 * seconds -- 63 % of everything the plant overview downloads.
 *
 * Of those 31,500 bytes, 5,307 carried grid power, which is the only field that
 * actually varies. Seven of the eight held ONE distinct value across all 180
 * samples: solar null throughout, meter_online true throughout, alarms zero
 * throughout, and so on -- 22 kB of the payload was the same answer repeated.
 * age_ms cost another 3,218 and is derivable, since sample_interval_ms is
 * already sent and the samples are evenly spaced.
 *
 * So: a column per field, a field that never changes collapsed to a single
 * value under "constant", and no per-sample timestamp. Same information, same
 * eight facts, 3,757 bytes.
 *
 * The overlays this feeds -- comms gaps, alarm markers, control bands -- read
 * meter_online, alarms and control_enabled per sample, so none of them may be
 * dropped. They are folded, not discarded, and the browser rebuilds the sample
 * array it has always consumed.
 */
/* Rounded to 10 W before transmission.
 *
 * A float that decodes to 341.239990234375 costs eighteen characters to say a
 * number this product displays to one decimal and measures to rather less. On
 * the field that dominates the payload once the constants are folded out, that
 * is most of what is left.
 *
 * The SUMMARY -- min, max, average -- is computed in add_summary() from the
 * unrounded samples, so the figures an operator reads are not derived from a
 * display convenience. This rounding applies to the drawn series alone, where
 * 10 W is a fifth of a pixel. */
static void add_number_or_null(cJSON *array, float value)
{
    if (isfinite(value)) {
        /* Rounded in DOUBLE, not in float. roundf() gives back a float whose
         * nearest double is 693.280029296875, and cJSON prints the double -- so
         * rounding in single precision produced the eighteen characters it was
         * meant to remove. */
        cJSON_AddItemToArray(array, cJSON_CreateNumber(round((double)value * 100.0) / 100.0));
    } else {
        cJSON_AddItemToArray(array, cJSON_CreateNull());
    }
}

static void add_series_json(cJSON *root, const operational_sample_t *samples, uint16_t count)
{
    cJSON *series = cJSON_AddObjectToObject(root, "series");
    cJSON *constant = cJSON_AddObjectToObject(root, "constant");
    if (!series || !constant || count == 0U) return;

    /* A field is constant only if EVERY sample agrees. Checked rather than
     * assumed: a run that happens to start flat must not have its later
     * variation folded away. */
    bool solar_same = true, meter_same = true, online_same = true;
    bool enabled_same = true, control_same = true, alarms_same = true;
    for (uint16_t i = 1; i < count; ++i) {
        const operational_sample_t *a = &samples[i - 1];
        const operational_sample_t *b = &samples[i];
        const bool a_solar = isfinite(a->solar_kw), b_solar = isfinite(b->solar_kw);
        if (a_solar != b_solar || (a_solar && a->solar_kw != b->solar_kw)) solar_same = false;
        if (a->meter_online != b->meter_online) meter_same = false;
        if (a->inverter_online != b->inverter_online) online_same = false;
        if (a->inverter_enabled != b->inverter_enabled) enabled_same = false;
        if (a->control_enabled != b->control_enabled) control_same = false;
        if (a->alarm_flags != b->alarm_flags) alarms_same = false;
    }

    /* grid_kw is never folded. It is the measurement this chart exists to draw,
     * and a flat fifteen minutes is a fact about the plant that the reader must
     * still see as a line rather than infer from its absence. */
    cJSON *grid = cJSON_AddArrayToObject(series, "grid_kw");
    if (grid) for (uint16_t i = 0; i < count; ++i) add_number_or_null(grid, samples[i].grid_kw);

    if (solar_same) {
        if (isfinite(samples[0].solar_kw)) {
            cJSON_AddNumberToObject(constant, "solar_kw", samples[0].solar_kw);
        } else {
            cJSON_AddNullToObject(constant, "solar_kw");
        }
    } else {
        cJSON *solar = cJSON_AddArrayToObject(series, "solar_kw");
        if (solar) for (uint16_t i = 0; i < count; ++i) add_number_or_null(solar, samples[i].solar_kw);
    }

    if (meter_same) {
        cJSON_AddBoolToObject(constant, "meter_online", samples[0].meter_online != 0);
    } else {
        cJSON *a = cJSON_AddArrayToObject(series, "meter_online");
        if (a) for (uint16_t i = 0; i < count; ++i)
            cJSON_AddItemToArray(a, cJSON_CreateBool(samples[i].meter_online != 0));
    }
    if (online_same) {
        cJSON_AddNumberToObject(constant, "inverter_online", samples[0].inverter_online);
    } else {
        cJSON *a = cJSON_AddArrayToObject(series, "inverter_online");
        if (a) for (uint16_t i = 0; i < count; ++i)
            cJSON_AddItemToArray(a, cJSON_CreateNumber(samples[i].inverter_online));
    }
    if (enabled_same) {
        cJSON_AddNumberToObject(constant, "inverter_enabled", samples[0].inverter_enabled);
    } else {
        cJSON *a = cJSON_AddArrayToObject(series, "inverter_enabled");
        if (a) for (uint16_t i = 0; i < count; ++i)
            cJSON_AddItemToArray(a, cJSON_CreateNumber(samples[i].inverter_enabled));
    }
    if (control_same) {
        cJSON_AddBoolToObject(constant, "control_enabled", samples[0].control_enabled != 0);
    } else {
        cJSON *a = cJSON_AddArrayToObject(series, "control_enabled");
        if (a) for (uint16_t i = 0; i < count; ++i)
            cJSON_AddItemToArray(a, cJSON_CreateBool(samples[i].control_enabled != 0));
    }
    if (alarms_same) {
        cJSON_AddNumberToObject(constant, "alarms", samples[0].alarm_flags);
    } else {
        cJSON *a = cJSON_AddArrayToObject(series, "alarms");
        if (a) for (uint16_t i = 0; i < count; ++i)
            cJSON_AddItemToArray(a, cJSON_CreateNumber(samples[i].alarm_flags));
    }
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
    /* Oldest first, evenly spaced by sample_interval_ms, newest last. The
     * browser derives each sample's age from its index; no timestamp is sent. */
    cJSON_AddNumberToObject(root, "count", count);
    add_series_json(root, snapshot, count);
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

cJSON *operational_api_build_events_json(void)
{
    operational_event_t *snapshot = calloc(EVENT_COUNT, sizeof(*snapshot));
    if (!snapshot) return NULL;

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
        return NULL;
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
    return root;
}

static esp_err_t events_get(httpd_req_t *request)
{
    cJSON *root = operational_api_build_events_json();
    if (!root) return httpd_resp_send_500(request);
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

/* Defined with the out-of-service endpoint it belongs to. Declared here so the
 * alarm listing can publish the same reason list the endpoint will accept: a
 * caller must never have to guess the vocabulary of a mandatory field. */
static void add_out_of_service_reasons(cJSON *parent, const char *name);

/* The alarm condition table: what is wrong now, since when, how often, and
 * whether anyone has taken responsibility. Cleared-but-unacknowledged rows are
 * still returned, because an operator needs to see that something happened
 * while they were not looking. */
cJSON *operational_api_build_alarms_json(void)
{
    operational_alarm_t snapshot[sizeof(s_alarms) / sizeof(s_alarms[0])];
    const uint32_t current = now_ms();
    /* A10: the window counts are lifted under the same lock as the table, so the
     * rate an operator reads cannot describe a different instant from the alarms
     * next to it. Scalars rather than a copy of the ring: 192 timestamps is more
     * stack than an HTTP handler on an 8 kB stack should spend, and the counting
     * is a few hundred integer comparisons with no allocation. */
    uint16_t rate_10min = 0, rate_hour = 0, rate_day = 0, rate_peak = 0;
    uint32_t rate_total = 0, rate_discarded = 0, rate_peak_age = 0;
    bool rate_day_truncated = false;
    portENTER_CRITICAL(&s_lock);
    /* A3: expire shelves before the snapshot is taken, so a shelf whose time
     * ran out can never be reported as still in force. Reading the alarm list
     * is the moment it matters most. */
    for (size_t code = 0; code < sizeof(s_alarms) / sizeof(s_alarms[0]); ++code) {
        service_shelf_locked((uint8_t)code, &s_alarms[code], current);
    }
    /* A9: and re-evaluate the system's own suppression decision, for the same
     * reason. Design suppression is derived from the causality table that this
     * response also reports as `caused_by`; if it were only refreshed on the
     * five-second tick the two could disagree in the same payload. */
    service_design_suppression_locked(current);
    memcpy(snapshot, s_alarms, sizeof(snapshot));
    rate_10min = alarm_rate_window_count(&s_rate, current, ALARM_RATE_WINDOW_MS);
    rate_hour = alarm_rate_window_count(&s_rate, current, ALARM_RATE_HOUR_MS);
    rate_day = alarm_rate_window_count(&s_rate, current, ALARM_RATE_DAY_MS);
    rate_day_truncated = alarm_rate_window_truncated(&s_rate, current, ALARM_RATE_DAY_MS);
    rate_peak = s_rate.peak_per_window;
    rate_peak_age = current - s_rate.peak_at_ms;
    rate_total = s_rate.total;
    rate_discarded = s_rate.discarded;
    portEXIT_CRITICAL(&s_lock);
    /* The auto-unshelve and the design-suppression edges above may have staged
     * audit records; write them now that interrupts are enabled again, before
     * anything is serialized. */
    journal_flush();

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
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
    /* A9: out of service has no expiry, so the reason is mandatory. The vocabulary
     * is published with the alarm list rather than only on rejection, so a caller
     * can offer the choice instead of discovering it from a 400. */
    cJSON_AddBoolToObject(root, "out_of_service_expires", false);
    cJSON_AddBoolToObject(root, "out_of_service_reason_required", true);
    add_out_of_service_reasons(root, "out_of_service_reasons");
    cJSON *items = cJSON_AddArrayToObject(root, "alarms");
    uint16_t active = 0, unacknowledged = 0;
    uint16_t primary_active = 0, consequential_active = 0, primary_unacknowledged = 0;
    uint16_t stale_count = 0, suppressed_total = 0;
    uint16_t shelved_count = 0, shelved_active = 0;
    uint16_t design_suppressed_count = 0, design_suppressed_active = 0;
    uint16_t out_of_service_count = 0;
    uint16_t suppressed_count = 0, suppressed_active = 0;

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

        /* A3 and A9: a suppressed alarm stays in the list, keeps its state, its
         * duration, its cause attribution and its acknowledgement - everything
         * except its claim on the operator's attention. All three ISA-18.2
         * suppression states are now implemented, and they are reported as three
         * independent facts plus one effective state rather than as a single
         * boolean, so "an operator shelved this until 14:00", "the controller
         * suppressed this because the network it depends on is down" and "a
         * technician took this out of service to replace the meter" can never be
         * mistaken for one another - or for "someone turned this off". */
        const alarm_suppression_flags_t suppression_flags = {
            .shelved = a->shelved,
            .by_design = a->suppressed_by_design,
            .out_of_service = a->out_of_service,
        };
        const alarm_suppression_t suppression = alarm_suppression_effective(suppression_flags);
        const bool suppressed = alarm_suppression_hidden_from_triage(suppression);
        cJSON_AddBoolToObject(item, "shelved", a->shelved);
        /* A9: all three flags travel separately, and the effective state travels
         * with them. Publishing only the effective state would collapse exactly
         * the distinction ISA-18.2 forbids collapsing as soon as an alarm is in
         * two of them at once - an instrument out for replacement that an
         * operator also shelved must not stop reporting the shelf. */
        cJSON_AddBoolToObject(item, "suppressed_by_design", a->suppressed_by_design);
        cJSON_AddBoolToObject(item, "out_of_service", a->out_of_service);
        cJSON_AddStringToObject(item, "suppression", alarm_suppression_name(suppression));
        /* Who decided, whether it ends by itself, and how many independent
         * suppressions are in force. These three answer the questions an audit
         * asks, and none of them can be reconstructed from a boolean. */
        cJSON_AddStringToObject(item, "suppression_authority",
                                alarm_suppression_authority(suppression));
        cJSON_AddBoolToObject(item, "suppression_expires",
                              alarm_suppression_expires(suppression));
        cJSON_AddNumberToObject(item, "suppression_count",
                                alarm_suppression_active_count(suppression_flags));
        if (a->suppressed_by_design) {
            /* Named, not implied: the system suppressed this because another
             * fault explains it, and the reader is entitled to know which. */
            cJSON_AddStringToObject(item, "design_suppressed_by",
                                    consequential ? alarm_code_id(cause) : "GEN-000");
            cJSON_AddNumberToObject(item, "design_suppressed_age_ms",
                                    (double)(current - a->design_suppressed_ms));
        } else {
            cJSON_AddNullToObject(item, "design_suppressed_by");
            cJSON_AddNullToObject(item, "design_suppressed_age_ms");
        }
        cJSON_AddNumberToObject(item, "design_suppression_count", a->design_suppression_count);
        if (a->out_of_service) {
            cJSON_AddStringToObject(item, "out_of_service_reason",
                                    alarm_out_of_service_reason_name(a->out_of_service_reason));
            cJSON_AddStringToObject(item, "out_of_service_reason_text",
                                    alarm_out_of_service_reason_text(a->out_of_service_reason));
            cJSON_AddNumberToObject(item, "out_of_service_age_ms",
                                    (double)(current - a->out_of_service_ms));
        } else {
            cJSON_AddNullToObject(item, "out_of_service_reason");
            cJSON_AddNullToObject(item, "out_of_service_reason_text");
            cJSON_AddNullToObject(item, "out_of_service_age_ms");
        }
        cJSON_AddNumberToObject(item, "out_of_service_count", a->out_of_service_count);
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

        /* Suppression changes the counts an operator triages from, and nothing
         * else. The row above was emitted in full either way: prominence is the
         * only thing given up, by whichever of the three states is in force.
         *
         * Each state is counted separately as well as together, because "three
         * alarms are quiet" is not a reviewable fact - "one was shelved by an
         * operator, one is a consequence of a live network fault, and one
         * instrument is out of service for replacement" is. */
        if (a->shelved) {
            shelved_count++;
            if (a->present) shelved_active++;
        }
        if (a->suppressed_by_design) {
            design_suppressed_count++;
            if (a->present) design_suppressed_active++;
        }
        if (a->out_of_service) out_of_service_count++;
        if (suppressed) {
            suppressed_count++;
            if (a->present) suppressed_active++;
        }
        if (a->present && !suppressed) {
            active++;
            if (consequential) consequential_active++;
            else primary_active++;
        }
        /* Counts work outstanding, not just live conditions: a fault that came
         * and went unnoticed still needs someone to see it. */
        if (!a->acknowledged) {
            if (!suppressed) {
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
    /* A9: each suppression state counted on its own, plus the total. The total
     * alone would be the collapsed "disabled" figure ISA-18.2 warns about. */
    cJSON_AddNumberToObject(summary, "suppressed_by_design", design_suppressed_count);
    cJSON_AddNumberToObject(summary, "suppressed_by_design_active", design_suppressed_active);
    cJSON_AddNumberToObject(summary, "out_of_service", out_of_service_count);
    cJSON_AddNumberToObject(summary, "suppressed", suppressed_count);
    cJSON_AddNumberToObject(summary, "suppressed_active", suppressed_active);
    cJSON_AddStringToObject(summary, "state_model", "ISA-18.2");
    cJSON_AddStringToObject(summary, "priority_model", "EEMUA-191");
    /* Named so the distinction ISA-18.2 draws cannot quietly collapse. All three
     * states now exist, and the point of saying so here is that they are three
     * states and not one flag with three labels: shelving is the operator's, is
     * time-limited and expires by itself; suppressed-by-design is the
     * controller's, driven by plant state, and releases when the plant does;
     * out-of-service is a maintenance action, carries a recorded reason, and
     * deliberately does not expire. */
    cJSON_AddStringToObject(summary, "suppression_model",
                            "ISA-18.2 shelving (operator, expiring), "
                            "suppressed-by-design (system, plant-state driven) and "
                            "out-of-service (maintenance, authorised, non-expiring) "
                            "are tracked separately and never collapsed into one "
                            "disabled flag");
    cJSON_AddStringToObject(summary, "suppression_states",
                            "none, shelved, suppressed_by_design, out_of_service");

    /* --- A6: the distribution, published next to the target ---------------- */
    cJSON *rationalisation = cJSON_AddObjectToObject(root, "rationalisation");
    if (rationalisation) {
        cJSON_AddStringToObject(rationalisation, "model", "EEMUA-191");
        cJSON *target = cJSON_AddObjectToObject(rationalisation, "target");
        if (target) {
            cJSON_AddNumberToObject(target, "high_percent",
                                    ALARM_PRIORITY_TARGET_HIGH_PERCENT);
            cJSON_AddNumberToObject(target, "medium_percent",
                                    ALARM_PRIORITY_TARGET_MEDIUM_PERCENT);
            cJSON_AddNumberToObject(target, "low_percent",
                                    ALARM_PRIORITY_TARGET_LOW_PERCENT);
            cJSON_AddNumberToObject(target, "tolerance_percent",
                                    ALARM_PRIORITY_TARGET_TOLERANCE_PERCENT);
            cJSON_AddNumberToObject(target, "minimum_population",
                                    ALARM_PRIORITY_TARGET_MIN_POPULATION);
        }
        const alarm_priority_census_t alarm_census = priority_census(true);
        const alarm_priority_census_t condition_census = priority_census(false);
        add_priority_census(rationalisation, "alarms", &alarm_census);
        add_priority_census(rationalisation, "conditions", &condition_census);
        /* Said in words as well as numbers, because a bare "meets_target: false"
         * reads as a defect to be closed, and the correct response here is not to
         * demote an alarm. */
        cJSON_AddStringToObject(rationalisation, "note",
            "Every condition was rationalised individually against one test: does it stop "
            "the plant being controlled safely (high), does it degrade control or need "
            "attention soon (medium), or is it informational (low). EEMUA 191's 5/15/80 "
            "distribution is not met and cannot be, because this controller has too few "
            "conditions for a 5% band to exist: the smallest non-zero share of its alarm "
            "population is far above 5%. The low band is carried by the event log rather "
            "than the alarm table, which is itself an outcome of rationalisation - an "
            "operator does not acknowledge a controller start, so it is not an alarm. "
            "The honest spread is published above; no severity was adjusted to move it.");
    }

    /* --- A10: the measured rate, against EEMUA's two numbers --------------- */
    cJSON *rate = cJSON_AddObjectToObject(root, "rate");
    if (rate) {
        /* Same convention the journal already declares. This controller has no
         * real-time clock, so every window here is measured against uptime and
         * says so rather than implying a calendar time it does not have. */
        cJSON_AddStringToObject(rate, "time_base", "uptime_ms");
        cJSON_AddNumberToObject(rate, "uptime_ms", current);
        cJSON_AddNumberToObject(rate, "window_ms", ALARM_RATE_WINDOW_MS);
        cJSON_AddNumberToObject(rate, "raises_total", rate_total);
        cJSON_AddNumberToObject(rate, "last_10_min", rate_10min);
        cJSON_AddNumberToObject(rate, "last_60_min", rate_hour);
        cJSON_AddNumberToObject(rate, "last_24_h", rate_day);
        /* Normalised to EEMUA's own unit - alarms per ten minutes - so the
         * comparison is arithmetic rather than mental. Scaled by 1000 because
         * the interesting values are below one. */
        const uint32_t observed_hour = current < ALARM_RATE_HOUR_MS ? current : ALARM_RATE_HOUR_MS;
        const uint32_t observed_day = current < ALARM_RATE_DAY_MS ? current : ALARM_RATE_DAY_MS;
        const uint32_t per_window_hour =
            alarm_rate_per_window_milli(rate_hour, observed_hour, ALARM_RATE_WINDOW_MS);
        const uint32_t per_window_day =
            alarm_rate_per_window_milli(rate_day, observed_day, ALARM_RATE_WINDOW_MS);
        cJSON_AddNumberToObject(rate, "per_10_min_from_60_min_milli", per_window_hour);
        cJSON_AddNumberToObject(rate, "per_10_min_from_24_h_milli", per_window_day);
        cJSON_AddNumberToObject(rate, "steady_limit_milli", ALARM_RATE_STEADY_LIMIT_MILLI);
        cJSON_AddNumberToObject(rate, "peak_limit", ALARM_RATE_UPSET_LIMIT);
        cJSON_AddNumberToObject(rate, "peak_per_10_min", rate_peak);
        if (rate_peak > 0U) cJSON_AddNumberToObject(rate, "peak_age_ms", rate_peak_age);
        else cJSON_AddNullToObject(rate, "peak_age_ms");
        /* A verdict is only worth reporting once the window it is measured over
         * has actually elapsed. Before that the honest answer is "not yet
         * measured", not a number extrapolated from three minutes of uptime. */
        const bool hour_observed = alarm_rate_window_observed(current, ALARM_RATE_HOUR_MS);
        const bool day_observed = alarm_rate_window_observed(current, ALARM_RATE_DAY_MS);
        cJSON_AddBoolToObject(rate, "steady_window_observed", hour_observed);
        cJSON_AddBoolToObject(rate, "day_window_observed", day_observed);
        if (hour_observed) {
            cJSON_AddBoolToObject(rate, "meets_steady_target",
                                  alarm_rate_meets_steady_target(per_window_hour));
        } else {
            cJSON_AddNullToObject(rate, "meets_steady_target");
        }
        /* The peak verdict needs no elapsed window: a flood that has already
         * happened is measured, and one that has not cannot be disproved by
         * waiting - so this is reported as "no breach observed", never as a pass. */
        cJSON_AddBoolToObject(rate, "peak_target_breached",
                              !alarm_rate_meets_peak_target(rate_peak));
        /* Losses are admitted rather than absorbed: a rate metric that silently
         * under-reports a flood is worse than no metric. */
        cJSON_AddNumberToObject(rate, "discarded", rate_discarded);
        cJSON_AddBoolToObject(rate, "last_24_h_truncated", rate_day_truncated);
        cJSON_AddNumberToObject(rate, "capacity", ALARM_RATE_CAPACITY);
        cJSON_AddStringToObject(rate, "note",
            "Counts every confirmed alarm raise, including raises that were suppressed at "
            "the time, so the figure is an upper bound on what an operator can be shown "
            "and cannot be improved by suppressing more alarms. EEMUA 191 targets fewer "
            "than 1 alarm per operator per 10 minutes in steady state and no more than 10 "
            "in the first 10 minutes of an upset. A reboot resets these counters: there is "
            "no real-time clock and no persisted rate history.");
    }
    return root;
}

static esp_err_t alarms_get(httpd_req_t *request)
{
    cJSON *root = operational_api_build_alarms_json();
    if (!root) return httpd_resp_send_500(request);
    return send_json(request, root);
}

/* Acknowledgement is a deliberate act, so it names the condition rather than
 * offering a blanket "clear all": acknowledging something an operator has not
 * looked at is exactly what this is meant to prevent. It never clears the
 * condition - only the plant can do that. */
static esp_err_t alarms_ack_post(httpd_req_t *request)
{
    /* Acknowledgement is an OPERATOR action and is deliberately not gated behind
     * an engineering session. This was the reverse for a while, and the reverse
     * was wrong in a way that mattered:
     *
     * ISA-18.2 assigns acknowledgement to the operator, and the whole point of
     * the RTN-Unacknowledged state is that an operator discharges it. Requiring
     * engineering credentials made that impossible for the only person normally
     * present, so in practice nothing was ever acknowledged: a fault that
     * appeared and cleared itself overnight stayed outstanding indefinitely, and
     * the outstanding list -- the thing an operator triages from -- grew without
     * bound and stopped meaning anything. A safeguard that guarantees the record
     * is never maintained is not protecting the record.
     *
     * What the gate was actually protecting was attribution, and attribution is
     * preserved directly instead: `detail` carries the actor class, so the
     * durable journal distinguishes an acknowledgement made from an authenticated
     * engineering session from one made by an unauthenticated operator. Nothing
     * is lost from the evidence trail; only the refusal is gone.
     *
     * Note the deliberate asymmetry with the endpoints below. Shelving and
     * out-of-service stay gated, because they REMOVE a live condition from the
     * operator's view -- a suppression is a decision someone must be accountable
     * for. Acknowledgement suppresses nothing: it hides no alarm, silences no
     * condition, and cannot clear a fault the plant still has. It records that
     * somebody looked. The two are not comparable risks and no longer share a
     * gate.
     *
     * The controller has no operator identity model, so this records the class of
     * session, not which person. Reporting a name would be inventing one. */
    const bool by_engineering = engineering_auth_is_authorized(request);

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
        /* detail = actor class, so the durable record says who acknowledged in the
         * only terms this controller can honestly report. See ALARM_JOURNAL_ACTOR_*. */
        journal_stage_locked((uint8_t)code, (uint8_t)ALARM_JOURNAL_ACKNOWLEDGED, timestamp,
                             by_engineering ? (uint16_t)ALARM_JOURNAL_ACTOR_ENGINEERING
                                            : (uint16_t)ALARM_JOURNAL_ACTOR_OPERATOR);
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
    /* Echoed so the caller can see what was recorded against the act, and so a
     * test can assert the two classes are distinguished rather than collapsed. */
    cJSON_AddStringToObject(reply, "acknowledged_by",
                            by_engineering ? "engineering_session" : "operator");
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
/* Shared by shelve, unshelve and out-of-service: all three name one condition, and
 * all three must reject a code this controller does not have rather than mutating
 * a neighbouring row. */
static esp_err_t alarm_target_code(httpd_req_t *request, const cJSON *root, int *out_code)
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
    const esp_err_t code_error = alarm_target_code(request, root, &code);
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
    const esp_err_t code_error = alarm_target_code(request, root, &code);
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

/* --- A9: out of service ----------------------------------------------------
 * The third ISA-18.2 suppression state, and the one that has to be hardest to
 * reach, because it is the only one with no expiry. A shelf ends by itself and a
 * design suppression ends when the plant recovers; an out-of-service alarm stays
 * quiet until somebody puts it back. That is the correct behaviour for an
 * instrument that has been physically removed, and it is also exactly the shape
 * of the "disabled alarm" that makes alarm systems decay, so the three things
 * that make it defensible are all required rather than optional:
 *
 *  - An authenticated engineering session, like every other mutation here. This
 *    translation unit sits outside the authorization gateway so operator history
 *    stays readable without a session, which is why the check is written out.
 *  - A reason, from a fixed list. Not free text: the journal record carries a
 *    single uint16 of detail, so an enumerated reason survives a reboot and a
 *    sentence does not, and a reason that vanishes on restart is not an audit
 *    trail. The list is published by the endpoint so a caller can offer it.
 *  - Both edges journalled, with the reason on the way in.
 *
 * It is deliberately NOT time-limited. Adding an expiry would make it a longer
 * shelf, and the standard keeps them apart precisely because one is an operator
 * asking for quiet and the other is a statement that the measurement does not
 * currently exist. What replaces the expiry is that the state is reported
 * permanently and prominently: every alarm listing carries the count, the reason
 * and how long it has been in force, so an out-of-service alarm cannot be
 * forgotten the way a disabled one can. */
static void add_out_of_service_reasons(cJSON *parent, const char *name)
{
    cJSON *reasons = cJSON_AddArrayToObject(parent, name);
    if (!reasons) return;
    for (uint8_t reason = 0; reason <= ALARM_OUT_OF_SERVICE_REASON_MAX; ++reason) {
        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddNumberToObject(item, "reason", reason);
        cJSON_AddStringToObject(item, "name", alarm_out_of_service_reason_name(reason));
        cJSON_AddStringToObject(item, "text", alarm_out_of_service_reason_text(reason));
        cJSON_AddItemToArray(reasons, item);
    }
}

static esp_err_t alarms_out_of_service_post(httpd_req_t *request)
{
    if (!engineering_auth_is_authorized(request)) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "engineering_authentication_required");
        cJSON_AddStringToObject(err, "message",
                                "Taking an alarm out of service is a maintenance action and "
                                "requires an authenticated engineering session.");
        return send_json_status(request, "401 Unauthorized", err);
    }

    cJSON *root = NULL;
    if (http_json_parse_bounded(request, 256U, 3000ULL, 4U, &root) != ESP_OK) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "Out-of-service request must be valid bounded JSON");
        return send_json_status(request, "400 Bad Request", err);
    }

    int code = -1;
    const esp_err_t code_error = alarm_target_code(request, root, &code);
    /* Explicit in both directions. A missing flag must not default to "take it
     * out of service": suppression is never the safe default. */
    const cJSON *flag_item = cJSON_GetObjectItemCaseSensitive(root, "out_of_service");
    const bool have_flag = cJSON_IsBool(flag_item);
    const bool wanted = have_flag && cJSON_IsTrue(flag_item);
    const cJSON *reason_item = cJSON_GetObjectItemCaseSensitive(root, "reason");
    const bool have_reason = cJSON_IsNumber(reason_item);
    const double requested_reason = have_reason ? reason_item->valuedouble : -1.0;
    cJSON_Delete(root);

    if (code_error != ESP_OK || !have_flag) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error",
                                "A known alarm code and an explicit out_of_service flag are required");
        return send_json_status(request, "400 Bad Request", err);
    }
    if (wanted && (!have_reason || !(requested_reason >= 0.0) ||
                   !alarm_out_of_service_reason_valid((uint32_t)requested_reason))) {
        cJSON *err = cJSON_CreateObject();
        if (!err) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(err, "error", "out_of_service_reason_required");
        cJSON_AddStringToObject(err, "message",
            "Taking an alarm out of service has no expiry, so it must carry a recorded "
            "reason from the published list. An unexplained out-of-service alarm is a "
            "disabled alarm under another name.");
        add_out_of_service_reasons(err, "reasons");
        return send_json_status(request, "400 Bad Request", err);
    }

    const uint8_t reason = wanted ? (uint8_t)requested_reason : 0U;
    const uint32_t timestamp = now_ms();
    bool changed = false;
    bool present = false;
    uint16_t out_of_service_count = 0;
    portENTER_CRITICAL(&s_lock);
    operational_alarm_t *alarm = &s_alarms[code];
    present = alarm->present;
    changed = alarm->out_of_service != wanted;
    if (changed) {
        alarm->out_of_service = wanted;
        if (wanted) {
            alarm->out_of_service_reason = reason;
            alarm->out_of_service_ms = timestamp;
            if (alarm->out_of_service_count < UINT16_MAX) alarm->out_of_service_count++;
            journal_stage_locked((uint8_t)code, (uint8_t)ALARM_JOURNAL_OUT_OF_SERVICE,
                                 timestamp, reason);
        } else {
            alarm->out_of_service_ms = 0U;
            journal_stage_locked((uint8_t)code, (uint8_t)ALARM_JOURNAL_RETURNED_TO_SERVICE,
                                 timestamp, alarm->out_of_service_reason);
        }
    }
    out_of_service_count = alarm->out_of_service_count;
    portEXIT_CRITICAL(&s_lock);
    /* The audit record is the whole justification for allowing a non-expiring
     * suppression, so it reaches flash before the caller is told it took effect. */
    journal_flush();

    cJSON *reply = cJSON_CreateObject();
    if (!reply) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(reply, "out_of_service", wanted);
    cJSON_AddBoolToObject(reply, "changed", changed);
    cJSON_AddNumberToObject(reply, "code", code);
    cJSON_AddBoolToObject(reply, "present", present);
    cJSON_AddNumberToObject(reply, "out_of_service_count", out_of_service_count);
    cJSON_AddStringToObject(reply, "suppression",
                            alarm_suppression_name(wanted ? ALARM_SUPPRESSION_OUT_OF_SERVICE
                                                          : ALARM_SUPPRESSION_NONE));
    cJSON_AddStringToObject(reply, "authority",
                            alarm_suppression_authority(ALARM_SUPPRESSION_OUT_OF_SERVICE));
    cJSON_AddBoolToObject(reply, "expires", alarm_suppression_expires(ALARM_SUPPRESSION_OUT_OF_SERVICE));
    if (wanted) {
        cJSON_AddStringToObject(reply, "reason", alarm_out_of_service_reason_name(reason));
        cJSON_AddStringToObject(reply, "reason_text", alarm_out_of_service_reason_text(reason));
    } else {
        cJSON_AddNullToObject(reply, "reason");
        cJSON_AddNullToObject(reply, "reason_text");
    }
    add_out_of_service_reasons(reply, "reasons");
    cJSON_AddStringToObject(reply, "note",
        wanted ? "Out of service: the condition is still detected, still recorded and still "
                 "listed, and it stays out of the triage counts until somebody returns it to "
                 "service. This does NOT expire by itself - that is the difference between "
                 "out of service and shelving."
               : "Returned to service: the condition is back in the triage counts.");
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
        /* A9: the suppression records say who decided and, for out of service,
         * why. A journal that recorded only "suppressed" would answer whether the
         * alarm was quiet while destroying the question an audit actually asks. */
        const bool suppression_record =
            entry->transition == (uint8_t)ALARM_JOURNAL_SHELVED ||
            entry->transition == (uint8_t)ALARM_JOURNAL_UNSHELVED ||
            entry->transition == (uint8_t)ALARM_JOURNAL_SHELF_EXPIRED ||
            entry->transition == (uint8_t)ALARM_JOURNAL_DESIGN_SUPPRESSED ||
            entry->transition == (uint8_t)ALARM_JOURNAL_DESIGN_RELEASED ||
            entry->transition == (uint8_t)ALARM_JOURNAL_OUT_OF_SERVICE ||
            entry->transition == (uint8_t)ALARM_JOURNAL_RETURNED_TO_SERVICE;
        if (suppression_record) {
            alarm_suppression_t state = ALARM_SUPPRESSION_SHELVED;
            if (entry->transition == (uint8_t)ALARM_JOURNAL_DESIGN_SUPPRESSED ||
                entry->transition == (uint8_t)ALARM_JOURNAL_DESIGN_RELEASED) {
                state = ALARM_SUPPRESSION_BY_DESIGN;
            } else if (entry->transition == (uint8_t)ALARM_JOURNAL_OUT_OF_SERVICE ||
                       entry->transition == (uint8_t)ALARM_JOURNAL_RETURNED_TO_SERVICE) {
                state = ALARM_SUPPRESSION_OUT_OF_SERVICE;
            }
            cJSON_AddStringToObject(item, "suppression", alarm_suppression_name(state));
            cJSON_AddStringToObject(item, "suppression_authority",
                                    alarm_suppression_authority(state));
        } else {
            cJSON_AddNullToObject(item, "suppression");
            cJSON_AddNullToObject(item, "suppression_authority");
        }
        if (entry->transition == (uint8_t)ALARM_JOURNAL_OUT_OF_SERVICE ||
            entry->transition == (uint8_t)ALARM_JOURNAL_RETURNED_TO_SERVICE) {
            cJSON_AddStringToObject(item, "out_of_service_reason",
                                    alarm_out_of_service_reason_name((uint8_t)entry->detail));
        } else {
            cJSON_AddNullToObject(item, "out_of_service_reason");
        }
        if (entry->transition == (uint8_t)ALARM_JOURNAL_DESIGN_SUPPRESSED) {
            /* The cause that justified the suppression, so a reader can check the
             * controller's own decision rather than take it on trust. */
            cJSON_AddStringToObject(item, "design_suppressed_by",
                                    alarm_code_id((uint8_t)entry->detail));
        } else {
            cJSON_AddNullToObject(item, "design_suppressed_by");
        }
        /* Acknowledgement is not credential-gated, so the class of actor is the
         * whole of the attribution and has to be readable, not merely stored. A
         * field written to flash and never rendered is not an audit trail. */
        if (entry->transition == (uint8_t)ALARM_JOURNAL_ACKNOWLEDGED) {
            cJSON_AddStringToObject(item, "acknowledged_by",
                                    entry->detail == (uint16_t)ALARM_JOURNAL_ACTOR_OPERATOR
                                        ? "operator"
                                        : "engineering_session");
        } else {
            cJSON_AddNullToObject(item, "acknowledged_by");
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
        /* Pinned to CPU0. This task takes its interrupt-disabling lock once
         * per sample period, which stops interrupts on whichever core it happens
         * to be running on. The Waveshare board allocates its RGB scanout refill
         * interrupt on CPU1 precisely to stay clear of that; without an explicit
         * affinity here this task can still land on CPU1 and stall the refill,
         * which the operator sees as a periodic sweep across the panel.
         * Affinity only - no control, alarm, journal or persistence semantics
         * change, and the journal write keeps its original synchronous timing. */
        BaseType_t created = xTaskCreatePinnedToCore(operational_task, "op_history", 5120, NULL,
                                                     4, &s_task, 0);
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
        /* A9. One route in both directions rather than two: taking an alarm out of
         * service and returning it are the same decision with opposite sign, and
         * an explicit boolean makes "which did I just call" unambiguous in the
         * request as well as in the journal. */
        {.uri = "/api/operator/alarms/out-of-service", .method = HTTP_POST,
         .handler = alarms_out_of_service_post},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[i]), "operational_api", "handler registration failed");
    }
    return ESP_OK;
}
