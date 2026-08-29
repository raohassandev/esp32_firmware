#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source_path = root / "boards/waveshare_esp32_s3_touch_lcd_5/screen/pages/commissioning_screen.c"
test_path = root / "tests/waveshare_flicker_source_contract.py"

source = source_path.read_text(encoding="utf-8")
test = test_path.read_text(encoding="utf-8")

anchor = "static void render(void);\n"
helper = r'''

static bool gate_snapshot_equal(const screen_commissioning_snapshot_t *a,
                                const screen_commissioning_snapshot_t *b)
{
    if (!a || !b) return false;
    return a->valid == b->valid &&
           a->commissioned == b->commissioned &&
           strcmp(a->scope, b->scope) == 0 &&
           a->production_qualified == b->production_qualified &&
           a->automatic_control_permitted == b->automatic_control_permitted &&
           a->command_authority == b->command_authority &&
           a->prerequisite_count == b->prerequisite_count &&
           a->satisfied_count == b->satisfied_count &&
           a->unmet_count == b->unmet_count &&
           strcmp(a->first_unmet, b->first_unmet) == 0 &&
           strcmp(a->first_unmet_title, b->first_unmet_title) == 0 &&
           strcmp(a->first_unmet_detail, b->first_unmet_detail) == 0 &&
           strcmp(a->summary, b->summary) == 0 &&
           strcmp(a->inhibit_reason, b->inhibit_reason) == 0;
}
'''
if "static bool gate_snapshot_equal(" not in source:
    if anchor not in source:
        raise SystemExit("render declaration anchor not found")
    source = source.replace(anchor, anchor + helper, 1)

old_apply = r'''void commissioning_screen_apply_gate(const screen_commissioning_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_ui.gate = *snapshot;
    else memset(&s_ui.gate, 0, sizeof(s_ui.gate));
    if (s_ui.root && s_ui.config.unlocked && s_ui.step == 7U) render();
}
'''
new_apply = r'''void commissioning_screen_apply_gate(const screen_commissioning_snapshot_t *snapshot)
{
    screen_commissioning_snapshot_t next = {0};
    if (snapshot && snapshot->valid) next = *snapshot;

    const bool changed = !gate_snapshot_equal(&s_ui.gate, &next);
    s_ui.gate = next;
    if (changed && s_ui.root && s_ui.config.unlocked && s_ui.step == 7U) render();
}
'''
if old_apply in source:
    source = source.replace(old_apply, new_apply, 1)
elif new_apply not in source:
    raise SystemExit("commissioning apply_gate block not found")

if 'COMMISSIONING = BASE / "pages/commissioning_screen.c"' not in test:
    test = test.replace(
        'ALARMS = BASE / "pages/alarms_screen.c"\n',
        'ALARMS = BASE / "pages/alarms_screen.c"\nCOMMISSIONING = BASE / "pages/commissioning_screen.c"\n',
        1,
    )
if 'commissioning = COMMISSIONING.read_text(encoding="utf-8")' not in test:
    test = test.replace(
        'alarms = ALARMS.read_text(encoding="utf-8")\n',
        'alarms = ALARMS.read_text(encoding="utf-8")\ncommissioning = COMMISSIONING.read_text(encoding="utf-8")\n',
        1,
    )
if 'commissioning_apply = function_body(' not in test:
    test = test.replace(
        'event_apply = function_body(alarms, "alarms_screen_apply_events", "alarms_screen_show_unavailable")\n',
        'event_apply = function_body(alarms, "alarms_screen_apply_events", "alarms_screen_show_unavailable")\ncommissioning_apply = function_body(commissioning, "commissioning_screen_apply_gate", "commissioning_screen_apply_status")\n',
        1,
    )
contract_anchor = 'assert "lv_obj_clean" not in alarms, "Alarms page must not tear down lists on unavailable transitions either"\n'
contract = '''\n# Commissioning may rebuild on explicit navigation/config edits, but an unchanged\n# periodic gate refresh must not destroy/recreate the Review page widget tree.\nassert "gate_snapshot_equal" in commissioning\nassert "const bool changed = !gate_snapshot_equal" in commissioning_apply\nassert "if (changed && s_ui.root && s_ui.config.unlocked && s_ui.step == 7U) render();" in commissioning_apply\n'''
if 'const bool changed = !gate_snapshot_equal' not in test:
    if contract_anchor not in test:
        raise SystemExit("flicker contract insertion anchor not found")
    test = test.replace(contract_anchor, contract_anchor + contract, 1)

source_path.write_text(source, encoding="utf-8")
test_path.write_text(test, encoding="utf-8")
print("Lane A commissioning refresh patch applied")
