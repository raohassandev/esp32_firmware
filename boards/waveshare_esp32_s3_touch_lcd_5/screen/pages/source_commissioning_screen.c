#include "source_commissioning_screen.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOURCE_FORM_CONTROL_WIDTH 330
#define SOURCE_KEYBOARD_HEIGHT 190

typedef struct {
    lv_obj_t *root;
    lv_obj_t *body;
    lv_obj_t *message;
    lv_obj_t *keyboard;
    lv_obj_t *credential;
    lv_obj_t *enabled;
    lv_obj_t *ga_meter;
    lv_obj_t *ga_function;
    lv_obj_t *ga_address;
    lv_obj_t *ga_mask;
    lv_obj_t *ga_active;
    lv_obj_t *gb_meter;
    lv_obj_t *gb_function;
    lv_obj_t *gb_address;
    lv_obj_t *gb_mask;
    lv_obj_t *gb_active;
    lv_obj_t *poll_ms;
    lv_obj_t *stale_ms;
    lv_obj_t *loss_ms;
    lv_obj_t *recovery_ms;
    source_commission_backend_t backend;
    source_commission_config_t config;
    bool backend_set;
    uint8_t page; /* 0 grid available, 1 breaker closed, 2 timing/enable */
} source_ui_t;

static source_ui_t s_ui;

static void render(void);

static void render_async(void *data)
{
    (void)data;
    render();
}

static void queue_render(void)
{
    (void)lv_async_call(render_async, NULL);
}

static void make_fixed(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void set_message(const char *text, bool good)
{
    if (!s_ui.message) return;
    lv_label_set_text(s_ui.message, text ? text : "");
    lv_obj_set_style_text_color(s_ui.message,
                                lv_color_hex(good ? 0x62D28F : 0xF07178),
                                LV_PART_MAIN);
}

static bool load_config(void)
{
    if (!s_ui.backend_set || !s_ui.backend.read_config) return false;
    source_commission_config_t next = {0};
    if (!s_ui.backend.read_config(s_ui.backend.context, &next) || !next.valid) return false;
    s_ui.config = next;
    return true;
}

static void keyboard_hide(void)
{
    if (!s_ui.keyboard) return;
    lv_keyboard_set_textarea(s_ui.keyboard, NULL);
    lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void keyboard_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) keyboard_hide();
}

static void textarea_focus(lv_event_t *event)
{
    if (!s_ui.keyboard) return;
    lv_obj_t *textarea = lv_event_get_target_obj(event);
    lv_keyboard_set_textarea(s_ui.keyboard, textarea);
    lv_obj_remove_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ui.keyboard);
}

static lv_obj_t *row(lv_obj_t *parent, const char *label)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_set_width(item, LV_PCT(100));
    lv_obj_set_height(item, LV_SIZE_CONTENT);
    lv_obj_set_layout(item, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *caption = lv_label_create(item);
    lv_label_set_text(caption, label);
    lv_obj_set_width(caption, 270);
    lv_obj_set_style_text_color(caption, lv_color_hex(0xC7D0DA), LV_PART_MAIN);
    return item;
}

static lv_obj_t *field(lv_obj_t *parent, const char *label, const char *value, bool password)
{
    lv_obj_t *item = row(parent, label);
    lv_obj_t *input = lv_textarea_create(item);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_password_mode(input, password);
    lv_textarea_set_text(input, value ? value : "");
    lv_obj_set_width(input, SOURCE_FORM_CONTROL_WIDTH);
    lv_obj_add_event_cb(input, textarea_focus, LV_EVENT_FOCUSED, NULL);
    return input;
}

static lv_obj_t *integer_field(lv_obj_t *parent, const char *label, unsigned long value)
{
    char text[32];
    snprintf(text, sizeof(text), "%lu", value);
    return field(parent, label, text, false);
}

static lv_obj_t *hex_field(lv_obj_t *parent, const char *label, uint16_t value)
{
    char text[16];
    snprintf(text, sizeof(text), "0x%04X", (unsigned)value);
    return field(parent, label, text, false);
}

static lv_obj_t *checkbox_field(lv_obj_t *parent, const char *label, bool value)
{
    lv_obj_t *item = row(parent, label);
    lv_obj_t *box = lv_checkbox_create(item);
    lv_checkbox_set_text(box, "");
    if (value) lv_obj_add_state(box, LV_STATE_CHECKED);
    lv_obj_set_width(box, SOURCE_FORM_CONTROL_WIDTH);
    return box;
}

static lv_obj_t *dropdown_field(lv_obj_t *parent, const char *label,
                                const char *options, uint32_t selected)
{
    lv_obj_t *item = row(parent, label);
    lv_obj_t *drop = lv_dropdown_create(item);
    lv_dropdown_set_options(drop, options);
    lv_dropdown_set_selected(drop, selected);
    lv_obj_set_width(drop, SOURCE_FORM_CONTROL_WIDTH);
    return drop;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t callback)
{
    lv_obj_t *obj = lv_button_create(parent);
    make_fixed(obj);
    lv_obj_set_height(obj, 38);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return obj;
}

static void heading(lv_obj_t *parent, const char *title, const char *detail)
{
    lv_obj_t *h = lv_label_create(parent);
    lv_label_set_text(h, title);
    lv_obj_set_style_text_color(h, lv_color_hex(0xF2F6FA), LV_PART_MAIN);
    lv_obj_t *d = lv_label_create(parent);
    lv_label_set_text(d, detail);
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(d, LV_PCT(100));
    lv_obj_set_style_text_color(d, lv_color_hex(0x9EADBF), LV_PART_MAIN);
}

static bool checked(lv_obj_t *obj)
{
    return obj && lv_obj_has_state(obj, LV_STATE_CHECKED);
}

static bool parse_unsigned(lv_obj_t *obj, int base,
                           unsigned long minimum, unsigned long maximum,
                           unsigned long *value)
{
    if (!obj || !value) return false;
    const char *text = lv_textarea_get_text(obj);
    char *end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(text, &end, base);
    if (errno || !end || end == text || *end != '\0' || parsed < minimum || parsed > maximum) {
        return false;
    }
    *value = parsed;
    return true;
}

static lv_obj_t *form_container(void)
{
    lv_obj_t *form = lv_obj_create(s_ui.body);
    lv_obj_set_size(form, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(form, lv_color_hex(0x101720), LV_PART_MAIN);
    lv_obj_set_style_border_width(form, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(form, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(form, 10, LV_PART_MAIN);
    lv_obj_set_layout(form, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(form, 7, LV_PART_MAIN);
    lv_obj_set_scroll_dir(form, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(form, LV_SCROLLBAR_MODE_AUTO);
    return form;
}

static void unlock_clicked(lv_event_t *event)
{
    (void)event;
    if (!s_ui.backend.unlock || !s_ui.credential) return;
    uint32_t retry = 0U;
    bool setup = false;
    const source_commission_auth_result_t result =
        s_ui.backend.unlock(s_ui.backend.context, lv_textarea_get_text(s_ui.credential),
                            &retry, &setup);
    lv_textarea_set_text(s_ui.credential, "");
    keyboard_hide();
    if (result == SOURCE_COMMISSION_AUTH_OK) {
        if (!load_config()) {
            set_message("Engineering unlocked, but source configuration could not be read.", false);
            return;
        }
        s_ui.config.unlocked = true;
        s_ui.config.setup_required = setup;
        set_message("Engineering unlocked for source commissioning.", true);
        queue_render();
    } else if (result == SOURCE_COMMISSION_AUTH_LOCKED) {
        char text[96];
        snprintf(text, sizeof(text), "Engineering login locked. Retry in %lu s.",
                 (unsigned long)((retry + 999U) / 1000U));
        set_message(text, false);
    } else {
        set_message("Engineering credential rejected.", false);
    }
}

static void lock_clicked(lv_event_t *event)
{
    (void)event;
    keyboard_hide();
    if (s_ui.backend.lock) s_ui.backend.lock(s_ui.backend.context);
    memset(&s_ui.config, 0, sizeof(s_ui.config));
    s_ui.page = 0U;
    queue_render();
}

static void page_prev_clicked(lv_event_t *event)
{
    (void)event;
    keyboard_hide();
    if (s_ui.page > 0U) s_ui.page--;
    queue_render();
}

static void page_next_clicked(lv_event_t *event)
{
    (void)event;
    keyboard_hide();
    if (s_ui.page < 2U) s_ui.page++;
    queue_render();
}

static bool read_signal(source_commission_signal_t *signal,
                        lv_obj_t *meter, lv_obj_t *function_code,
                        lv_obj_t *address, lv_obj_t *mask, lv_obj_t *active)
{
    if (!signal) return false;
    if (meter) signal->meter_index = (uint8_t)lv_dropdown_get_selected(meter);
    if (function_code) {
        signal->function_code = lv_dropdown_get_selected(function_code) == 0U ? 3U : 4U;
    }
    unsigned long value = 0U;
    if (address) {
        if (!parse_unsigned(address, 0, 0U, 65535U, &value)) return false;
        signal->address = (uint16_t)value;
    }
    if (mask) {
        if (!parse_unsigned(mask, 0, 0U, 65535U, &value)) return false;
        signal->mask = (uint16_t)value;
    }
    if (active) {
        if (!parse_unsigned(active, 0, 0U, 65535U, &value)) return false;
        signal->active_value = (uint16_t)value;
    }
    return true;
}

static bool read_form(source_commission_config_t *config)
{
    if (!config) return false;
    if (s_ui.enabled) config->evidence_enabled = checked(s_ui.enabled);
    if (!read_signal(&config->grid_available, s_ui.ga_meter, s_ui.ga_function,
                     s_ui.ga_address, s_ui.ga_mask, s_ui.ga_active) ||
        !read_signal(&config->grid_breaker_closed, s_ui.gb_meter, s_ui.gb_function,
                     s_ui.gb_address, s_ui.gb_mask, s_ui.gb_active)) return false;

    unsigned long value = 0U;
    if (s_ui.poll_ms) {
        if (!parse_unsigned(s_ui.poll_ms, 10, 100U, 60000U, &value)) return false;
        config->evidence_poll_interval_ms = (uint32_t)value;
    }
    if (s_ui.stale_ms) {
        if (!parse_unsigned(s_ui.stale_ms, 10, config->evidence_poll_interval_ms, 600000U, &value)) return false;
        config->evidence_stale_timeout_ms = (uint32_t)value;
    }
    if (s_ui.loss_ms) {
        if (!parse_unsigned(s_ui.loss_ms, 10, 0U, 60000U, &value)) return false;
        config->grid_loss_trip_ms = (uint32_t)value;
    }
    if (s_ui.recovery_ms) {
        if (!parse_unsigned(s_ui.recovery_ms, 10, 0U, 600000U, &value)) return false;
        config->grid_recovery_stable_ms = (uint32_t)value;
    }
    if (config->evidence_enabled &&
        (config->grid_available.mask == 0U || config->grid_breaker_closed.mask == 0U)) {
        return false;
    }
    return true;
}

static void save_clicked(lv_event_t *event)
{
    (void)event;
    if (!s_ui.backend.save_config) return;
    source_commission_config_t next = s_ui.config;
    if (!read_form(&next)) {
        set_message("Source evidence fields are invalid. Enabled signals require a non-zero mask.", false);
        return;
    }
    source_commission_action_result_t result = {0};
    (void)s_ui.backend.save_config(s_ui.backend.context, &next, &result);
    set_message(result.message[0] ? result.message : (result.ok ? "Saved" : "Save failed"), result.ok);
    if (result.ok) {
        (void)load_config();
        queue_render();
    }
}

static void refresh_clicked(lv_event_t *event)
{
    (void)event;
    if (!load_config()) {
        set_message("Source configuration could not be refreshed.", false);
        return;
    }
    set_message("Source configuration refreshed from Core.", true);
    queue_render();
}

static void restart_clicked(lv_event_t *event)
{
    (void)event;
    if (!s_ui.backend.restart_controller) return;
    source_commission_action_result_t result = {0};
    (void)s_ui.backend.restart_controller(s_ui.backend.context, &result);
    set_message(result.message[0] ? result.message : "Restart request failed.", result.ok);
}

static void render_locked(void)
{
    lv_obj_t *form = form_container();
    heading(form, "Source evidence · Engineering locked",
            "Use the same Engineering password as the protected web workspace. This page configures the two source-evidence registers used by Core source detection; it never infers a source locally.");
    s_ui.credential = field(form, "Engineering credential", "", true);
    button(form, "Unlock source commissioning", unlock_clicked);
}

static void signal_fields(lv_obj_t *form, const char *title,
                          const source_commission_signal_t *signal,
                          lv_obj_t **meter, lv_obj_t **function_code,
                          lv_obj_t **address, lv_obj_t **mask, lv_obj_t **active)
{
    heading(form, title, "Read-only Modbus evidence. Meter numbering below is 1-based for the operator; Core stores the selected slot and performs acquisition in its normal background path.");
    *meter = dropdown_field(form, "Meter slot", "Meter 1\nMeter 2\nMeter 3\nMeter 4",
                            signal->meter_index < SOURCE_COMMISSIONING_MAX_METERS ? signal->meter_index : 0U);
    *function_code = dropdown_field(form, "Read function", "FC03\nFC04",
                                    signal->function_code == 4U ? 1U : 0U);
    *address = hex_field(form, "PDU address (dec or 0xHEX)", signal->address);
    *mask = hex_field(form, "Mask (dec or 0xHEX)", signal->mask);
    *active = hex_field(form, "Active value (dec or 0xHEX)", signal->active_value);
}

static void render_unlocked(void)
{
    lv_obj_t *form = form_container();
    static const char *const page_names[] = { "Grid available", "Breaker closed", "Timing + enable" };
    heading(form, "Grid source evidence",
            "Source commissioning is split into lightweight sections for exact-board DRAM headroom. Configure and save both register sections first; enable the pair only on the final section.");

    lv_obj_t *nav = lv_obj_create(form);
    lv_obj_remove_style_all(nav);
    lv_obj_set_width(nav, LV_PCT(100));
    lv_obj_set_height(nav, 40);
    lv_obj_set_layout(nav, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    button(nav, "< Section", page_prev_clicked);
    char page_text[64];
    snprintf(page_text, sizeof(page_text), "%s %u/3", page_names[s_ui.page],
             (unsigned)(s_ui.page + 1U));
    lv_obj_t *page_label = lv_label_create(nav);
    lv_label_set_text(page_label, page_text);
    lv_obj_set_width(page_label, 260);
    lv_obj_set_style_text_align(page_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    button(nav, "Section >", page_next_clicked);

    if (s_ui.page == 0U) {
        signal_fields(form, "Grid available evidence", &s_ui.config.grid_available,
                      &s_ui.ga_meter, &s_ui.ga_function, &s_ui.ga_address,
                      &s_ui.ga_mask, &s_ui.ga_active);
        button(form, "Save grid-available section", save_clicked);
    } else if (s_ui.page == 1U) {
        signal_fields(form, "Grid breaker closed evidence", &s_ui.config.grid_breaker_closed,
                      &s_ui.gb_meter, &s_ui.gb_function, &s_ui.gb_address,
                      &s_ui.gb_mask, &s_ui.gb_active);
        button(form, "Save breaker section", save_clicked);
    } else {
        heading(form, "Evidence timing",
                "Enable only after both real evidence registers have been configured. Core requires the two signals as one complete pair; unknown or stale evidence remains fail-closed.");
        s_ui.enabled = checkbox_field(form, "Enable source evidence", s_ui.config.evidence_enabled);
        s_ui.poll_ms = integer_field(form, "Evidence poll interval (ms)", s_ui.config.evidence_poll_interval_ms);
        s_ui.stale_ms = integer_field(form, "Evidence stale timeout (ms)", s_ui.config.evidence_stale_timeout_ms);
        s_ui.loss_ms = integer_field(form, "Grid loss trip (ms)", s_ui.config.grid_loss_trip_ms);
        s_ui.recovery_ms = integer_field(form, "Grid recovery stable (ms)", s_ui.config.grid_recovery_stable_ms);
        button(form, "Save timing / enable pair", save_clicked);
        button(form, "Refresh from Core", refresh_clicked);
        if (s_ui.config.restart_required) button(form, "Restart controller", restart_clicked);
    }
}

static void render(void)
{
    if (!s_ui.root || !s_ui.body) return;
    keyboard_hide();
    lv_obj_clean(s_ui.body);
    s_ui.credential = NULL;
    if (!s_ui.backend_set || !s_ui.config.unlocked) render_locked();
    else render_unlocked();
}

lv_obj_t *source_commissioning_screen_create(lv_obj_t *parent)
{
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.root = lv_obj_create(parent ? parent : lv_screen_active());
    make_fixed(s_ui.root);
    lv_obj_set_size(s_ui.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ui.root, lv_color_hex(0x0B1017), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.root, 8, LV_PART_MAIN);
    lv_obj_set_layout(s_ui.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ui.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ui.root, 5, LV_PART_MAIN);

    lv_obj_t *top = lv_obj_create(s_ui.root);
    lv_obj_remove_style_all(top);
    make_fixed(top);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_height(top, 38);
    lv_obj_set_layout(top, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = lv_label_create(top);
    lv_label_set_text(title, "Source Evidence Commissioning");
    lv_obj_set_width(title, 500);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF2F6FA), LV_PART_MAIN);
    button(top, "Lock", lock_clicked);

    s_ui.body = lv_obj_create(s_ui.root);
    lv_obj_remove_style_all(s_ui.body);
    make_fixed(s_ui.body);
    lv_obj_set_width(s_ui.body, LV_PCT(100));
    lv_obj_set_flex_grow(s_ui.body, 1);

    s_ui.message = lv_label_create(s_ui.root);
    lv_label_set_text(s_ui.message, "");
    lv_label_set_long_mode(s_ui.message, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_ui.message, LV_PCT(100));
    lv_obj_set_height(s_ui.message, 22);

    s_ui.keyboard = lv_keyboard_create(s_ui.root);
    lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(s_ui.keyboard, LV_PCT(100), SOURCE_KEYBOARD_HEIGHT);
    lv_obj_align(s_ui.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_ui.keyboard, keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_ui.keyboard, keyboard_event, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);

    render();
    return s_ui.root;
}

void source_commissioning_screen_set_backend(const source_commission_backend_t *backend)
{
    if (!backend) {
        memset(&s_ui.backend, 0, sizeof(s_ui.backend));
        memset(&s_ui.config, 0, sizeof(s_ui.config));
        s_ui.backend_set = false;
    } else {
        s_ui.backend = *backend;
        s_ui.backend_set = true;
    }
    render();
}

void source_commissioning_screen_show_unavailable(void)
{
    if (!s_ui.config.unlocked) return;
    set_message("Source commissioning backend unavailable.", false);
}
