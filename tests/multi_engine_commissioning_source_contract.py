#!/usr/bin/env python3
"""A multi-engine plant must be commissionable through the product.

The firmware supports up to three gensets in parallel with per-engine ratings,
minimum loading, roles and an explicit kW load-sharing mode. The commissioning
gate enforces every one of those and the aggregate minimum-loading floor is
computed from them. But GET/POST /api/solar-grid/config carried engine slot 0
only, so the feature was enforced and unreachable: the controller correctly
refused to command PV on a two- or three-engine plant, and no engineer had any
way to supply what it was asking for.

This contract pins the hole closed, and pins the four properties that must
survive any future edit to it:

  * ABSENT MEANS UNCHANGED. A body that omits a field leaves the stored value
    alone. Silently zeroing a generator rating would close the commissioning
    gate on a working plant; silently zeroing a base-load setpoint would do it
    for a base-loaded engine.
  * BACKWARD COMPATIBILITY. A body carrying only the engine-0 scalars this API
    has always accepted still works and still means exactly what it meant.
  * NO MODE IS OFFERED AS IF SELECTING IT WOULD WORK. Droop is refused by the
    control engine, permanently. Whether a mode is supported comes from the
    control engine's own predicate, and the reason for a refusal comes from the
    commissioning gate's own sentence - never from a list or a phrase kept in
    the API or the browser.
  * SAFETY SENTENCES ARE RENDERED, NOT PARAPHRASED. The reason slugs already
    carry operator-facing sentences. The UI reproduces them byte for byte, and
    this contract fails if one character drifts.
"""

import re
import sys
from pathlib import Path
import sys as _sys, pathlib as _pathlib
_sys.path.insert(0, str(_pathlib.Path(__file__).resolve().parent))
import bundle_membership as bundle

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/solar_grid_api.c").read_text(encoding="utf-8")
STATUS_API = (ROOT / "components/web_server/solar_grid_status_api.c").read_text(encoding="utf-8")
SG_H = (ROOT / "components/solar_grid_config/include/solar_grid_config.h").read_text(encoding="utf-8")
FLEET_H = (ROOT / "components/control_engine/include/generator_fleet_limit.h").read_text(encoding="utf-8")
FLEET_C = (ROOT / "components/control_engine/generator_fleet_limit.c").read_text(encoding="utf-8")
CONTROL_C = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
GATE_C = (ROOT / "components/commissioning_gate/commissioning_gate.c").read_text(encoding="utf-8")
JS = (ROOT / "web/solar-grid.js").read_text(encoding="utf-8")
APP_JS = (ROOT / "web/app.js").read_text(encoding="utf-8")
APP_CSS = (ROOT / "web/app.css").read_text(encoding="utf-8")
THEME_CSS = (ROOT / "web/theme.css").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


def section(source, start, end, label):
    if start not in source:
        failures.append(f"{label}: cannot find {start!r}")
        return ""
    begin = source.index(start)
    if end not in source[begin:]:
        failures.append(f"{label}: cannot find {end!r} after {start!r}")
        return source[begin:]
    return source[begin:source.index(end, begin)]


# ==================================================================== the model
#
# The API can only be pinned against fields the persisted model actually has.

for token in ("solar_grid_load_sharing_t", "solar_grid_engine_role_t",
              "solar_grid_config_generator(", "solar_grid_config_load_sharing_mode(",
              "solar_grid_config_engine_role(", "solar_grid_config_engine_base_load_kw(",
              "solar_grid_load_sharing_name(", "solar_grid_engine_role_name("):
    require(token in SG_H, f"the persisted generator policy is missing: {token}")
require("SOLAR_GRID_MAX_GENERATORS 3u" in SG_H,
        "the engine slot count must stay declared in the persisted model")


# ================================================================== GET, config
#
# A field this API never serialises cannot be read back, so it cannot be edited
# and therefore cannot be commissioned: it stays zero for the life of the unit.
# That is exactly the hole this change closes, so every new field is pinned.

CONFIG_JSON = section(API, "static cJSON *config_json(", "static bool read_bool(",
                      "config_json")
require("generator_policy_json(root, config);" in CONFIG_JSON,
        "GET /api/solar-grid/config must serialise the multi-engine generator policy; "
        "engine slot 0 alone is what made a multi-engine plant uncommissionable")

POLICY_JSON = section(API, "static void generator_policy_json(", "static cJSON *config_json(",
                      "generator_policy_json")
for key in ("load_sharing_mode", "load_sharing_mode_name", "load_sharing_mode_supported",
            "load_sharing_mode_reason", "engine_slot_count", "engines_in_service",
            "load_sharing_mode_required"):
    require(f'"{key}"' in POLICY_JSON,
            f"GET /api/solar-grid/config must report {key}")

ENGINES_JSON = section(API, "static void engines_json(", "/* The multi-engine generator policy",
                       "engines_json")
for key in ("slot", "in_service", "in_service_derived", "rated_kw",
            "minimum_loading_percent", "reserve_kw", "reverse_power_margin_kw",
            "minimum_loading_kw", "role", "role_name", "base_load_kw"):
    require(f'"{key}"' in ENGINES_JSON,
            f"each engine slot in GET /api/solar-grid/config must report {key}")
require("solar_grid_config_generator(config, slot)" in ENGINES_JSON,
        "the per-slot view must come from the uniform accessor, not from either "
        "storage half directly: slot 0 lives in the legacy scalars and slots 1.. in "
        "the appended array, and a reader that walked one of them would miss engines")
require("SOLAR_GRID_MAX_GENERATORS" in ENGINES_JSON,
        "every engine slot the model can describe must be serialised, not a literal count")

# Which modes are supported must be the control engine's own answer.
MODES_JSON = section(API, "static void sharing_modes_json(", "/* One engine slot,",
                     "sharing_modes_json")
require("generator_sharing_mode_supported(mode)" in MODES_JSON,
        "whether a load-sharing mode is supported must come from the control engine's "
        "own predicate; a list maintained in the API would offer a mode the controller "
        "refuses as if selecting it would work")
for key in ("supported", "refused", "reason", "selected", "id", "value"):
    require(f'"{key}"' in MODES_JSON,
            f"each offered load-sharing mode must publish {key}")
require("solar_grid_load_sharing_name(mode)" in MODES_JSON,
        "the mode slug must come from the shared vocabulary")

REFUSAL = section(API, "static const char *sharing_mode_refusal(",
                  "static void sharing_modes_json(", "sharing_mode_refusal")
require("generator_sharing_mode_supported(mode)" in REFUSAL,
        "the refusal predicate must be the control engine's")
require("commissioning_reason_message(" in REFUSAL,
        "the reason a mode is refused must be the commissioning gate's own sentence, "
        "not a phrase composed in the API: an interface must never paraphrase a safety "
        "decision it did not make")
require("COMMISSIONING_REASON_GENERATOR_SHARING_MODE_UNSET" in REFUSAL and
        "COMMISSIONING_REASON_GENERATOR_SHARING_MODE_UNSUPPORTED" in REFUSAL,
        "an uncommissioned mode and a refused mode are different findings and must "
        "carry different sentences")

# The three enums that must agree numerically, proved at compile time rather than
# assumed: if they diverged this API would report a refused mode as supported.
require("_Static_assert" in API and "GENERATOR_SHARING_DROOP" in API,
        "solar_grid_api.c must _Static_assert that the persisted load-sharing enum and "
        "the control engine's agree value for value")
require("GENERATOR_ENGINE_ROLE_BASE_LOAD" in API,
        "the engine-role enums must be asserted to agree as well")


# ================================================================= POST, config

CONFIG_POST = section(API, "static esp_err_t config_post(",
                      "esp_err_t solar_grid_api_register(", "config_post")
require("parse_generator_policy(root, &next, field_error," in CONFIG_POST,
        "POST /api/solar-grid/config must accept the multi-engine generator policy")
# Backward compatibility: the four engine-0 scalars are still read at the top level
# and still mean what they meant. A client that knows nothing about engines[] works.
for field in ("generator_rated_kw", "generator_minimum_loading_percent",
              "generator_reserve_kw", "generator_reverse_power_margin_kw"):
    require(f'read_limit(root, "{field}"' in CONFIG_POST,
            f"POST must keep accepting the engine-0 scalar {field} at the top level; "
            f"a body carrying only today's fields must keep working")
require(CONFIG_POST.index('read_limit(root, "generator_rated_kw"') <
        CONFIG_POST.index("parse_generator_policy(root"),
        "the engine-0 scalars must be applied before engines[], so a body carrying "
        "both settles on the per-engine value, which is the more specific statement")

POLICY_PARSE = section(API, "static bool parse_generator_policy(",
                       "static esp_err_t config_get(", "parse_generator_policy")
require('read_uint(root, "load_sharing_mode", SOLAR_GRID_SHARING_MODE_MAX' in POLICY_PARSE,
        "the load-sharing mode must be accepted, bounded by the enum rather than a literal")
require('cJSON_GetObjectItemCaseSensitive(root, "engines")' in POLICY_PARSE,
        "POST must accept an engines array")
require("if (!engines) return true;" in POLICY_PARSE,
        "an absent engines array must leave every stored engine untouched: silently "
        "zeroing a commissioned rating would close the gate on a working plant")

ENGINE_PARSE = section(API, "static bool parse_engine(",
                       "/* The kW load-sharing mode and the engine slots.", "parse_engine")
for key in ("slot", "rated_kw", "minimum_loading_percent", "reserve_kw",
            "reverse_power_margin_kw", "base_load_kw", "role", "in_service"):
    require(f'"{key}"' in ENGINE_PARSE,
            f"POST must accept the per-engine field {key}")
# Bounded by the SAME ceilings the engine-0 fields use, not new ones.
for key in ("rated_kw", "reserve_kw", "reverse_power_margin_kw", "base_load_kw"):
    call = ENGINE_PARSE[ENGINE_PARSE.index(f'engine_limit(item, "{key}"'):]
    require("SOLAR_GRID_KW_MAX" in call[:200],
            f"per-engine {key} must be bounded by the shared 1000000 kW ceiling, the "
            f"same one the engine-0 scalars use")
percent = ENGINE_PARSE[ENGINE_PARSE.index('engine_limit(item, "minimum_loading_percent"'):]
require("SOLAR_GRID_PERCENT_MAX" in percent[:200],
        "per-engine minimum_loading_percent must be bounded at 100")
require("SOLAR_GRID_ENGINE_ROLE_MAX" in ENGINE_PARSE,
        "the engine role must be bounded by the enum rather than a literal")
require("generator_extra[slot - 1U]" in ENGINE_PARSE,
        "slots 1.. are stored in the appended array at index slot-1; writing slot "
        "directly would put engine 2's rating in engine 3's field")

# Zero remains legal for every per-engine limit, exactly as for engine 0: a zero
# rating is the uncommissioned state that holds PV at zero, not a validation error.
require("engine_limit(" in ENGINE_PARSE and "read_limit(" in
        section(API, "static bool engine_limit(", "/*\n * One engine slot from the request body",
                "engine_limit"),
        "the per-engine reader must defer to read_limit, which accepts zero and leaves "
        "an absent key untouched")
for rejection in ("rated_kw <= 0.0f", "rated_kw == 0"):
    require(rejection not in ENGINE_PARSE,
            f"a zero per-engine rating must be accepted, not rejected: {rejection}")

# Slot 0's in-service flag is derived and must be refused, never quietly discarded.
require("stored_in_service" in ENGINE_PARSE,
        "slot 0 has no stored in-service flag and the parser must know it")
require("Solar-Grid engine 0 is in service exactly when its rated_kw is" in ENGINE_PARSE,
        "a request that asks slot 0 to be in service against its rating must be "
        "REFUSED with the reason. Accepting and ignoring it would show a slot out of "
        "service that the controller still counts")
require(ENGINE_PARSE.index('engine_limit(item, "rated_kw"') <
        ENGINE_PARSE.index('"in_service"'),
        "the in-service check must run after the rating this request settles on, or "
        "slot 0 would be judged against the previous rating")

# The write must still defer to the full model validator and must still never arm
# automatic control. Both are pinned by the older contract; restated here because
# this change touches the same handler.
require("solar_grid_config_valid(&next)" in CONFIG_POST,
        "the POST handler must still defer to the full model validator")
require("application->control.enabled = false" in CONFIG_POST,
        "a generator policy write must persist automatic control disabled")
require("control_engine_force_disable();" in CONFIG_POST,
        "a generator policy write must latch the running control task disabled")

# Nothing on this path may perform Modbus I/O in the HTTP handler.
for name, text in (("solar_grid_api.c", API), ("solar_grid_status_api.c", STATUS_API)):
    require("meter_manager_read_registers" not in text,
            f"{name} must not issue a Modbus transaction from an HTTP handler")
    require("inverter_manager_write" not in text,
            f"{name} must not write to an inverter from an HTTP handler")


# ======================================================= the runtime aggregate

FLEET_STATUS = section(STATUS_API, "static void add_generator_fleet(",
                       "static esp_err_t status_get(", "add_generator_fleet")
require("add_generator_fleet(root, &status);" in STATUS_API,
        "the generator fleet must be published where control status already is")
require("generator_fleet_limit_evaluate(&input)" in FLEET_STATUS,
        "the floor must come from the control loop's own function, not from a second "
        "implementation in the web server: two implementations of a safety floor are "
        "two answers")
for field in ("sharing_mode", "base_loaded_count", "base_load_total_kw",
              "online_count", "online_rated_kw", "minimum_loading_kw",
              "required_generator_kw"):
    require(f'"{field}"' in FLEET_STATUS,
            f"the published fleet limit must carry {field}")
require('"basis"' in FLEET_STATUS and "every_engine_in_service_treated_as_on_the_bus" in FLEET_STATUS,
        "the published floor must say what it is computed over. A floor without its "
        "basis reads as the cycle-by-cycle answer, which it is not")
require('cJSON_AddBoolToObject(floor_object, "safe_pv_published", false)' in FLEET_STATUS,
        "no safe-PV figure may be published from an evaluation whose plant load is "
        "zero: a real number answering an imaginary question")
# The runtime verdict is now published, so the assertion that the gap be NAMED is
# replaced by one that the gap be CLOSED. It must come from the control loop itself,
# not be recomputed here: a recomputation over the commissioned set answers "what
# would the floor be if every in-service engine were running", and offering that as
# the runtime answer would misreport why PV is being held down.
require("control_engine_get_generator_fleet(&live)" in FLEET_STATUS,
        "the runtime floor must be read from the control loop, not recomputed")
require('cJSON_AddBoolToObject(fleet, "runtime_fleet_limit_published", have_live)' in FLEET_STATUS,
        "whether a runtime verdict exists must be reported from the accessor's return, "
        "so 'no verdict yet' stays distinguishable from 'a verdict of zero'")
require('"engines_the_control_loop_believed_online"' in FLEET_STATUS,
        "the runtime floor must state its basis, so it cannot be confused with the "
        "commissioned-set floor published beside it")
require('add_finite(runtime, "safe_pv_kw", live.safe_pv_kw)' in FLEET_STATUS,
        "the runtime floor MAY publish safe PV, unlike the commissioned-set floor: it "
        "was computed against the real plant load and is therefore a real answer")
# And the loop must actually publish it every cycle, including when it is not in
# generator mode, or a verdict from a previous source mode would stand as current.
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
require("store_fleet_limit(&fleet_limit);" in CONTROL,
        "the control loop must publish its fleet verdict")
require(CONTROL.index("store_fleet_limit(&fleet_limit);") >
        CONTROL.index("generator_safe_limit_kw = fleet_limit.known"),
        "the verdict must be published after it is used, so what is published is what "
        "the loop acted on")
require('"runtime_reason"' in FLEET_STATUS,
        "the control loop's own inhibit sentence must travel with the fleet block")
require("modbus_io_in_http_handler" in FLEET_STATUS,
        "the fleet block must state that it performed no Modbus I/O")
# One definition of stale. This handler publishes the observation and the
# configured timeout; it must not re-decide freshness.
require("meter_stale_timeout_ms" in FLEET_STATUS,
        "the configured staleness timeout must be published so a reader applies the "
        "control loop's one rule rather than inventing a second one")
require("sample_fresh = limits.enabled" in FLEET_STATUS,
        "the commissioned-set evaluation must place every in-service engine on the "
        "bus, which is the largest denominator and therefore the largest floor")
require("meter_sample_fresh" not in STATUS_API,
        "the status API must not carry a second copy of the control loop's freshness "
        "rule; one definition of stale governs the whole product")
require("policy_readable" in FLEET_STATUS,
        "an unreadable generator policy must be reported as unreadable, never as a "
        "commissioned fleet of zero engines")


# ============================================ reason slugs, rendered not reworded
#
# Both slug vocabularies are extracted from the firmware and checked against what
# the interface actually renders. The gate's sentences reach the browser over the
# wire; the aggregate limit's are copied into the browser module, so those are
# compared character for character.

ENUM_BLOCK = section(FLEET_H, "typedef enum {\n    GENERATOR_FLEET_OK",
                     "} generator_fleet_reason_t;", "generator_fleet_reason_t")
ENUM_NAMES = re.findall(r"^\s{4}(GENERATOR_FLEET_[A-Z_]+)\s*(?:=\s*\d+\s*)?,",
                        ENUM_BLOCK, re.MULTILINE)
ENUM_NAMES = [name for name in ENUM_NAMES if name != "GENERATOR_FLEET_REASON_COUNT"]
IDS_BLOCK = section(FLEET_C, "REASON_IDS[GENERATOR_FLEET_REASON_COUNT] = {", "};",
                    "REASON_IDS")
SLUGS = re.findall(r'"([a-z_]+)"', IDS_BLOCK)
require(len(ENUM_NAMES) == len(SLUGS) and len(SLUGS) >= 11,
        f"the fleet reason enum ({len(ENUM_NAMES)}) and its slug table ({len(SLUGS)}) "
        "must stay the same length, or a reason would be reported under another's name")
SLUG_FOR = dict(zip(ENUM_NAMES, SLUGS))

# Every fleet reason named in the brief must exist as a slug.
for slug in ("sharing_mode_unset", "sharing_mode_unsupported",
             "base_load_setpoint_unknown", "base_load_below_minimum",
             "no_swing_engine"):
    require(slug in SLUGS, f"the aggregate limit must publish the reason slug {slug}")

# The firmware's sentences, and the browser's copies of them.
INHIBIT = section(CONTROL_C, "static const char *generator_fleet_inhibit(",
                  "static meter_role_assignment_t current_role_assignment(",
                  "generator_fleet_inhibit")
FIRMWARE_SENTENCES = {}
for name, sentence in re.findall(
        r"case (GENERATOR_FLEET_[A-Z_]+):\s*\n\s*return \"((?:[^\"\\]|\\.)*)\";", INHIBIT):
    if name in SLUG_FOR:
        FIRMWARE_SENTENCES[SLUG_FOR[name]] = sentence
require(len(FIRMWARE_SENTENCES) >= 10,
        f"only {len(FIRMWARE_SENTENCES)} aggregate-limit sentences could be extracted "
        "from control_engine.c; the contract cannot prove the UI renders them")

JS_MAP = section(JS, "const FLEET_REASON_SENTENCES = {", "};", "FLEET_REASON_SENTENCES")
JS_SENTENCES = {}
for key, single, double in re.findall(
        r"(\w+):\s*(?:'((?:[^'\\]|\\.)*)'|\"((?:[^\"\\]|\\.)*)\")", JS_MAP):
    JS_SENTENCES[key] = single if single else double

for slug, sentence in sorted(FIRMWARE_SENTENCES.items()):
    require(slug in JS_SENTENCES,
            f"the interface has no sentence for the aggregate-limit reason {slug}; an "
            f"unrendered reason is a controller refusing to command PV with nothing on "
            f"screen saying why")
    if slug in JS_SENTENCES:
        require(JS_SENTENCES[slug] == sentence,
                f"the interface's sentence for {slug} is not the firmware's, verbatim.\n"
                f"  firmware: {sentence!r}\n"
                f"  interface: {JS_SENTENCES[slug]!r}\n"
                "A paraphrase of a safety decision is a different decision.")
for slug in JS_SENTENCES:
    require(slug == "ok" or slug in SLUGS,
            f"the interface carries a sentence for {slug}, which is not a reason the "
            f"firmware can report")

# The commissioning gate's five new reasons carry operator-facing sentences, and the
# gate API hands them to the browser, which renders them verbatim.
GATE_IDS = section(GATE_C, "REASON_IDS[COMMISSIONING_REASON_COUNT] = {", "};", "gate REASON_IDS")
GATE_MESSAGES = section(GATE_C, "REASON_MESSAGES[COMMISSIONING_REASON_COUNT] = {", "};",
                        "gate REASON_MESSAGES")
gate_slugs = re.findall(r'"([a-z_]+)"', GATE_IDS)
gate_messages = re.findall(r'"((?:[^"\\]|\\.)*)"', GATE_MESSAGES)
require(len(gate_slugs) == len(gate_messages),
        "every commissioning reason slug must have a message beside it")
GATE_TEXT = dict(zip(gate_slugs, gate_messages))
for slug in ("generator_sharing_mode_unset", "generator_sharing_mode_unsupported",
             "generator_base_load_unknown", "generator_base_load_below_minimum",
             "generator_no_swing_engine"):
    require(slug in GATE_TEXT, f"the commissioning gate must publish the reason {slug}")
    require(len(GATE_TEXT.get(slug, "")) > 40,
            f"the gate reason {slug} must carry an operator-facing sentence, not a slug")
require("commissioning_reason_message(status.results[i].reason)" in
        (ROOT / "components/web_server/commissioning_gate_api.c").read_text(encoding="utf-8"),
        "the gate API must hand the firmware's own sentence to the client")
require("detail.textContent = detailText;" in APP_JS,
        "the interface must render the gate's sentence verbatim, with no substitution")

# The Control route must show the gate's verdict on the generator limits, not leave
# an engineer to find it on another page.
require("'generator_limits'" in JS,
        "the generator commissioning panel must show the commissioning gate's own "
        "reason for the generator-limits prerequisite")
require("verbatimText(item.detail)" in JS,
        "that reason must be rendered verbatim")
for key in ("base_load_below_minimum_reason", "base_load_unknown_reason",
            "load_sharing_unset_reason"):
    require(key in JS and key in API,
            f"{key} must be supplied by the firmware and rendered by the interface, "
            f"so the browser never composes a safety sentence of its own")


# ============================================================== the browser form

for control in ("generatorFleetEditor", "generatorSharingMode", "generatorFleetEngines",
                "generatorFleetSave", "generatorFleetMessage"):
    require(control in JS, f"the generator commissioning form is missing {control}")
for suffix in ("InService", "Rated", "Loading", "Reserve", "Margin", "Role", "BaseLoad"):
    require(f"engine${{slot}}{suffix}" in JS,
            f"the per-engine form is missing the {suffix} field")
require("inService.disabled = true;" in JS,
        "engine 1's in-service control must be shown as derived, not offered as a "
        "checkbox the firmware would refuse")
require("if (slot > 0) entry.in_service = engine.inService;" in JS,
        "engine 1's in-service flag must not be sent: the firmware derives it from the "
        "rating and refuses a contradicting request")
require("ENGINE_SLOT_FALLBACK" in JS and "engine_slot_count" in JS,
        "the number of engine slots must follow the firmware's own count")
require("- refused by the controller" in JS,
        "a refused load-sharing mode must be labelled as refused in the picker")
require("item.disabled = !supported" in JS,
        "a refused mode must not be selectable; offering it would let an engineer "
        "commission a mode the controller will not act on")

# The derived floor, stated as a floor with a subject.
for row in ("generatorFloorMode", "generatorFloorOnline", "generatorFloorRating",
            "generatorFloorMinimum", "generatorFloorBaseLoad", "generatorFloorRequired",
            "generatorFloorReason", "generatorFloorRuntime"):
    require(row in JS, f"the derived-floor statement is missing {row}")
require("largest denominator" in JS,
        "the floor must state what it is computed over, or it reads as the "
        "cycle-by-cycle answer it is not")

# The consequence of base load, made explicit.
require("commissioning fault" in JS,
        "the interface must state that a base-loaded engine held below its own minimum "
        "loading is a commissioning fault, not something the controller works around")

# The Control route asset already ships; the panel adds no new asset and no new route.
require(bundle.delivered("solar-grid.js"),
        "the generator commissioning form must live in an already-embedded asset")
require(".uri = " not in section(API, "esp_err_t solar_grid_api_register(",
                                 "\n}\n", "register").replace(
            '.uri = "/api/solar-grid/config", .method = HTTP_GET', "").replace(
            '.uri = "/api/solar-grid/config", .method = HTTP_POST', ""),
        "the multi-engine policy must extend the existing routes rather than add new "
        "ones; HTTP route capacity is finite and an overflow takes the whole web "
        "server down")


# =========================================================== WCAG AA, both themes
#
# Computed from the token values parsed out of the stylesheets, so a token declared
# in one theme only cannot pass: the lookup will not find it.

TOKEN_PATTERN = re.compile(r"(--[a-z0-9-]+)\s*:\s*(#[0-9a-fA-F]{6})\s*;")


def token_block(sheet, opener):
    if opener not in sheet:
        failures.append(f"stylesheet has no {opener!r} block to read tokens from")
        return ""
    return sheet.split(opener, 1)[1].split("\n}", 1)[0]


DARK = dict(TOKEN_PATTERN.findall(token_block(APP_CSS, ":root {")))
LIGHT = dict(TOKEN_PATTERN.findall(token_block(THEME_CSS, 'html[data-theme="light"] {')))


def channel(value):
    fraction = value / 255.0
    return fraction / 12.92 if fraction <= 0.03928 else ((fraction + 0.055) / 1.055) ** 2.4


def luminance(colour):
    text = colour.lstrip("#")
    red, green, blue = (int(text[i:i + 2], 16) for i in (0, 2, 4))
    return 0.2126 * channel(red) + 0.7152 * channel(green) + 0.0722 * channel(blue)


def contrast(first, second):
    high, low = sorted((luminance(first), luminance(second)), reverse=True)
    return (high + 0.05) / (low + 0.05)


def resolved(theme, name, theme_name):
    if name not in theme:
        failures.append(f"{name} is not declared as a literal colour for the "
                        f"{theme_name} theme")
        return None
    return theme[name]


def check_contrast(label, foreground, background, minimum):
    for theme, theme_name in ((DARK, "dark"), (LIGHT, "light")):
        first = resolved(theme, foreground, theme_name)
        second = resolved(theme, background, theme_name)
        if first is None or second is None:
            continue
        ratio = contrast(first, second)
        require(ratio >= minimum,
                f"{theme_name} theme: {label} measures {ratio:.2f}:1 on {background}, "
                f"below the required {minimum}:1")


# Every string in the engine panel is 12-15px, so 4.5:1 applies to all of it.
for token in ("--engine-in-service", "--engine-out-of-service", "--engine-fault",
              "--engine-note"):
    check_contrast(f"{token} text", token, "--engine-surface", 4.5)
check_contrast("engine card body text", "--text", "--engine-surface", 4.5)
check_contrast("engine card secondary text", "--muted", "--engine-surface", 4.5)
# The card boundary is what separates one machine's numbers from another's, and it
# butts onto the panel as well as its own surface. 3:1 in both directions.
check_contrast("engine card boundary on its surface", "--engine-line", "--engine-surface", 3.0)
check_contrast("engine card boundary on the panel", "--engine-line", "--panel", 3.0)

# The three state colours must be three different colours in both themes. Two states
# sharing a value is two commissioning findings the reader cannot separate.
for theme, theme_name in ((DARK, "dark"), (LIGHT, "light")):
    values = [theme.get(token) for token in
              ("--engine-in-service", "--engine-out-of-service", "--engine-fault")]
    if all(values):
        require(len(set(values)) == 3,
                f"{theme_name} theme: the engine states share a colour ({values}); in "
                f"service, not in service and refused are three different findings")

# Hue must not be the only channel. The badge carries a word and the fault line a
# whole sentence, so a reader who cannot separate the colours still gets the answer.
require("badge.textContent = engine.inService ? 'In service' : 'Not in service';" in JS,
        "the in-service state must be carried by a word, not by colour alone")
require(".engine-state.is-out-of-service" in APP_CSS and
        "border-style: dashed" in APP_CSS,
        "an out-of-service card must differ by more than hue")


# ================================================================== CI, or nothing
require("tests/multi_engine_commissioning_source_contract.py" in WORKFLOW,
        "this contract must be registered in the CI workflow to be worth anything")
require("node --check web/solar-grid.js" in WORKFLOW,
        "the browser module carrying the commissioning form must be syntax checked")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print(f"multi-engine commissioning source contract passed "
      f"({len(FIRMWARE_SENTENCES)} aggregate-limit sentences and "
      f"{len(gate_slugs)} gate reasons checked)")
