#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile, shutil

REPO = Path.cwd()
BOARD = "board/waveshare-esp32-s3-touch-lcd-5"
TARGET = Path("boards/waveshare_esp32_s3_touch_lcd_5/screen/pages/source_commissioning_screen.c")
TEST = Path("tests/waveshare_screen_source_contract.py")


def run(*args, cwd=REPO):
    subprocess.run(args, cwd=cwd, check=True)


def out(*args, cwd=REPO):
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


run("git", "fetch", "origin", BOARD)
base = out("git", "rev-parse", f"origin/{BOARD}")
tmp = Path(tempfile.mkdtemp(prefix="source-paging-"))

try:
    run("git", "worktree", "add", "--detach", str(tmp), base)
    path = tmp / TARGET
    src = path.read_text(encoding="utf-8")

    src = replace_once(
        src,
        "    source_commission_config_t config;\n    bool backend_set;\n} source_ui_t;",
        "    source_commission_config_t config;\n    bool backend_set;\n    uint8_t page; /* 0 grid available, 1 breaker closed, 2 timing/enable */\n} source_ui_t;",
        "source page field",
    )

    src = replace_once(
        src,
        '''static void lock_clicked(lv_event_t *event)\n{\n    (void)event;\n    keyboard_hide();\n    if (s_ui.backend.lock) s_ui.backend.lock(s_ui.backend.context);\n    memset(&s_ui.config, 0, sizeof(s_ui.config));\n    queue_render();\n}\n''',
        '''static void lock_clicked(lv_event_t *event)\n{\n    (void)event;\n    keyboard_hide();\n    if (s_ui.backend.lock) s_ui.backend.lock(s_ui.backend.context);\n    memset(&s_ui.config, 0, sizeof(s_ui.config));\n    s_ui.page = 0U;\n    queue_render();\n}\n\nstatic void page_prev_clicked(lv_event_t *event)\n{\n    (void)event;\n    keyboard_hide();\n    if (s_ui.page > 0U) s_ui.page--;\n    queue_render();\n}\n\nstatic void page_next_clicked(lv_event_t *event)\n{\n    (void)event;\n    keyboard_hide();\n    if (s_ui.page < 2U) s_ui.page++;\n    queue_render();\n}\n''',
        "source page callbacks",
    )

    old_read_signal = '''static bool read_signal(source_commission_signal_t *signal,\n                        lv_obj_t *meter, lv_obj_t *function_code,\n                        lv_obj_t *address, lv_obj_t *mask, lv_obj_t *active)\n{\n    if (!signal) return false;\n    signal->meter_index = (uint8_t)lv_dropdown_get_selected(meter);\n    signal->function_code = lv_dropdown_get_selected(function_code) == 0U ? 3U : 4U;\n    unsigned long value = 0U;\n    if (!parse_unsigned(address, 0, 0U, 65535U, &value)) return false;\n    signal->address = (uint16_t)value;\n    if (!parse_unsigned(mask, 0, 0U, 65535U, &value)) return false;\n    signal->mask = (uint16_t)value;\n    if (!parse_unsigned(active, 0, 0U, 65535U, &value)) return false;\n    signal->active_value = (uint16_t)value;\n    return true;\n}\n\nstatic bool read_form(source_commission_config_t *config)\n{\n    if (!config) return false;\n    config->evidence_enabled = checked(s_ui.enabled);\n    if (!read_signal(&config->grid_available, s_ui.ga_meter, s_ui.ga_function,\n                     s_ui.ga_address, s_ui.ga_mask, s_ui.ga_active) ||\n        !read_signal(&config->grid_breaker_closed, s_ui.gb_meter, s_ui.gb_function,\n                     s_ui.gb_address, s_ui.gb_mask, s_ui.gb_active)) return false;\n\n    unsigned long value = 0U;\n    if (!parse_unsigned(s_ui.poll_ms, 10, 100U, 60000U, &value)) return false;\n    config->evidence_poll_interval_ms = (uint32_t)value;\n    if (!parse_unsigned(s_ui.stale_ms, 10, config->evidence_poll_interval_ms, 600000U, &value)) return false;\n    config->evidence_stale_timeout_ms = (uint32_t)value;\n    if (!parse_unsigned(s_ui.loss_ms, 10, 0U, 60000U, &value)) return false;\n    config->grid_loss_trip_ms = (uint32_t)value;\n    if (!parse_unsigned(s_ui.recovery_ms, 10, 0U, 600000U, &value)) return false;\n    config->grid_recovery_stable_ms = (uint32_t)value;\n    if (config->evidence_enabled &&\n        (config->grid_available.mask == 0U || config->grid_breaker_closed.mask == 0U)) {\n        return false;\n    }\n    return true;\n}\n'''

    new_read_signal = '''static bool read_signal(source_commission_signal_t *signal,\n                        lv_obj_t *meter, lv_obj_t *function_code,\n                        lv_obj_t *address, lv_obj_t *mask, lv_obj_t *active)\n{\n    if (!signal) return false;\n    if (meter) signal->meter_index = (uint8_t)lv_dropdown_get_selected(meter);\n    if (function_code) {\n        signal->function_code = lv_dropdown_get_selected(function_code) == 0U ? 3U : 4U;\n    }\n    unsigned long value = 0U;\n    if (address) {\n        if (!parse_unsigned(address, 0, 0U, 65535U, &value)) return false;\n        signal->address = (uint16_t)value;\n    }\n    if (mask) {\n        if (!parse_unsigned(mask, 0, 0U, 65535U, &value)) return false;\n        signal->mask = (uint16_t)value;\n    }\n    if (active) {\n        if (!parse_unsigned(active, 0, 0U, 65535U, &value)) return false;\n        signal->active_value = (uint16_t)value;\n    }\n    return true;\n}\n\nstatic bool read_form(source_commission_config_t *config)\n{\n    if (!config) return false;\n    if (s_ui.enabled) config->evidence_enabled = checked(s_ui.enabled);\n    if (!read_signal(&config->grid_available, s_ui.ga_meter, s_ui.ga_function,\n                     s_ui.ga_address, s_ui.ga_mask, s_ui.ga_active) ||\n        !read_signal(&config->grid_breaker_closed, s_ui.gb_meter, s_ui.gb_function,\n                     s_ui.gb_address, s_ui.gb_mask, s_ui.gb_active)) return false;\n\n    unsigned long value = 0U;\n    if (s_ui.poll_ms) {\n        if (!parse_unsigned(s_ui.poll_ms, 10, 100U, 60000U, &value)) return false;\n        config->evidence_poll_interval_ms = (uint32_t)value;\n    }\n    if (s_ui.stale_ms) {\n        if (!parse_unsigned(s_ui.stale_ms, 10, config->evidence_poll_interval_ms, 600000U, &value)) return false;\n        config->evidence_stale_timeout_ms = (uint32_t)value;\n    }\n    if (s_ui.loss_ms) {\n        if (!parse_unsigned(s_ui.loss_ms, 10, 0U, 60000U, &value)) return false;\n        config->grid_loss_trip_ms = (uint32_t)value;\n    }\n    if (s_ui.recovery_ms) {\n        if (!parse_unsigned(s_ui.recovery_ms, 10, 0U, 600000U, &value)) return false;\n        config->grid_recovery_stable_ms = (uint32_t)value;\n    }\n    if (config->evidence_enabled &&\n        (config->grid_available.mask == 0U || config->grid_breaker_closed.mask == 0U)) {\n        return false;\n    }\n    return true;\n}\n'''
    src = replace_once(src, old_read_signal, new_read_signal, "partial source form reader")

    old_render = '''static void render_unlocked(void)\n{\n    lv_obj_t *form = form_container();\n    heading(form, "Grid source evidence",\n            "Both evidence signals are enabled or disabled together because the shared Core validator requires them as one complete source model. Saving always forces automatic control disabled and requires a restart before qualification.");\n    s_ui.enabled = checkbox_field(form, "Enable source evidence", s_ui.config.evidence_enabled);\n\n    signal_fields(form, "Grid available evidence", &s_ui.config.grid_available,\n                  &s_ui.ga_meter, &s_ui.ga_function, &s_ui.ga_address,\n                  &s_ui.ga_mask, &s_ui.ga_active);\n    signal_fields(form, "Grid breaker closed evidence", &s_ui.config.grid_breaker_closed,\n                  &s_ui.gb_meter, &s_ui.gb_function, &s_ui.gb_address,\n                  &s_ui.gb_mask, &s_ui.gb_active);\n\n    heading(form, "Evidence timing", "These bounds mirror the shared Solar-Grid validator. Unknown or stale evidence keeps source attribution fail-closed.");\n    s_ui.poll_ms = integer_field(form, "Evidence poll interval (ms)", s_ui.config.evidence_poll_interval_ms);\n    s_ui.stale_ms = integer_field(form, "Evidence stale timeout (ms)", s_ui.config.evidence_stale_timeout_ms);\n    s_ui.loss_ms = integer_field(form, "Grid loss trip (ms)", s_ui.config.grid_loss_trip_ms);\n    s_ui.recovery_ms = integer_field(form, "Grid recovery stable (ms)", s_ui.config.grid_recovery_stable_ms);\n\n    button(form, "Save source evidence", save_clicked);\n    button(form, "Refresh from Core", refresh_clicked);\n    if (s_ui.config.restart_required) button(form, "Restart controller", restart_clicked);\n}\n'''

    new_render = '''static void render_unlocked(void)\n{\n    lv_obj_t *form = form_container();\n    static const char *const page_names[] = { "Grid available", "Breaker closed", "Timing + enable" };\n    heading(form, "Grid source evidence",\n            "Source commissioning is split into lightweight sections for exact-board DRAM headroom. Configure and save both register sections first; enable the pair only on the final section.");\n\n    lv_obj_t *nav = lv_obj_create(form);\n    lv_obj_remove_style_all(nav);\n    lv_obj_set_width(nav, LV_PCT(100));\n    lv_obj_set_height(nav, 40);\n    lv_obj_set_layout(nav, LV_LAYOUT_FLEX);\n    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);\n    button(nav, "< Section", page_prev_clicked);\n    char page_text[64];\n    snprintf(page_text, sizeof(page_text), "%s %u/3", page_names[s_ui.page],\n             (unsigned)(s_ui.page + 1U));\n    lv_obj_t *page_label = lv_label_create(nav);\n    lv_label_set_text(page_label, page_text);\n    lv_obj_set_width(page_label, 260);\n    lv_obj_set_style_text_align(page_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);\n    button(nav, "Section >", page_next_clicked);\n\n    if (s_ui.page == 0U) {\n        signal_fields(form, "Grid available evidence", &s_ui.config.grid_available,\n                      &s_ui.ga_meter, &s_ui.ga_function, &s_ui.ga_address,\n                      &s_ui.ga_mask, &s_ui.ga_active);\n        button(form, "Save grid-available section", save_clicked);\n    } else if (s_ui.page == 1U) {\n        signal_fields(form, "Grid breaker closed evidence", &s_ui.config.grid_breaker_closed,\n                      &s_ui.gb_meter, &s_ui.gb_function, &s_ui.gb_address,\n                      &s_ui.gb_mask, &s_ui.gb_active);\n        button(form, "Save breaker section", save_clicked);\n    } else {\n        heading(form, "Evidence timing",\n                "Enable only after both real evidence registers have been configured. Core requires the two signals as one complete pair; unknown or stale evidence remains fail-closed.");\n        s_ui.enabled = checkbox_field(form, "Enable source evidence", s_ui.config.evidence_enabled);\n        s_ui.poll_ms = integer_field(form, "Evidence poll interval (ms)", s_ui.config.evidence_poll_interval_ms);\n        s_ui.stale_ms = integer_field(form, "Evidence stale timeout (ms)", s_ui.config.evidence_stale_timeout_ms);\n        s_ui.loss_ms = integer_field(form, "Grid loss trip (ms)", s_ui.config.grid_loss_trip_ms);\n        s_ui.recovery_ms = integer_field(form, "Grid recovery stable (ms)", s_ui.config.grid_recovery_stable_ms);\n        button(form, "Save timing / enable pair", save_clicked);\n        button(form, "Refresh from Core", refresh_clicked);\n        if (s_ui.config.restart_required) button(form, "Restart controller", restart_clicked);\n    }\n}\n'''
    src = replace_once(src, old_render, new_render, "paged source render")

    path.write_text(src, encoding="utf-8")

    test_path = tmp / TEST
    test = test_path.read_text(encoding="utf-8")
    anchor = '''    assert "Grid available evidence" in source_ui\n    assert "Grid breaker closed evidence" in source_ui\n    assert "Enable source evidence" in source_ui\n'''
    replacement = anchor + '''    assert "uint8_t page; /* 0 grid available, 1 breaker closed, 2 timing/enable */" in source_ui\n    assert "Save grid-available section" in source_ui\n    assert "Save breaker section" in source_ui\n    assert "Save timing / enable pair" in source_ui\n    assert "if (s_ui.enabled) config->evidence_enabled = checked(s_ui.enabled);" in source_ui\n'''
    test = replace_once(test, anchor, replacement, "source paging contract")
    test_path.write_text(test, encoding="utf-8")

    run("python", str(TEST), cwd=tmp)
    run("git", "add", str(TARGET), str(TEST), cwd=tmp)
    run("git", "commit", "-m", "screen: page source commissioning for DRAM headroom", cwd=tmp)
    sha = out("git", "rev-parse", "HEAD", cwd=tmp)
    run("git", "push", "origin", f"HEAD:{BOARD}", cwd=tmp)
    print(f"BOARD_SHA={sha}")
finally:
    try:
        run("git", "worktree", "remove", "--force", str(tmp))
    except Exception:
        pass
    shutil.rmtree(tmp, ignore_errors=True)
