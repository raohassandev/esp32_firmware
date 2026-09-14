#include "source_commissioning_screen.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "screen_widgets.h"

#if defined(CONFIG_WAVESHARE_BENCH_ENGINEERING_CREDENTIAL)
#define WAVESHARE_BENCH_CREDENTIAL CONFIG_WAVESHARE_BENCH_ENGINEERING_CREDENTIAL
#else
#define WAVESHARE_BENCH_CREDENTIAL ""
#endif

#define SOURCE_FORM_CONTROL_WIDTH 330
#define SOURCE_KEYBOARD_HEIGHT 190
#define SOURCE_SIGNAL_PAGE_COUNT 10U
#define SOURCE_TIMING_PAGE 10U
#define SOURCE_PAGE_COUNT 11U

typedef struct {
    lv_obj_t *root;
    lv_obj_t *body;
    lv_obj_t *message;
    lv_obj_t *keyboard;
    lv_obj_t *credential;

    lv_obj_t *signal_meter;
    lv_obj_t *signal_function;
    lv_obj_t *signal_address;
    lv_obj_t *signal_mask;
    lv_obj_t *signal_active;

    lv_obj_t *enable_grid;
    lv_obj_t *enable_generators[SOURCE_COMMISSIONING_MAX_GENERATORS];
    lv_obj_t *enable_transfer;
    lv_obj_t *enable_sync;
    lv_obj_t *poll_ms;
    lv_obj_t *stale_ms;
    lv_obj_t *loss_ms;
    lv_obj_t *recovery_ms;

    source_commission_backend_t backend;
    source_commission_config_t config;
    bool backend_set;
    uint8_t page;
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

static void clear_form_refs(void)
{
    s_ui.credential = NULL;
    s_ui.signal_meter = NULL;
    s_ui.signal_function = NULL;
    s_ui.signal_address = NULL;
    s_ui.signal_mask = NULL;
    s_ui.signal_active = NULL;
    s_ui.enable_grid = NULL;
    for (uint8_t i = 0U; i < SOURCE_COMMISSIONING_MAX_GENERATORS; ++i) {
        s_ui.enable_generators[i] = NULL;
    }
    s_ui.enable_transfer = NULL;
    s_ui.enable_sync = NULL;
    s_ui.poll_ms = NULL;
    s_ui.stale_ms = NULL;
    s_ui.loss_ms = NULL;
    s_ui.recovery_ms = NULL;
}

static void set_message(const char *text, bool good)
{
    if (!s_ui.message) return;
    lv_label_set_text(s_ui.message, text ? text : "");
    lv_obj_set_style_text_color(s_ui.message,
                                lv_color_hex(good ? SCREEN_COLOR_SUCCESS : SCREEN_COLOR_DANGER),
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
    lv_obj_set_style_text_color(caption, lv_color_hex(SCREEN_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
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
    lv_obj_set_height(input, 44);
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
    lv_obj_set_size(box, SOURCE_FORM_CONTROL_WIDTH, 44);
    return box;
}

static lv_obj_t *dropdown_field(lv_obj_t *parent, const char *label,
                                const char *options, uint32_t selected)
{
    lv_obj_t *item = row(parent, label);
    lv_obj_t *drop = lv_dropdown_create(item);
    lv_dropdown_set_options(drop, options);
    lv_dropdown_set_selected(drop, selected);
    lv_obj_set_size(drop, SOURCE_FORM_CONTROL_WIDTH, 44);
    return drop;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t callback)
{
    lv_obj_t *obj = lv_button_create(parent);
    make_fixed(obj);
    lv_obj_set_height(obj, 44);
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
    lv_obj_set_style_text_color(h, lv_color_hex(SCREEN_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_t *d = lv_label_create(parent);
    lv_label_set_text(d, detail);
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(d, LV_PCT(100));
    lv_obj_set_style_text_color(d, lv_color_hex(SCREEN_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
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
    lv_obj_set_style_bg_color(form, lv_color_hex(SCREEN_COLOR_SURFACE), LV_PART_MAIN);
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

static source_commission_signal_t *signal_for_page(source_commission_config_t *config,
                                                    uint8_t page)
{
    if (!config) return NULL;
    switch (page) {
        case 0U: return &config->grid_available;
        case 1U: return &config->grid_breaker_closed;
        case 2U: return &config->generator_running[0];
        case 3U: return &config->generator_breaker_closed[0];
        case 4U: return &config->generator_running[1];
        case 5U: return &config->generator_breaker_closed[1];
        case 6U: return &config->generator_running[2];
        case 7U: return &config->generator_breaker_closed[2];
        case 8U: return &config->transfer_active;
        case 9U: return &config->grid_generator_synchronized;
        default: return NULL;
    }
}

static const char *page_name(uint8_t page)
{
    static const char *const names[SOURCE_PAGE_COUNT] = {
        "Grid available",
        "Grid breaker",
        "Gen 1 running",
        "Gen 1 breaker",
        "Gen 2 running",
        "Gen 2 breaker",
        "Gen 3 running",
        "Gen 3 breaker",
        "Transfer active",
        "Grid + Gen sync",
        "Enable + timing",
    };
    return page < SOURCE_PAGE_COUNT ? names[page] : "Source evidence";
}

static const char *page_detail(uint8_t page)
{
    if (page <= 1U) return "Authoritative grid contact/register evidence only. Do not infer breaker state from kW sign.";
    if (page >= 2U && page <= 7U) return "Generator run and breaker channels are commissioned independently per generator and enabled only as a complete pair.";
    if (page == 8U) return "Optional plant transfer/ATS evidence. Configure only from the real commissioned contact/register and polarity.";
    if (page == 9U) return "Optional synchronism evidence for topologies that genuinely support simultaneous Grid + Generator operation.";
    return "Enable only channels backed by authoritative site/manual evidence. Unknown, stale or conflicting evidence remains fail-closed.";
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
    if (s_ui.page + 1U < SOURCE_PAGE_COUNT) s_ui.page++;
    queue_render();
}

static bool read_signal(source_commission_signal_t *signal)
{
    if (!signal || !s_ui.signal_meter || !s_ui.signal_function ||
        !s_ui.signal_address || !s_ui.signal_mask || !s_ui.signal_active) return false;

    signal->meter_index = (uint8_t)lv_dropdown_get_selected(s_ui.signal_meter);
    signal->function_code = lv_dropdown_get_selected(s_ui.signal_function) == 0U ? 3U : 4U;

    unsigned long value = 0U;
    if (!parse_unsigned(s_ui.signal_address, 0, 0U, 65535U, &value)) return false;
    signal->address = (uint16_t)value;
    if (!parse_unsigned(s_ui.signal_mask, 0, 0U, 65535U, &value)) return false;
    signal->mask = (uint16_t)value;
    if (!parse_unsigned(s_ui.signal_active, 0, 0U, 65535U, &value)) return false;
    signal->active_value = (uint16_t)value;
    return true;
}

static bool signal_ready_when_enabled(bool enabled, const source_commission_signal_t *signal)
{
    return !enabled || (signal && signal->mask != 0U &&
                        (signal->function_code == 3U || signal->function_code == 4U) &&
                        signal->meter_index < SOURCE_COMMISSIONING_MAX_METERS);
}

static bool enabled_channels_valid(const source_commission_config_t *config)
{
    if (!config) return false;
    if (!signal_ready_when_enabled(config->grid_evidence_enabled, &config->grid_available) ||
        !signal_ready_when_enabled(config->grid_evidence_enabled, &config->grid_breaker_closed)) {
        return false;
    }
    for (uint8_t i = 0U; i < SOURCE_COMMISSIONING_MAX_GENERATORS; ++i) {
        if (!signal_ready_when_enabled(config->generator_evidence_enabled[i],
                                       &config->generator_running[i]) ||
            !signal_ready_when_enabled(config->generator_evidence_enabled[i],
                                       &config->generator_breaker_closed[i])) {
            return false;
        }
    }
    return signal_ready_when_enabled(config->transfer_evidence_enabled, &config->transfer_active) &&
           signal_ready_when_enabled(config->synchronism_evidence_enabled,
                                     &config->grid_generator_synchronized);
}

static bool read_timing_form(source_commission_config_t *config)
{
    if (!config || !s_ui.enable_grid || !s_ui.enable_transfer || !s_ui.enable_sync) return false;

    config->grid_evidence_enabled = checked(s_ui.enable_grid);
    for (uint8_t i = 0U; i < SOURCE_COMMISSIONING_MAX_GENERATORS; ++i) {
        if (!s_ui.enable_generators[i]) return false;
        config->generator_evidence_enabled[i] = checked(s_ui.enable_generators[i]);
    }
    config->transfer_evidence_enabled = checked(s_ui.enable_transfer);
    config->synchronism_evidence_enabled = checked(s_ui.enable_sync);

    unsigned long value = 0U;
    if (!parse_unsigned(s_ui.poll_ms, 10, 100U, 60000U, &value)) return false;
    config->evidence_poll_interval_ms = (uint32_t)value;
    if (!parse_unsigned(s_ui.stale_ms, 10, config->evidence_poll_interval_ms, 600000U, &value)) return false;
    config->evidence_stale_timeout_ms = (uint32_t)value;
    if (!parse_unsigned(s_ui.loss_ms, 10, 0U, 60000U, &value)) return false;
    config->grid_loss_trip_ms = (uint32_t)value;
    if (!parse_unsigned(s_ui.recovery_ms, 10, 0U, 600000U, &value)) return false;
    config->grid_recovery_stable_ms = (uint32_t)value;

    return enabled_channels_valid(config);
}

static bool read_form(source_commission_config_t *config)
{
    if (!config) return false;
    if (s_ui.page < SOURCE_SIGNAL_PAGE_COUNT) {
        source_commission_signal_t *signal = signal_for_page(config, s_ui.page);
        return read_signal(signal);
    }
    return read_timing_form(config);
}

static void save_clicked(lv_event_t *event)
{
    (void)event;
    if (!s_ui.backend.save_config) return;
    source_commission_config_t next = s_ui.config;
    if (!read_form(&next)) {
        set_message("Source evidence is invalid. Enabled channels need a valid meter, FC03/FC04 and non-zero mask; timing must remain in Core bounds.", false);
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
            "Use the same Engineering password as the protected web workspace. This page commissions Grid, Generator 1..3, Transfer and Synchronism evidence directly into the authoritative Core model; it never infers source authority from measured power.");
    s_ui.credential = field(form, "Engineering credential", WAVESHARE_BENCH_CREDENTIAL, true);
    if (WAVESHARE_BENCH_CREDENTIAL[0] != '\0') {
        ESP_LOGW("source_commissioning_screen",
                 "BENCH BUILD: an Engineering credential is compiled into this image. This build must not ship.");
    }
    button(form, "Unlock source commissioning", unlock_clicked);
}

static void signal_fields(lv_obj_t *form, const source_commission_signal_t *signal)
{
    if (!signal) return;
    s_ui.signal_meter = dropdown_field(form, "Meter slot", "Meter 1\nMeter 2\nMeter 3\nMeter 4",
                                       signal->meter_index < SOURCE_COMMISSIONING_MAX_METERS
                                           ? signal->meter_index : 0U);
    s_ui.signal_function = dropdown_field(form, "Read function", "FC03\nFC04",
                                          signal->function_code == 4U ? 1U : 0U);
    s_ui.signal_address = hex_field(form, "PDU address (dec or 0xHEX)", signal->address);
    s_ui.signal_mask = hex_field(form, "Mask (dec or 0xHEX)", signal->mask);
    s_ui.signal_active = hex_field(form, "Active value (dec or 0xHEX)", signal->active_value);
}

static void render_signal_page(lv_obj_t *form)
{
    source_commission_signal_t *signal = signal_for_page(&s_ui.config, s_ui.page);
    heading(form, page_name(s_ui.page), page_detail(s_ui.page));
    signal_fields(form, signal);
    button(form, "Save this evidence channel", save_clicked);
}

static void render_timing_page(lv_obj_t *form)
{
    heading(form, "Enable source evidence + timing", page_detail(SOURCE_TIMING_PAGE));
    s_ui.enable_grid = checkbox_field(form, "Enable Grid pair", s_ui.config.grid_evidence_enabled);
    s_ui.enable_generators[0] = checkbox_field(form, "Enable Generator 1 pair", s_ui.config.generator_evidence_enabled[0]);
    s_ui.enable_generators[1] = checkbox_field(form, "Enable Generator 2 pair", s_ui.config.generator_evidence_enabled[1]);
    s_ui.enable_generators[2] = checkbox_field(form, "Enable Generator 3 pair", s_ui.config.generator_evidence_enabled[2]);
    s_ui.enable_transfer = checkbox_field(form, "Enable Transfer/ATS evidence", s_ui.config.transfer_evidence_enabled);
    s_ui.enable_sync = checkbox_field(form, "Enable Grid + Generator sync", s_ui.config.synchronism_evidence_enabled);
    s_ui.poll_ms = integer_field(form, "Evidence poll interval (ms)", s_ui.config.evidence_poll_interval_ms);
    s_ui.stale_ms = integer_field(form, "Evidence stale timeout (ms)", s_ui.config.evidence_stale_timeout_ms);
    s_ui.loss_ms = integer_field(form, "Grid loss trip (ms)", s_ui.config.grid_loss_trip_ms);
    s_ui.recovery_ms = integer_field(form, "Grid recovery stable (ms)", s_ui.config.grid_recovery_stable_ms);
    button(form, "Save enable / timing model", save_clicked);
    button(form, "Refresh from Core", refresh_clicked);
    if (s_ui.config.restart_required) button(form, "Restart controller", restart_clicked);
}

static void render_unlocked(void)
{
    lv_obj_t *form = form_container();
    heading(form, "Plant source evidence",
            "Commission exact contact/register evidence channel by channel. The final section enables only complete Grid/Generator pairs and optional Transfer/Sync channels backed by real site evidence.");

    lv_obj_t *nav = lv_obj_create(form);
    lv_obj_remove_style_all(nav);
    lv_obj_set_width(nav, LV_PCT(100));
    lv_obj_set_height(nav, 44);
    lv_obj_set_layout(nav, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    button(nav, "< Section", page_prev_clicked);
    char page_text[80];
    snprintf(page_text, sizeof(page_text), "%s %u/%u", page_name(s_ui.page),
             (unsigned)(s_ui.page + 1U), (unsigned)SOURCE_PAGE_COUNT);
    lv_obj_t *page_label = lv_label_create(nav);
    lv_label_set_text(page_label, page_text);
    lv_obj_set_width(page_label, 300);
    lv_obj_set_style_text_align(page_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    button(nav, "Section >", page_next_clicked);

    if (s_ui.page < SOURCE_SIGNAL_PAGE_COUNT) render_signal_page(form);
    else render_timing_page(form);
}

static void render(void)
{
    if (!s_ui.root || !s_ui.body) return;
    keyboard_hide();
    lv_obj_clean(s_ui.body);
    clear_form_refs();
    if (!s_ui.backend_set || !s_ui.config.unlocked) render_locked();
    else render_unlocked();
}

lv_obj_t *source_commissioning_screen_create(lv_obj_t *parent)
{
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.root = lv_obj_create(parent ? parent : lv_screen_active());
    make_fixed(s_ui.root);
    lv_obj_set_size(s_ui.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ui.root, lv_color_hex(SCREEN_COLOR_BG_APP), LV_PART_MAIN);
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
    lv_obj_set_height(top, 44);
    lv_obj_set_layout(top, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = lv_label_create(top);
    lv_label_set_text(title, "Source Evidence Commissioning");
    lv_obj_set_width(title, 500);
    lv_obj_set_style_text_color(title, lv_color_hex(SCREEN_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
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
