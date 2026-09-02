#!/usr/bin/env python3
"""The captive portal must answer on the recovery access point and nowhere else.

Joining the setup network should open the setup page. The mechanism is a DNS
listener that answers every A query with the controller's own address, so
whatever hostname a phone asks for resolves here and its connectivity check
fetches this controller instead of the reply it expected.

That mechanism is a network-wide denial of service pointed the wrong way.

If the socket binds to INADDR_ANY it also answers DNS on the SITE LAN. Every
device on that network -- PLCs, HMIs, the customer's office machines -- would
resolve every hostname to this controller. The plant would appear to lose the
internet and its internal name resolution at once, and the fault would look like
the router rather than like the solar controller somebody installed that morning.
It would be found late and blamed elsewhere.

So the binding address is the single most important line in the file, and it is
asserted here rather than trusted to review. Everything else in this contract
exists because the parser is reachable by anyone within radio range of an access
point whose passphrase is printed on a console.

Asserted against comment-stripped source.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PORTAL = ROOT / "components" / "network_manager" / "captive_portal.c"
MANAGER = ROOT / "components" / "network_manager" / "network_manager.c"

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


def strip_comments(text):
    return re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)


portal = strip_comments(PORTAL.read_text(encoding="utf-8", errors="replace"))
manager = strip_comments(MANAGER.read_text(encoding="utf-8", errors="replace"))

# The responder task's stack lives in PSRAM. It only reads and writes lwip UDP
# sockets and touches no NVS, esp_partition or esp_flash, so its stack is never
# accessed while the flash cache is disabled, and keeping it out of internal RAM
# preserves the scarce internal DMA pool for control and safety tasks.
require(
    "xTaskCreateWithCaps(portal_task" in portal,
    "the captive portal task must be created with the capability-aware API so "
    "its stack is allocated from PSRAM",
)
require(
    "MALLOC_CAP_SPIRAM" in portal,
    "the captive portal task stack must be requested from PSRAM",
)
for _forbidden in ("nvs_", "esp_partition_", "esp_flash_"):
    require(
        _forbidden not in portal,
        "the captive portal task must not touch flash (%s): a PSRAM stack is "
        "unsafe while the cache is disabled" % _forbidden,
    )

# --- THE ONE THAT MATTERS ---------------------------------------------------

require(
    "INADDR_ANY" not in portal,
    "the captive portal binds to INADDR_ANY. It would then answer DNS for the "
    "whole site LAN, resolving every hostname on the customer's network to this "
    "controller -- a network-wide outage that looks like a router fault",
)
require(
    re.search(r"\.sin_addr\.s_addr\s*=\s*s_portal_address", portal) is not None,
    "the listener does not bind to the access point address specifically",
)
require(
    re.search(r"esp_netif_get_ip_info\(s_ap_netif", manager) is not None
    and re.search(r"captive_portal_start\(ap_ip\.ip\.addr\)", manager) is not None,
    "the portal is not started with the ACCESS POINT's address; started with the "
    "station address it would answer on the site network",
)

# --- The parser is exposed to anyone in radio range -------------------------

# A compression pointer sends the parser chasing an offset chosen by the sender.
# Refusing them outright is the whole defence; following them is how a DNS
# parser is turned into a loop or an out-of-bounds read.
require(
    "0xC0" in portal and re.search(r"return false;\s*/?\*?", portal) is not None,
    "the QNAME walker does not reject compression pointers",
)
require(
    re.search(r"if \(\(label & 0xC0U\) != 0U\) return false;", portal) is not None,
    "compression pointers are not rejected in skip_qname()",
)
require(
    "length < DNS_HEADER_BYTES" in portal,
    "a datagram shorter than a DNS header is not rejected before its fields are read",
)
require(
    re.search(r"offset \+ 4U > length", portal) is not None,
    "the question's type and class are read without checking they are within the "
    "datagram",
)
require(
    re.search(r"offset \+ answer_bytes > capacity", portal) is not None,
    "the answer is appended without checking it fits the buffer",
)

# --- It answers narrowly, and invents nothing -------------------------------

require(
    re.search(r"if \(qtype != DNS_TYPE_A \|\| qclass != DNS_CLASS_IN\) return -1;", portal)
    is not None,
    "the portal answers query types other than A/IN; a fabricated AAAA or TXT "
    "record is an invented answer, and unanswered is the honest reply",
)
require(
    "questions != 1U" in portal,
    "a multi-question query is answered as though it had one question",
)
require(
    re.search(r"if \(\(flags & 0x8000U\) != 0U\) return -1;", portal) is not None,
    "a DNS RESPONSE is treated as a query, which lets two of these reflect "
    "packets at each other",
)

# It is not a resolver: no upstream, no cache, no recursion offered.
for forbidden, why in (
    ("recursion available", "claiming recursion invites clients to use it as a resolver"),
    ("upstream", "an upstream would make this a resolver for the recovery AP"),
):
    require(
        forbidden not in portal.lower(),
        f"the captive portal appears to implement {forbidden}: {why}",
    )

# --- It must never block the access point from coming up --------------------

# The recovery AP is the guaranteed way back into a controller. Refusing to
# bring it up because a convenience failed to start would create the exact
# lockout it exists to prevent.
require(
    re.search(r"ESP_RETURN_ON_ERROR\(\s*captive_portal_start", manager) is None,
    "a captive portal failure aborts bringing the recovery access point on air, "
    "which turns a lost convenience into a lost controller",
)
require(
    "Captive portal did not start" in manager,
    "a captive portal failure is silent; it must be logged, because the symptom "
    "is a setup page that does not open by itself",
)

if failures:
    print("Captive portal contract FAILED:")
    for failure in failures:
        print(f"  - {failure}")
    sys.exit(1)

print(
    "Captive portal contract passed (bound to the AP address only, bounds-checked, "
    "answers A/IN and invents nothing, never blocks the recovery AP)"
)
