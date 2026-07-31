#!/usr/bin/env python3
"""Re-opening the commissioning wizard must not destroy what is commissioned.

The wizard writes live device configuration. Before this contract existed it
opened on tuning() defaults regardless of what was stored and its save wrote
those defaults over the controller: on a live unit a meter reading 372 kW became
one reading 25 kW, because register 58 was replaced by the default 0 and scale
0.00001 by the default 0.001. The engineer saw a blank form, so nothing on
screen said a working configuration was about to be discarded.

Four properties keep that from recurring. None is visible from reading one
function, and all four are cheap to break by accident.

1. IT READS BEFORE IT WRITES. Opening the wizard on an empty draft loads the
   stored configuration from the engineering-gated GET endpoints.

2. EVERY WRITTEN FIELD IS AN IMPORTED FIELD. A key that the save sends but the
   import never reads is a key the wizard silently replaces with a default it
   never displayed. The two sets are compared directly.

3. A VALUE IT CANNOT INTERPRET IS NOT GUESSED. An enum outside this build's
   tables is recorded as unknown, shown as unknown, and withheld from the save
   so the stored value survives -- rather than being coerced to entry zero,
   which would be a plausible-looking wrong answer with nothing to question.

4. A PARTIAL WRITE DOES NOT DELETE THE OTHER SLOTS. POST /api/meters/config and
   POST /api/inverters/config both memset the stored array and refill it from
   the request body, so the body's LENGTH becomes the new device count. The
   wizard used to post exactly one entry into slot 0 no matter how many devices
   were commissioned, which deleted every other one: on a two-meter site,
   qualifying the grid meter removed the generator meter.

Asserted against comment-stripped source, so a promise made only in a comment
cannot satisfy any of it.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
WIZARD = ROOT / "web" / "commissioning-release-v3.js"
PRODUCT_MODE = ROOT / "web" / "product-mode.js"

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"^\s*//[^\n]*", " ", text, flags=re.MULTILINE)


wizard = strip_comments(WIZARD.read_text(encoding="utf-8", errors="replace"))
product_mode = strip_comments(PRODUCT_MODE.read_text(encoding="utf-8", errors="replace"))

# --- 1. It reads before it writes ----------------------------------------

for route in ("/api/meters/config", "/api/inverters/config"):
    require(
        f"api('{route}')" in wizard,
        f"the wizard never issues a plain GET to {route}; it cannot know what is "
        f"already commissioned and will open on defaults",
    )

require(
    re.search(r"function start\(\)\{[^}]*importCommissioned", wizard) is not None,
    "start() does not call importCommissioned(); opening the wizard would not "
    "load the stored configuration",
)

# Only on an empty draft. Importing over a draft that has devices in it would
# discard work in progress without asking, which is the same class of surprise.
require(
    re.search(r"!state\.imported&&!state\.devices\.length\)importCommissioned", wizard)
    is not None,
    "the automatic import is not conditioned on an empty draft; it would replace "
    "in-progress work without asking",
)

require(
    'data-action="import"' in wizard,
    "there is no explicit reload control, so an engineer holding a stale draft "
    "has no way to resync it with the controller",
)

# The endpoint is engineering-gated. Without a scope entry the client would
# spend a socket on a guaranteed 401.
require(
    "'/api/meters/config'" in product_mode
    and re.search(
        r"\{\s*path:\s*'/api/meters/config',\s*routes:\s*\[[^\]]*'commissioning'", product_mode
    )
    is not None,
    "product-mode.js does not list /api/meters/config as engineering-only for the "
    "commissioning route",
)

# --- 2. Every written field is an imported field --------------------------

# Keys the meter save sends, taken from the object literal it posts.
save_block = re.search(r"const cfg=\{(.*?)\};", wizard, re.DOTALL)
require(save_block is not None, "the meter save object literal could not be located")

import_fn = re.search(r"function meterFromConfig\(m\)\{(.*?)\nfunction ", wizard, re.DOTALL)
require(import_fn is not None, "meterFromConfig() could not be located")

if save_block is not None and import_fn is not None:
    written = set(re.findall(r"([a-z_]+):", save_block.group(1)))
    read = set(re.findall(r"m\.([a-z_]+)", import_fn.group(1)))
    require(
        len(written) >= 10,
        f"only {len(written)} written meter keys were found; the extraction "
        f"pattern has probably stopped matching and this check is vacuous",
    )
    missing = sorted(written - read)
    require(
        not missing,
        f"the wizard writes {missing} but never imports them, so re-opening it "
        f"replaces those stored values with this build's defaults",
    )

# --- 3. It does not guess a value it cannot interpret ---------------------

require(
    "d.unknown.push('data_type')" in wizard and "d.unknown.push('word_order')" in wizard,
    "an unrecognised data type or word order is not recorded as unknown; it "
    "would be coerced to entry zero and presented as though it were read",
)

require(
    re.search(r"\(d\.unknown\|\|\[\]\)\.forEach\(key=>\{delete cfg\[key\];\}\)", wizard)
    is not None,
    "fields marked unknown are not withheld from the save; the wizard would "
    "write a value it could not display",
)

require(
    "could not be interpreted" in wizard,
    "nothing on screen tells the engineer that a stored value could not be "
    "interpreted, so the default shown would read as the stored value",
)

# An unknown field must not become permanently uneditable: deliberately changing
# it has to clear the mark, or the engineer can never correct it.
require(
    re.search(r"d\.unknown=d\.unknown\.filter\(key=>!changed\[key\]\)", wizard) is not None,
    "changing a field marked unknown does not clear the mark, so it would be "
    "withheld from every future save and could never be corrected",
)

# The enum tables are declared once and used in both directions. Two independent
# copies are how a value gets read back as something other than what was written.
require(
    wizard.count("const TYPE_NAMES=") == 1 and wizard.count("const TYPE_CODES=") == 1,
    "the data-type tables are not declared exactly once each",
)
require(
    "const typeMap=" not in wizard and "const orderMap=" not in wizard,
    "a local enum map survives alongside the shared tables; the two can diverge "
    "and a value would be read back as something other than what was written",
)

# --- 4. A partial write does not delete the other slots -------------------

require(
    "JSON.stringify({meters:[cfg]})" not in wizard,
    "the wizard posts a single-element meters array; POST /api/meters/config "
    "refills the stored array from the body, so this deletes every other meter",
)

require(
    re.search(r"const meters=\[\];for\(let i=0;i<Math\.max\(slot\+1,others\.length\)", wizard)
    is not None,
    "the meter save does not carry the untouched slots through by index",
)

require(
    re.search(r"for\(let i=slot\+1;i<all\.length;i\+=1\)", wizard) is not None,
    "the inverter save does not carry slots after the edited one through; "
    "they would be dropped from the array and deleted",
)

# The rated-power fallback that invented a 100 kW machine is gone.
require(
    "?? 100)" not in wizard and "rated_kw ?? seen?.rated_power_kw" not in wizard,
    "the inverter save still falls back to a literal rated power; an inverter "
    "whose rating could not be read would be commissioned as a value nobody entered",
)
require(
    "is never assumed" in wizard,
    "there is no refusal path for an unknown rated power",
)

if failures:
    print("Commissioning re-entry contract FAILED:")
    for failure in failures:
        print(f"  - {failure}")
    sys.exit(1)

print("Commissioning re-entry contract passed (reads before writing, imports every "
      "written field, refuses to guess, preserves untouched slots)")
