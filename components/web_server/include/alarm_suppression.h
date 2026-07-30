#pragma once

/* ISA-18.2 suppression states: gap A9 of docs/ALARM_MANAGEMENT_RESEARCH.md.
 *
 * ISA-18.2 defines three ways an alarm can stop pressing the operator, and the
 * standard is explicit that they must NOT be collapsed into one "disabled" flag:
 *
 *   Shelved              the operator's own decision. Time-limited, and it
 *                        expires by itself.
 *   Suppressed by design the system's decision, driven by plant state. Nobody
 *                        chose it for this alarm individually; it engages and
 *                        releases with the condition that justifies it.
 *   Out of service       a maintenance action taken under authorisation. It does
 *                        NOT expire, which is exactly why it needs a named
 *                        reason and an audit record.
 *
 * Conflating them removes the audit trail that makes suppression safe. Six months
 * after the fact, "somebody turned this off" and "the controller stopped raising
 * this because the network it depends on was down" and "this instrument is out
 * for replacement, authorised, reason recorded" are three completely different
 * findings, and only the first is a defect. A single boolean cannot tell them
 * apart, so this module keeps them as three independent facts and derives one
 * effective state for display without ever discarding the other two.
 *
 * Everything here is pure: no state, no allocation, no logging, no locks. That
 * makes it callable from inside the alarm module's critical section, and it makes
 * the state machine executable on a host - see tests/alarm_suppression_test.c,
 * which runs it rather than pattern-matching the source.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ALARM_SUPPRESSION_NONE = 0,
    ALARM_SUPPRESSION_SHELVED = 1,
    ALARM_SUPPRESSION_BY_DESIGN = 2,
    ALARM_SUPPRESSION_OUT_OF_SERVICE = 3,
} alarm_suppression_t;

/* Three independent facts, never one enum, because an alarm can genuinely be in
 * more than one of these states at once: an instrument taken out of service for
 * replacement can also be shelved by an operator who did not know, and the
 * network it hangs off can be down at the same time. Each of the three has to
 * survive into the payload and the journal separately. */
typedef struct {
    bool shelved;
    bool by_design;
    bool out_of_service;
} alarm_suppression_flags_t;

/* The one state to display, chosen by how hard it is to get out of rather than
 * by convenience: out of service needs a maintenance action, design suppression
 * needs the plant to recover, a shelf just needs time. Showing the weakest of
 * several concurrent states would let a shelf that expires in five minutes hide
 * an instrument that is out for a fortnight. */
alarm_suppression_t alarm_suppression_effective(alarm_suppression_flags_t flags);

uint8_t alarm_suppression_active_count(alarm_suppression_flags_t flags);
bool alarm_suppression_any(alarm_suppression_flags_t flags);

/* The wire names. These appear in the API and in the journal, so they are part of
 * the interface: extend, never rename. */
const char *alarm_suppression_name(alarm_suppression_t state);

/* Who decided. This is the distinction ISA-18.2 cares about most and the one a
 * single "disabled" flag destroys. */
const char *alarm_suppression_authority(alarm_suppression_t state);

/* Only a shelf expires on its own. Out of service deliberately does not, and
 * saying so plainly is the argument for requiring a reason with it. */
bool alarm_suppression_expires(alarm_suppression_t state);

/* Every suppression state removes the alarm from the counts an operator triages
 * from - that is what suppression means - and none of them removes it from the
 * list, the record or the journal. */
bool alarm_suppression_hidden_from_triage(alarm_suppression_t state);

/* --------------------------------------------- suppressed-by-design mechanism
 *
 * The system's decision needs a rule, and on this controller the rule is already
 * established by the root-cause work (A5): a condition with a live upstream cause
 * is a consequence of that cause, not an independent fault. Losing the site
 * network takes the grid meter with it, so "meter offline" while the network is
 * down is not news - it is arithmetic. EEMUA's ten-alarm ceiling for the first
 * ten minutes of an upset is spent four times over by one physical event unless
 * something says so.
 *
 * A5 attributed those alarms. A9 suppresses them, with an audit record on both
 * edges, which is the difference between an annotation and a suppression state.
 * The rule is deliberately trivial and deliberately symmetric: it engages when a
 * cause is live and releases the moment it is not, so a suppression can never
 * outlive the plant state that justified it. */
typedef enum {
    ALARM_DESIGN_STEP_NONE = 0,
    ALARM_DESIGN_STEP_ENGAGE,
    ALARM_DESIGN_STEP_RELEASE,
} alarm_design_step_t;

alarm_design_step_t alarm_design_suppression_step(bool suppressed_now, bool cause_present);

/* ------------------------------------------------------- out-of-service reasons
 *
 * Enumerated, not free text, and for a hard reason rather than a stylistic one:
 * the persistent journal record is sixteen bytes and carries a single uint16 of
 * transition detail. A typed reason fits that and therefore survives a reboot; a
 * sentence does not, and an audit trail that loses the reason on restart is not
 * an audit trail. */
#define ALARM_OUT_OF_SERVICE_REASON_MAINTENANCE 0U
#define ALARM_OUT_OF_SERVICE_REASON_REPLACEMENT 1U
#define ALARM_OUT_OF_SERVICE_REASON_COMMISSIONING 2U
#define ALARM_OUT_OF_SERVICE_REASON_AWAITING_REPAIR 3U
#define ALARM_OUT_OF_SERVICE_REASON_PLANT_CHANGE 4U
#define ALARM_OUT_OF_SERVICE_REASON_MAX 4U

bool alarm_out_of_service_reason_valid(uint32_t reason);
const char *alarm_out_of_service_reason_name(uint8_t reason);
const char *alarm_out_of_service_reason_text(uint8_t reason);

#ifdef __cplusplus
}
#endif
