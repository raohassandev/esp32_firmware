from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


# 1) Commissioning UI: remove retired-schema controls from the visible/editable
# surface. The DTO/backend neutral guards remain so stale callers still fail safe.
path = Path("boards/waveshare_esp32_s3_touch_lcd_5/screen/pages/commissioning_screen.c")
text = path.read_text()

text = replace_once(
    text,
    "Core schema 9 does not persist them yet.",
    "the current Core schema does not persist them.",
    "remove stale schema-version claim",
)

text = replace_once(
    text,
    """    if (s_ui.model) meter->model = lv_dropdown_get_selected(s_ui.model);\n    if (s_ui.phase_basis) meter->phase_basis = lv_dropdown_get_selected(s_ui.phase_basis);\n\n""",
    "",
    "remove legacy meter reads",
)

text = replace_once(
    text,
    """    if (s_ui.failsafe_ms) {\n        unsigned long value = 0U;\n        if (!parse_ulong(s_ui.failsafe_ms, 0U, 3600000U, &value)) return false;\n        inverter->comms_failsafe_ms = (uint32_t)value;\n    }\n""",
    "",
    "remove legacy inverter fail-safe read",
)

for old, label in [
    ("    if (s_ui.sharing) plant->load_sharing_mode = (uint8_t)lv_dropdown_get_selected(s_ui.sharing);\n", "remove load-sharing read"),
    ("    if (s_ui.base_tolerance_kw && !parse_float(s_ui.base_tolerance_kw, &plant->base_load_tolerance_kw)) return false;\n", "remove base tolerance kw read"),
    ("    if (s_ui.base_tolerance_percent && !parse_float(s_ui.base_tolerance_percent, &plant->base_load_tolerance_percent)) return false;\n", "remove base tolerance percent read"),
    ("    if (s_ui.urgent_fraction && !parse_float(s_ui.urgent_fraction, &plant->urgent_loading_fraction)) return false;\n", "remove urgent fraction read"),
    ("    if (s_ui.urgent_multiplier && !parse_float(s_ui.urgent_multiplier, &plant->urgent_ramp_multiplier)) return false;\n", "remove urgent multiplier read"),
    ("    if (s_ui.generator_role) g->role = (uint8_t)lv_dropdown_get_selected(s_ui.generator_role);\n", "remove generator role read"),
    ("    if (s_ui.generator_base_load && !parse_float(s_ui.generator_base_load, &g->base_load_kw)) return false;\n", "remove generator base-load read"),
]:
    text = replace_once(text, old, "", label)

text = replace_once(
    text,
    """        s_ui.role = dropdown_field(form, \"Role\", \"Unassigned\\nGrid\\nGenerator\\nLoad\\nPV\", m->role <= 4U ? m->role : 0U);\n        s_ui.model = dropdown_field(form, \"Meter model\", \"Undeclared\\nAutomatrix EM500/Lovato\\nGeneric Modbus (out of phase)\", m->model <= 2U ? m->model : 0U);\n        s_ui.phase_basis = dropdown_field(form, \"Grid phase basis\", \"Lowest phase\\nTotal\", m->phase_basis <= 1U ? m->phase_basis : 0U);\n""",
    """        s_ui.role = dropdown_field(form, \"Role\", \"Unassigned\\nGrid\\nGenerator\\nLoad\\nPV\", m->role <= 4U ? m->role : 0U);\n""",
    "remove legacy meter controls",
)

text = replace_once(
    text,
    "        s_ui.failsafe_ms = integer_field(form, \"Inverter comms fail-safe (ms, 0=unstated)\", v->comms_failsafe_ms);\n",
    "",
    "remove legacy inverter fail-safe control",
)

for old, label in [
    ("        s_ui.sharing = dropdown_field(form, \"Generator load sharing\", \"Unset\\nIsochronous\\nBase load\\nDroop (refused)\", p->load_sharing_mode <= 3U ? p->load_sharing_mode : 0U);\n", "remove load-sharing control"),
    ("        s_ui.base_tolerance_kw = number_field(form, \"Base-load tolerance (kW)\", p->base_load_tolerance_kw, 2U);\n", "remove base tolerance kw control"),
    ("        s_ui.base_tolerance_percent = number_field(form, \"Base-load tolerance (% rating)\", p->base_load_tolerance_percent, 2U);\n", "remove base tolerance percent control"),
    ("        s_ui.generator_role = dropdown_field(form, \"Base-load role\", \"Unset\\nSwing\\nBase load\", g->role <= 2U ? g->role : 0U);\n", "remove generator role control"),
    ("        s_ui.generator_base_load = number_field(form, \"Base-load setpoint (kW)\", g->base_load_kw, 2U);\n", "remove generator base-load control"),
    ("        s_ui.urgent_fraction = number_field(form, \"Urgent loading fraction (0..1)\", p->urgent_loading_fraction, 3U);\n", "remove urgent fraction control"),
    ("        s_ui.urgent_multiplier = number_field(form, \"Urgent ramp multiplier\", p->urgent_ramp_multiplier, 2U);\n", "remove urgent multiplier control"),
]:
    text = replace_once(text, old, "", label)

old_review_start = text.index("static void render_review(void)\n")
old_review_end = text.index("static void render_step(void)\n", old_review_start)
new_review = """static void render_review(void)
{
    lv_obj_t *form = form_container();
    heading(form, "Review / arm for restart",
            "This LCD does not infer production qualification. ARM only persists automatic control for the next restart; current Core starts fail-safe at zero PV command and grants command authority only when its runtime evidence gates pass.");
    if (!s_ui.gate.valid) {
        status_line(form, "Runtime command authority", "Unavailable");
        status_line(form, "Production qualification", "Not asserted by LCD");
    } else {
        status_line(form, "Runtime command authority",
                    s_ui.gate.command_authority ? "ACTIVE" : "INHIBITED");
        status_line(form, "Production qualification",
                    s_ui.gate.production_qualified ? "Qualified" : "Not asserted by LCD");
        status_line(form, "Runtime scope", s_ui.gate.scope[0] ? s_ui.gate.scope : "--");
        char count[64];
        snprintf(count, sizeof(count), "%u/%u current authority evidence met",
                 (unsigned)s_ui.gate.satisfied_count,
                 (unsigned)s_ui.gate.prerequisite_count);
        status_line(form, "Authority evidence", count);
        if (!s_ui.gate.command_authority) {
            status_line(form, "Current runtime blocker",
                        s_ui.gate.first_unmet_title[0] ? s_ui.gate.first_unmet_title : "--");
            lv_obj_t *detail = lv_label_create(form);
            lv_label_set_text(detail, s_ui.gate.first_unmet_detail[0]
                                      ? s_ui.gate.first_unmet_detail
                                      : s_ui.gate.inhibit_reason);
            lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(detail, LV_PCT(100));
            lv_obj_set_style_text_color(detail, lv_color_hex(0xF2B84B), LV_PART_MAIN);
        }
    }
    if (s_ui.config.restart_required) button(form, "Restart controller", restart_clicked, NULL);
    button(form, "DISARM automatic control", control_clicked, (void *)(uintptr_t)0U);
    button(form, "ARM automatic control for next restart", control_clicked, (void *)(uintptr_t)1U);
    button(form, "Refresh runtime/config", refresh_clicked, NULL);
}

"""
text = text[:old_review_start] + new_review + text[old_review_end:]
path.write_text(text)

# 2) Legacy-shaped runtime DTO must never claim commissioning/qualification.
path = Path("boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480/main/local_backend_provider.c")
text = path.read_text()
text = replace_once(
    text,
    "    out->commissioned = control.command_authority;\n",
    "    out->commissioned = false; /* Current Core has no commissioning_gate; never infer qualification. */\n",
    "stop inferring commissioning",
)
path.write_text(text)

# 3) Readiness page describes current runtime authority truthfully instead of
# presenting the retired commissioning-gate model.
path = Path("boards/waveshare_esp32_s3_touch_lcd_5/screen/pages/readiness_screen.c")
text = path.read_text()
text = replace_once(text, 'screen_ui_title(s_ui.root, "Site Commissioning / Readiness");',
                    'screen_ui_title(s_ui.root, "Runtime Readiness");',
                    "readiness title")
text = replace_once(text, 'screen_ui_row(s_ui.root, "Commissioning gate", &s_ui.commissioning);',
                    'screen_ui_row(s_ui.root, "Production qualification", &s_ui.commissioning);',
                    "readiness qualification label")
text = replace_once(text, 'screen_ui_row(s_ui.root, "Commissioning scope", &s_ui.commissioning_scope);',
                    'screen_ui_row(s_ui.root, "Runtime scope", &s_ui.commissioning_scope);',
                    "readiness scope label")
text = replace_once(text, 'screen_ui_row(s_ui.root, "Prerequisites", &s_ui.commissioning_progress);',
                    'screen_ui_row(s_ui.root, "Authority evidence", &s_ui.commissioning_progress);',
                    "readiness evidence label")
text = replace_once(text, 'screen_ui_row(s_ui.root, "Automatic permitted", &s_ui.automatic_permitted);',
                    'screen_ui_row(s_ui.root, "Command authority", &s_ui.automatic_permitted);',
                    "readiness command authority label")
text = replace_once(text, 'screen_ui_muted_label(s_ui.root, "Next commissioning blocker");',
                    'screen_ui_muted_label(s_ui.root, "Current runtime blocker");',
                    "readiness blocker label")
text = replace_once(
    text,
    '"Configure site: Engineering web > Commissioning. This HMI mirrors the Core gate; it does not bypass Engineering authentication.");',
    '"Configure site through authenticated Engineering. This HMI mirrors current Core runtime authority and does not infer production qualification.");',
    "readiness hint",
)

apply_start = text.index("void readiness_screen_apply_commissioning(const screen_commissioning_snapshot_t *snapshot)\n")
apply_end = text.index("void readiness_screen_apply(const screen_telemetry_snapshot_t *snapshot,\n", apply_start)
new_apply = """void readiness_screen_apply_commissioning(const screen_commissioning_snapshot_t *snapshot)
{
    if (!s_ui.root || !snapshot || !snapshot->valid) return;

    screen_ui_set_state_text(s_ui.commissioning,
                             snapshot->production_qualified ? "Qualified" : "Not asserted by LCD",
                             snapshot->production_qualified);
    screen_ui_set_state_text(s_ui.commissioning_scope,
                             screen_ui_safe_text(snapshot->scope, "none"),
                             snapshot->command_authority);

    char progress[64];
    snprintf(progress, sizeof(progress), "%lu/%lu current evidence met / %lu unmet",
             (unsigned long)snapshot->satisfied_count,
             (unsigned long)snapshot->prerequisite_count,
             (unsigned long)snapshot->unmet_count);
    screen_ui_set_state_text(s_ui.commissioning_progress, progress, snapshot->command_authority);
    screen_ui_set_state_text(s_ui.automatic_permitted,
                             snapshot->command_authority ? "Active" : "Inhibited",
                             snapshot->command_authority);

    if (snapshot->command_authority) {
        set_blocker("Current Core command authority is active. Production qualification remains a separate physical-governance decision.",
                    true);
    } else if (snapshot->first_unmet_title[0] != '\\0') {
        char blocker[256];
        snprintf(blocker, sizeof(blocker), "%s%s%s",
                 snapshot->first_unmet_title,
                 snapshot->first_unmet_detail[0] != '\\0' ? ": " : "",
                 snapshot->first_unmet_detail);
        set_blocker(blocker, false);
    } else {
        set_blocker(screen_ui_safe_text(snapshot->summary, snapshot->inhibit_reason), false);
    }
}

"""
text = text[:apply_start] + new_apply + text[apply_end:]
text = replace_once(text, 'set_blocker("Commissioning gate unavailable", false);',
                    'set_blocker("Runtime authority unavailable", false);',
                    "readiness unavailable text")
path.write_text(text)

# 4) Correct the stale API comment without renaming the compatibility DTO.
path = Path("boards/waveshare_esp32_s3_touch_lcd_5/screen/api/screen_api.h")
text = path.read_text()
text = replace_once(
    text,
    """/* Read-only projection of the existing GET /api/commissioning/gate authority.\n * The product build fills this from the same commissioning_gate/control_engine\n * state used by that endpoint; the screen never re-evaluates prerequisites. */\n""",
    """/* Compatibility-shaped read-only projection of current Core runtime command\n * authority. The historical commissioning_gate endpoint no longer exists. The\n * product build never infers production qualification from this runtime view. */\n""",
    "correct runtime authority DTO comment",
)
path.write_text(text)

# Successful generated commit leaves no temporary patch machinery.
Path(".github/workflows/waveshare-commissioning-semantics-patch.yml").unlink()
Path(".github/scripts/waveshare_commissioning_semantics_patch.py").unlink()
