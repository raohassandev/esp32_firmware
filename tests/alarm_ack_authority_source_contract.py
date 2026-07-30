"""Acknowledgement is an operator action; suppression is not.

This contract exists because the two were once behind the same gate, and that
was a real defect rather than a stylistic choice. Requiring engineering
credentials to acknowledge meant the only person normally at the plant could
not acknowledge anything, so the RTN-Unacknowledged state -- whose entire
purpose is that an operator discharges it -- was unreachable in practice and
the outstanding list grew without bound.

Two things must therefore hold together, and each is easy to break while
"fixing" the other:

  1. Acknowledgement must NOT demand an engineering session.
  2. Shelving, unshelving and out-of-service must CONTINUE to demand one,
     because those remove a live condition from the operator's view.

A change that opened all four, or re-closed all four, would look locally
reasonable and would defeat the distinction. So the asymmetry itself is the
assertion, not either half of it.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "components/web_server/operational_api.c").read_text(encoding="utf-8")
JOURNAL_H = (ROOT / "components/web_server/include/alarm_journal.h").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def handler_body(name: str) -> str:
    """The text of one handler, from its definition to the next one."""
    start = SOURCE.index(f"static esp_err_t {name}(httpd_req_t *request)")
    nxt = SOURCE.find("\nstatic esp_err_t ", start + 10)
    return SOURCE[start:nxt if nxt != -1 else len(SOURCE)]


ACK = handler_body("alarms_ack_post")

# 1. Acknowledgement must not refuse an unauthenticated operator. Asserting on
#    the absence of a 401 rather than on the absence of the auth CALL is
#    deliberate: the handler legitimately calls engineering_auth_is_authorized()
#    to classify the actor. What must not exist is the rejection.
require("401 Unauthorized" not in ACK,
        "alarms_ack_post must not reject an unauthenticated operator: "
        "acknowledgement is an operator action under ISA-18.2, and gating it "
        "makes the RTN-Unacknowledged state impossible to discharge")
require("engineering_authentication_required" not in ACK,
        "alarms_ack_post must not demand an engineering session")

# 2. But it must still classify the actor, or the attribution the gate used to
#    provide is simply gone.
require("engineering_auth_is_authorized(request)" in ACK,
        "alarms_ack_post must still determine whether the caller holds an "
        "engineering session, so the journal can record which class acted")
require("ALARM_JOURNAL_ACTOR_OPERATOR" in ACK and
        "ALARM_JOURNAL_ACTOR_ENGINEERING" in ACK,
        "the acknowledgement record must carry the actor class; an "
        "unattributable acknowledgement is what the removed gate was for")

# 3. The actor class must be readable, not merely written. A field persisted to
#    flash and never rendered is not an audit trail.
require('"acknowledged_by"' in SOURCE,
        "the journal and the acknowledgement reply must expose the actor class")
require(SOURCE.count('"acknowledged_by"') >= 2,
        "the actor class must appear in both the journal rendering and the "
        "acknowledgement reply, so it can be audited after a reboot and "
        "observed at the time")

# 4. The on-flash encoding must not be renumbered, and ENGINEERING must stay 0
#    so that records written before the field had meaning still read back
#    correctly -- at that time only an engineering session could acknowledge.
require("#define ALARM_JOURNAL_ACTOR_ENGINEERING 0U" in JOURNAL_H,
        "ALARM_JOURNAL_ACTOR_ENGINEERING must remain 0 so pre-existing "
        "acknowledgement records, which stored a literal 0, are not "
        "retroactively reattributed to an operator")
require("#define ALARM_JOURNAL_ACTOR_OPERATOR 1U" in JOURNAL_H,
        "ALARM_JOURNAL_ACTOR_OPERATOR must remain 1: written to flash")

# 5. The other side of the asymmetry. These SUPPRESS an alarm, so they stay shut.
for name in ("alarms_shelve_post", "alarms_unshelve_post", "alarms_out_of_service_post"):
    body = handler_body(name)
    require("engineering_auth_is_authorized(request)" in body,
            f"{name} must require an authenticated engineering session")
    require("401 Unauthorized" in body,
            f"{name} must reject an unauthenticated caller: shelving and "
            "out-of-service remove a live condition from the operator's view, "
            "which is a decision someone has to be accountable for. Only "
            "acknowledgement -- which suppresses nothing -- is open.")

print("Alarm acknowledgement authority contract passed")
