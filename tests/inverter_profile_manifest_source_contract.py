from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/inverter_profile_api.c").read_text(encoding="utf-8")
STORE = (ROOT / "components/inverter_manager/inverter_profile_store.c").read_text(encoding="utf-8")
HEADER = (ROOT / "components/inverter_manager/include/inverter_profile_store.h").read_text(encoding="utf-8")
GUARD = (ROOT / "components/web_server/inverter_profile_store_guard.c").read_text(encoding="utf-8")
WEB = (ROOT / "web/inverter-profiles.js").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require('"/api/inverter-profile-manifest"' in API,
        "profile assignment manifest endpoint is missing")
require(API.count('"/api/inverter-profile-manifest"') >= 2,
        "manifest must support both export and import")
require("profile_manifest_get" in API and "profile_manifest_post" in API,
        "manifest handlers are incomplete")
require("PROFILE_MANIFEST_MAX_BODY" in API and "http_json_parse_bounded" in API,
        "manifest import must use a bounded JSON parser")
require('PROFILE_MANIFEST_KIND "compiled_profile_assignment_manifest"' in API,
        "manifest kind must distinguish assignment backup from profile definitions")
require('"dynamic_profile_definitions_importable", false' in API,
        "export must state that dynamic profile definitions are not importable")
require('"qualification_importable", false' in API,
        "export must state that qualification is not importable")
require('"production_approval_importable", false' in API,
        "export must state that production approval is not importable")
require('"dynamic_profile_definitions_imported", false' in API and
        '"qualification_imported", false' in API and
        '"production_approval_imported", false' in API,
        "import response must prove no authority was imported")
require("profile_definition_fingerprint" in API and "fnv1a64-v1" in API,
        "manifest must bind assignments to the exact compiled profile definition")
require("strcmp(fingerprint_item->valuestring, expected_fingerprint)" in API,
        "manifest import must reject a profile-definition fingerprint mismatch")
for field in ("manufacturer", "model_family", "protocol", "connection",
              "qualification", "manual_reference"):
    require(f'manifest_string_matches(item, "{field}"' in API,
            f"manifest import must bind {field} to the compiled profile")
require("seen[APP_MAX_INVERTERS]" in API and
        "cJSON_GetArraySize(assignments) != APP_MAX_INVERTERS" in API,
        "manifest import must require one unique assignment for every channel")
require("inverter_profiles_find(profile_item->valuestring)" in API,
        "manifest import must select only a known compiled profile")
require("inverter_profile_store_set_all_guarded(&manifest)" in API,
        "HTTP import must use the live-control guarded bulk store")

require("inverter_profile_assignment_manifest_t" in HEADER and
        "inverter_profile_store_get_all" in HEADER and
        "inverter_profile_store_set_all" in HEADER,
        "profile store must expose a typed bulk assignment API")
require("manifest_valid(manifest)" in STORE,
        "bulk store must validate the complete manifest before persistence")
require("inverter_profiles_find(profile_id)" in STORE,
        "bulk store must reject unknown compiled profile ids")
require("persist(&next)" in STORE,
        "bulk profile assignment must use one persistent manifest commit")
require("config->control.enabled = false" in STORE,
        "bulk profile assignment must disable persisted automatic control")
require("return inverter_profile_store_set_all(&manifest);" in STORE,
        "single assignment must reuse the atomic bulk-store safety path")
require("control_engine_force_disable();" in GUARD and
        "inverter_profile_store_set_all(manifest)" in GUARD,
        "manifest HTTP guard must stop the running control task before persistence")

# Import is intentionally assignment-only. These profile-definition fields may
# exist in the compiled catalogue/fingerprint code, but the JSON importer must
# never read them as values supplied by the backup file.
for forbidden_key in ("identity_address", "active_power_address", "power_limit_address",
                      "status_register", "raw_units_per_percent", "readback_tolerance_percent"):
    require(f'cJSON_GetObjectItemCaseSensitive(item, "{forbidden_key}")' not in API,
            f"manifest importer must not accept dynamic profile field {forbidden_key}")

require("inverterProfileManifestExport" in WEB and "inverterProfileManifestImport" in WEB,
        "Engineering UI must expose assignment backup and restore actions")
require("window.confirm" in WEB and "Automatic PV-DG control will be disabled" in WEB,
        "UI must warn before importing an assignment manifest")
require("dynamic_profile_definitions_imported" in WEB and
        "qualification_imported" in WEB and "production_approval_imported" in WEB,
        "UI must reject an unsafe import response")
require("python3 tests/inverter_profile_manifest_source_contract.py" in WORKFLOW,
        "Firmware/Web CI must enforce the profile manifest safety contract")

print("Inverter profile assignment manifest source contract passed")