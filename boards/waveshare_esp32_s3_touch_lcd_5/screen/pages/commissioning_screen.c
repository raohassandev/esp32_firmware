#include "commissioning_screen.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMISSION_STEP_COUNT 8U
#define FORM_CONTROL_WIDTH 330
#define KEYBOARD_HEIGHT 190

typedef struct {
    lv_obj_t *root;
    lv_obj_t *body;
    lv_obj_t *step_label;
    lv_obj_t *message;
    lv_obj_t *keyboard;
    lv_obj_t *credential;

    lv_obj_t *enabled;
    lv_obj_t *name;
    lv_obj_t *role;
    lv_obj_t *model;
    lv_obj_t *phase_basis;
    lv_obj_t *profile;
    lv_obj_t *rated_kw;
    lv_obj_t *failsafe_ms;
    lv_obj_t *host;
    lv_obj_t *port;
    lv_obj_t *unit_id;
    lv_obj_t *timeout_ms;
    lv_obj_t *function_code;
    lv_obj_t *address;
    lv_obj_t *data_type;
    lv_obj_t *word_order;
    lv_obj_t *scale;
    lv_obj_t *poll_ms;

    lv_obj_t *policy;
    lv_obj_t *orientation;
    lv_obj_t *export_limit;
    lv_obj_t *minimum_import;
    lv_obj_t *sharing;
    lv_obj_t *generator_enabled;
    lv_obj_t *generator_rated;
    lv_obj_t *generator_minimum;
    lv_obj_t *generator_reserve;
    lv_obj_t *generator_reverse;
    lv_obj_t *generator_role;
    lv_obj_t *generator_base_load;
    lv_obj_t *base_tolerance_kw;
    lv_obj_t *base_tolerance_percent;
    lv_obj_t *grid_import_target;
    lv_obj_t *deadband;
    lv_obj_t *kp;
    lv_obj_t *ki;
    lv_obj_t *control_interval;
    lv_obj_t *stale_timeout;
    lv_obj_t *grid_ramp_enabled;
    lv_obj_t *grid_ramp_up;
    lv_obj_t *grid_ramp_down;
    lv_obj_t *generator_ramp_enabled;
    lv_obj_t *generator_ramp_up;
    lv_obj_t *generator_ramp_down;
    lv_obj_t *urgent_fraction;
    lv_obj_t *urgent_multiplier;

    screen_commissioning_backend_t backend;
    screen_commissioning_config_t config;
    screen_commissioning_snapshot_t gate;
    screen_status_snapshot_t status;
    screen_meters_snapshot_t meters;
    screen_inverters_snapshot_t inverters;
    screen_telemetry_snapshot_t telemetry;
    bool backend_set;
    uint8_t step;
    uint8_t device_kind; /* 0 meter, 1 inverter */
    uint8_t meter_index;
    uint8_t inverter_index;
    uint8_t generator_index;
} commissioning_ui_t;

static commissioning_ui_t s_ui;

static const char *const s_step_names[COMMISSION_STEP_COUNT] = {
    "Site", "Devices", "Channel", "Modbus", "Plant", "Test", "Health", "Review"
};

static void render(void);

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
    screen_commissioning_config_t next = {0};
    if (!s_ui.backend.read_config(s_ui.backend.context, &next) || !next.valid) return false;
    s_ui.config = next;
    return true;
}

static void keyboard_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_ui.keyboard, NULL);
    }
}

static void textarea_focus(lv_event_t *event)
{
    if (!s_ui.keyboard) return;
    lv_obj_t *textarea = lv_event_get_target_obj(event);
    lv_keyboard_set_textarea(s_ui.keyboard, textarea);
    lv_obj_remove_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ui.keyboard);
}

static lv_obj_t *field(lv_obj_t *parent, const char *label, const char *value, bool password)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *caption = lv_label_create(row);
    lv_label_set_text(caption, label);
    lv_obj_set_width(caption, 250);
    lv_obj_set_style_text_color(caption, lv_color_hex(0xC7D0DA), LV_PART_MAIN);

    lv_obj_t *input = lv_textarea_create(row);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_password_mode(input, password);
    lv_textarea_set_text(input, value ? value : "");
    lv_obj_set_width(input, FORM_CONTROL_WIDTH);
    lv_obj_add_event_cb(input, textarea_focus, LV_EVENT_FOCUSED, NULL);
    return input;
}

static lv_obj_t *number_field(lv_obj_t *parent, const char *label, double value, unsigned precision)
{
    char text[48];
    snprintf(text, sizeof(text), "%.*f", (int)precision, value);
    return field(parent, label, text, false);
}

static lv_obj_t *integer_field(lv_obj_t *parent, const char *label, unsigned long value)
{
    char text[32];
    snprintf(text, sizeof(text), "%lu", value);
    return field(parent, label, text, false);
}

static lv_obj_t *checkbox_field(lv_obj_t *parent, const char *label, bool checked)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t *caption = lv_label_create(row);
    lv_label_set_text(caption, label);
    lv_obj_set_width(caption, 250);
    lv_obj_set_style_text_color(caption, lv_color_hex(0xC7D0DA), LV_PART_MAIN);
    lv_obj_t *box = lv_checkbox_create(row);
    lv_checkbox_set_text(box, "");
    if (checked) lv_obj_add_state(box, LV_STATE_CHECKED);
    lv_obj_set_width(box, FORM_CONTROL_WIDTH);
    return box;
}

static lv_obj_t *dropdown_field(lv_obj_t *parent, const char *label,
                                const char *options, uint32_t selected)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t *caption = lv_label_create(row);
    lv_label_set_text(caption, label);
    lv_obj_set_width(caption, 250);
    lv_obj_set_style_text_color(caption, lv_color_hex(0xC7D0DA), LV_PART_MAIN);
    lv_obj_t *drop = lv_dropdown_create(row);
    lv_dropdown_set_options(drop, options);
    lv_dropdown_set_selected(drop, selected);
    lv_obj_set_width(drop, FORM_CONTROL_WIDTH);
    return drop;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t callback, void *data)
{
    lv_obj_t *obj = lv_button_create(parent);
    make_fixed(obj);
    lv_obj_set_height(obj, 38);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, data);
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
    if (detail && detail[0]) {
        lv_obj_t *d = lv_label_create(parent);
        lv_label_set_text(d, detail);
        lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(d, LV_PCT(100));
        lv_obj_set_style_text_color(d, lv_color_hex(0x9EADBF), LV_PART_MAIN);
    }
}

static bool checked(lv_obj_t *obj)
{
    return obj && lv_obj_has_state(obj, LV_STATE_CHECKED);
}

static bool parse_ulong(lv_obj_t *obj, unsigned long minimum, unsigned long maximum,
                        unsigned long *value)
{
    if (!obj || !value) return false;
    const char *text = lv_textarea_get_text(obj);
    char *end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno || !end || *end != '\0' || parsed < minimum || parsed > maximum) return false;
    *value = parsed;
    return true;
}

static bool parse_float(lv_obj_t *obj, float *value)
{
    if (!obj || !value) return false;
    const char *text = lv_textarea_get_text(obj);
    char *end = NULL;
    errno = 0;
    const float parsed = strtof(text, &end);
    if (errno || !end || *end != '\0') return false;
    *value = parsed;
    return true;
}

static void clear_form_refs(void)
{
    s_ui.enabled = s_ui.name = s_ui.role = s_ui.model = s_ui.phase_basis = NULL;
    s_ui.profile = s_ui.rated_kw = s_ui.failsafe_ms = NULL;
    s_ui.host = s_ui.port = s_ui.unit_id = s_ui.timeout_ms = NULL;
    s_ui.function_code = s_ui.address = s_ui.data_type = s_ui.word_order = NULL;
    s_ui.scale = s_ui.poll_ms = NULL;
    s_ui.policy = s_ui.orientation = s_ui.export_limit = s_ui.minimum_import = NULL;
    s_ui.sharing = s_ui.generator_enabled = s_ui.generator_rated = NULL;
    s_ui.generator_minimum = s_ui.generator_reserve = s_ui.generator_reverse = NULL;
    s_ui.generator_role = s_ui.generator_base_load = NULL;
    s_ui.base_tolerance_kw = s_ui.base_tolerance_percent = NULL;
    s_ui.grid_import_target = s_ui.deadband = s_ui.kp = s_ui.ki = NULL;
    s_ui.control_interval = s_ui.stale_timeout = NULL;
    s_ui.grid_ramp_enabled = s_ui.grid_ramp_up = s_ui.grid_ramp_down = NULL;
    s_ui.generator_ramp_enabled = s_ui.generator_ramp_up = s_ui.generator_ramp_down = NULL;
    s_ui.urgent_fraction = s_ui.urgent_multiplier = NULL;
}

static void result_message(const screen_commission_action_result_t *result)
{
    if (!result) return;
    if (result->restart_required) s_ui.config.restart_required = true;
    set_message(result->message[0] ? result->message : (result->ok ? "Saved" : "Operation failed"),
                result->ok);
}

static void unlock_clicked(lv_event_t *event)
{
    (void)event;
    if (!s_ui.backend_set || !s_ui.backend.unlock || !s_ui.credential) return;
    const char *credential = lv_textarea_get_text(s_ui.credential);
    uint32_t retry = 0U;
    bool setup = false;
    screen_commission_auth_result_t result =
        s_ui.backend.unlock(s_ui.backend.context, credential, &retry, &setup);
    lv_textarea_set_text(s_ui.credential, "");
    lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
    if (result == SCREEN_COMMISSION_AUTH_OK) {
        if (!load_config()) {
            set_message("Engineering unlocked, but configuration could not be read.", false);
            return;
        }
        s_ui.config.unlocked = true;
        s_ui.config.setup_required = setup;
        render();
        set_message(setup ? "Unlocked with one-time setup code. Configure a permanent Engineering password after commissioning."
                          : "Engineering unlocked on the local HMI.", true);
    } else if (result == SCREEN_COMMISSION_AUTH_LOCKED) {
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
    if (s_ui.backend_set && s_ui.backend.lock) s_ui.backend.lock(s_ui.backend.context);
    memset(&s_ui.config, 0, sizeof(s_ui.config));
    render();
}

static void step_clicked(lv_event_t *event)
{
    const uintptr_t raw = (uintptr_t)lv_event_get_user_data(event);
    if (raw >= COMMISSION_STEP_COUNT) return;
    s_ui.step = (uint8_t)raw;
    render();
}

static void next_clicked(lv_event_t *event)
{
    (void)event;
    if (s_ui.step + 1U < COMMISSION_STEP_COUNT) s_ui.step++;
    render();
}

static void back_clicked(lv_event_t *event)
{
    (void)event;
    if (s_ui.step > 0U) s_ui.step--;
    render();
}

static void device_kind_clicked(lv_event_t *event)
{
    s_ui.device_kind = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    render();
}

static void slot_prev_clicked(lv_event_t *event)
{
    (void)event;
    if (s_ui.device_kind == 0U) {
        if (s_ui.meter_index > 0U) s_ui.meter_index--;
    } else if (s_ui.inverter_index > 0U) s_ui.inverter_index--;
    render();
}

static void slot_next_clicked(lv_event_t *event)
{
    (void)event;
    if (s_ui.device_kind == 0U) {
        if (s_ui.meter_index + 1U < SCREEN_COMMISSIONING_MAX_METERS) s_ui.meter_index++;
    } else if (s_ui.inverter_index + 1U < SCREEN_COMMISSIONING_MAX_INVERTERS) s_ui.inverter_index++;
    render();
}

static void generator_prev_clicked(lv_event_t *event)
{
    (void)event;
    if (s_ui.generator_index > 0U) s_ui.generator_index--;
    render();
}

static void generator_next_clicked(lv_event_t *event)
{
    (void)event;
    if (s_ui.generator_index + 1U < SCREEN_COMMISSIONING_MAX_GENERATORS) s_ui.generator_index++;
    render();
}

static void save_site_clicked(lv_event_t *event)
{
    (void)event;
    if (!s_ui.backend.save_site || !s_ui.name) return;
    screen_commission_action_result_t result = {0};
    (void)s_ui.backend.save_site(s_ui.backend.context, lv_textarea_get_text(s_ui.name), &result);
    result_message(&result);
    if (result.ok) (void)load_config();
}

static bool read_meter_form(screen_commission_meter_t *meter, bool include_channel, bool include_modbus)
{
    if (!meter) return false;
    if (s_ui.enabled) meter->enabled = checked(s_ui.enabled);
    if (s_ui.name) snprintf(meter->name, sizeof(meter->name), "%s", lv_textarea_get_text(s_ui.name));
    if (s_ui.role) meter->role = (uint8_t)lv_dropdown_get_selected(s_ui.role);
    meter->generator_index = meter->role == 2U ? meter->generator_index : 0xFFU;
    if (meter->role == 2U && meter->generator_index >= SCREEN_COMMISSIONING_MAX_GENERATORS) meter->generator_index = 0U;
    if (s_ui.model) meter->model = lv_dropdown_get_selected(s_ui.model);
    if (s_ui.phase_basis) meter->phase_basis = lv_dropdown_get_selected(s_ui.phase_basis);
    if (include_channel) {
        unsigned long value = 0U;
        snprintf(meter->host, sizeof(meter->host), "%s", lv_textarea_get_text(s_ui.host));
        if (!parse_ulong(s_ui.port, 1U, 65535U, &value)) return false; meter->port = (uint16_t)value;
        if (!parse_ulong(s_ui.unit_id, 1U, 247U, &value)) return false; meter->unit_id = (uint8_t)value;
        if (!parse_ulong(s_ui.timeout_ms, 100U, 60000U, &value)) return false; meter->timeout_ms = (uint32_t)value;
    }
    if (include_modbus) {
        unsigned long value = 0U;
        meter->function_code = lv_dropdown_get_selected(s_ui.function_code) == 0U ? 3U : 4U;
        if (!parse_ulong(s_ui.address, 0U, 65535U, &value)) return false; meter->active_power_address = (uint16_t)value;
        meter->data_type = (uint8_t)lv_dropdown_get_selected(s_ui.data_type);
        meter->word_order = (uint8_t)lv_dropdown_get_selected(s_ui.word_order);
        if (!parse_float(s_ui.scale, &meter->scale)) return false;
        if (!parse_ulong(s_ui.poll_ms, 0U, 3600000U, &value)) return false; meter->poll_ms = (uint32_t)value;
    }
    return true;
}

static bool read_inverter_form(screen_commission_inverter_t *inverter, bool include_channel)
{
    if (!inverter) return false;
    if (s_ui.enabled) inverter->enabled = checked(s_ui.enabled);
    if (s_ui.name) snprintf(inverter->name, sizeof(inverter->name), "%s", lv_textarea_get_text(s_ui.name));
    if (s_ui.rated_kw && !parse_float(s_ui.rated_kw, &inverter->rated_kw)) return false;
    if (s_ui.failsafe_ms) {
        unsigned long value = 0U;
        if (!parse_ulong(s_ui.failsafe_ms, 0U, 3600000U, &value)) return false;
        inverter->comms_failsafe_ms = (uint32_t)value;
    }
    if (s_ui.profile && s_ui.config.profile_count > 0U) {
        const uint32_t selected = lv_dropdown_get_selected(s_ui.profile);
        if (selected < s_ui.config.profile_count) {
            snprintf(inverter->profile_id, sizeof(inverter->profile_id), "%s",
                     s_ui.config.profiles[selected].id);
        }
    }
    if (include_channel) {
        unsigned long value = 0U;
        snprintf(inverter->host, sizeof(inverter->host), "%s", lv_textarea_get_text(s_ui.host));
        if (!parse_ulong(s_ui.port, 1U, 65535U, &value)) return false; inverter->port = (uint16_t)value;
        if (!parse_ulong(s_ui.unit_id, 1U, 247U, &value)) return false; inverter->unit_id = (uint8_t)value;
        if (!parse_ulong(s_ui.timeout_ms, 100U, 60000U, &value)) return false; inverter->timeout_ms = (uint32_t)value;
    }
    return true;
}

static void save_device_clicked(lv_event_t *event)
{
    (void)event;
    screen_commission_action_result_t result = {0};
    if (s_ui.device_kind == 0U) {
        screen_commission_meter_t meter = s_ui.config.meters[s_ui.meter_index];
        if (!read_meter_form(&meter, false, false)) {
            set_message("Meter fields contain an invalid value.", false); return;
        }
        if (!s_ui.backend.save_meter ||
            !s_ui.backend.save_meter(s_ui.backend.context, s_ui.meter_index, &meter, &result)) {
            result_message(&result); return;
        }
    } else {
        screen_commission_inverter_t inverter = s_ui.config.inverters[s_ui.inverter_index];
        if (!read_inverter_form(&inverter, false)) {
            set_message("Inverter fields contain an invalid value.", false); return;
        }
        if (!s_ui.backend.save_inverter ||
            !s_ui.backend.save_inverter(s_ui.backend.context, s_ui.inverter_index, &inverter, &result)) {
            result_message(&result); return;
        }
    }
    result_message(&result);
    if (result.ok) (void)load_config();
}

static void save_channel_clicked(lv_event_t *event)
{
    (void)event;
    screen_commission_action_result_t result = {0};
    if (s_ui.device_kind == 0U) {
        screen_commission_meter_t meter = s_ui.config.meters[s_ui.meter_index];
        if (!read_meter_form(&meter, true, false)) { set_message("TCP channel values are invalid.", false); return; }
        (void)s_ui.backend.save_meter(s_ui.backend.context, s_ui.meter_index, &meter, &result);
    } else {
        screen_commission_inverter_t inverter = s_ui.config.inverters[s_ui.inverter_index];
        if (!read_inverter_form(&inverter, true)) { set_message("TCP channel values are invalid.", false); return; }
        (void)s_ui.backend.save_inverter(s_ui.backend.context, s_ui.inverter_index, &inverter, &result);
    }
    result_message(&result);
    if (result.ok) (void)load_config();
}

static void save_modbus_clicked(lv_event_t *event)
{
    (void)event;
    screen_commission_meter_t meter = s_ui.config.meters[s_ui.meter_index];
    if (!read_meter_form(&meter, false, true)) { set_message("Modbus tuning values are invalid.", false); return; }
    screen_commission_action_result_t result = {0};
    (void)s_ui.backend.save_meter(s_ui.backend.context, s_ui.meter_index, &meter, &result);
    result_message(&result);
    if (result.ok) (void)load_config();
}

static bool read_plant_form(screen_commission_plant_t *plant)
{
    if (!plant) return false;
    plant->policy = (uint8_t)lv_dropdown_get_selected(s_ui.policy);
    plant->meter_orientation = (uint8_t)lv_dropdown_get_selected(s_ui.orientation);
    plant->load_sharing_mode = (uint8_t)lv_dropdown_get_selected(s_ui.sharing);
    if (!parse_float(s_ui.export_limit, &plant->export_limit_kw) ||
        !parse_float(s_ui.minimum_import, &plant->minimum_import_kw) ||
        !parse_float(s_ui.base_tolerance_kw, &plant->base_load_tolerance_kw) ||
        !parse_float(s_ui.base_tolerance_percent, &plant->base_load_tolerance_percent) ||
        !parse_float(s_ui.grid_import_target, &plant->grid_import_target_kw) ||
        !parse_float(s_ui.deadband, &plant->deadband_kw) ||
        !parse_float(s_ui.kp, &plant->kp) || !parse_float(s_ui.ki, &plant->ki) ||
        !parse_float(s_ui.grid_ramp_up, &plant->grid_ramp_up_percent_per_second) ||
        !parse_float(s_ui.grid_ramp_down, &plant->grid_ramp_down_percent_per_second) ||
        !parse_float(s_ui.generator_ramp_up, &plant->generator_ramp_up_percent_per_second) ||
        !parse_float(s_ui.generator_ramp_down, &plant->generator_ramp_down_percent_per_second) ||
        !parse_float(s_ui.urgent_fraction, &plant->urgent_loading_fraction) ||
        !parse_float(s_ui.urgent_multiplier, &plant->urgent_ramp_multiplier)) return false;
    unsigned long value = 0U;
    if (!parse_ulong(s_ui.control_interval, 50U, 3600000U, &value)) return false;
    plant->control_interval_ms = (uint32_t)value;
    if (!parse_ulong(s_ui.stale_timeout, 100U, 3600000U, &value)) return false;
    plant->meter_stale_timeout_ms = (uint32_t)value;
    plant->grid_ramp_enabled = checked(s_ui.grid_ramp_enabled);
    plant->generator_ramp_enabled = checked(s_ui.generator_ramp_enabled);

    screen_commission_generator_t *g = &plant->generators[s_ui.generator_index];
    g->enabled = checked(s_ui.generator_enabled);
    g->role = (uint8_t)lv_dropdown_get_selected(s_ui.generator_role);
    if (!parse_float(s_ui.generator_rated, &g->rated_kw) ||
        !parse_float(s_ui.generator_minimum, &g->minimum_loading_percent) ||
        !parse_float(s_ui.generator_reserve, &g->reserve_kw) ||
        !parse_float(s_ui.generator_reverse, &g->reverse_power_margin_kw) ||
        !parse_float(s_ui.generator_base_load, &g->base_load_kw)) return false;
    return true;
}

static void save_plant_clicked(lv_event_t *event)
{
    (void)event;
    screen_commission_plant_t plant = s_ui.config.plant;
    if (!read_plant_form(&plant)) { set_message("Plant control values are invalid.", false); return; }
    screen_commission_action_result_t result = {0};
    (void)s_ui.backend.save_plant(s_ui.backend.context, &plant, &result);
    result_message(&result);
    if (result.ok) (void)load_config();
}

static void refresh_clicked(lv_event_t *event)
{
    (void)event;
    if (load_config()) { render(); set_message("Controller configuration/evidence refreshed.", true); }
    else set_message("Controller configuration could not be refreshed.", false);
}

static void control_clicked(lv_event_t *event)
{
    const bool enabled = (uintptr_t)lv_event_get_user_data(event) != 0U;
    if (!s_ui.backend.set_control_enabled) return;
    screen_commission_action_result_t result = {0};
    (void)s_ui.backend.set_control_enabled(s_ui.backend.context, enabled, &result);
    result_message(&result);
    if (result.ok) (void)load_config();
}

static void restart_clicked(lv_event_t *event)
{
    (void)event;
    if (!s_ui.backend.restart_controller) return;
    screen_commission_action_result_t result = {0};
    (void)s_ui.backend.restart_controller(s_ui.backend.context, &result);
    result_message(&result);
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

static void device_selector(lv_obj_t *form)
{
    lv_obj_t *bar = lv_obj_create(form);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, 42);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, 6, LV_PART_MAIN);
    button(bar, "Meter", device_kind_clicked, (void *)(uintptr_t)0U);
    button(bar, "Inverter", device_kind_clicked, (void *)(uintptr_t)1U);
    button(bar, "<", slot_prev_clicked, NULL);
    char slot[40];
    snprintf(slot, sizeof(slot), "%s %u/%u",
             s_ui.device_kind == 0U ? "Meter" : "Inv",
             (unsigned)(s_ui.device_kind == 0U ? s_ui.meter_index + 1U : s_ui.inverter_index + 1U),
             (unsigned)(s_ui.device_kind == 0U ? SCREEN_COMMISSIONING_MAX_METERS : SCREEN_COMMISSIONING_MAX_INVERTERS));
    lv_obj_t *label = lv_label_create(bar);
    lv_label_set_text(label, slot);
    lv_obj_set_width(label, 120);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    button(bar, ">", slot_next_clicked, NULL);
}

static void render_locked(void)
{
    lv_obj_t *form = form_container();
    heading(form, "Engineering unlock", "Use the same Engineering password as the protected web workspace. A fresh unit may use its one-time SETUP code from the serial console. Five failed attempts share the normal Engineering lockout.");
    s_ui.credential = field(form, "Engineering credential", "", true);
    button(form, "Unlock commissioning", unlock_clicked, NULL);
}

static void render_site(void)
{
    lv_obj_t *form = form_container();
    heading(form, "Site / controller identity", "The controller currently persists one device/site name. Location, engineer and project-reference fields are not invented here because Core schema 9 does not persist them yet.");
    s_ui.name = field(form, "Controller / site name", s_ui.config.device_name, false);
    button(form, "Save site name", save_site_clicked, NULL);
}

static uint32_t profile_selected(const char *id)
{
    for (uint32_t i = 0U; i < s_ui.config.profile_count; ++i) {
        if (strcmp(id ? id : "", s_ui.config.profiles[i].id) == 0) return i;
    }
    return 0U;
}

static void render_devices(void)
{
    lv_obj_t *form = form_container();
    heading(form, "Devices", "Declare installed equipment. Unknown or out-of-scope equipment remains a valid stored configuration but the Core commissioning gate refuses command authority.");
    device_selector(form);
    if (s_ui.device_kind == 0U) {
        screen_commission_meter_t *m = &s_ui.config.meters[s_ui.meter_index];
        s_ui.enabled = checkbox_field(form, "Enabled", m->enabled);
        s_ui.name = field(form, "Name", m->name, false);
        s_ui.role = dropdown_field(form, "Role", "Unassigned\nGrid\nGenerator\nLoad\nPV", m->role <= 4U ? m->role : 0U);
        s_ui.model = dropdown_field(form, "Meter model", "Undeclared\nAutomatrix EM500/Lovato\nGeneric Modbus (out of phase)", m->model <= 2U ? m->model : 0U);
        s_ui.phase_basis = dropdown_field(form, "Grid phase basis", "Lowest phase\nTotal", m->phase_basis <= 1U ? m->phase_basis : 0U);
    } else {
        screen_commission_inverter_t *v = &s_ui.config.inverters[s_ui.inverter_index];
        s_ui.enabled = checkbox_field(form, "Enabled", v->enabled);
        s_ui.name = field(form, "Name", v->name, false);
        s_ui.rated_kw = number_field(form, "Rated power (kW)", v->rated_kw, 2U);
        s_ui.failsafe_ms = integer_field(form, "Inverter comms fail-safe (ms, 0=unstated)", v->comms_failsafe_ms);
        char options[1200] = {0};
        size_t used = 0U;
        for (uint8_t i = 0U; i < s_ui.config.profile_count; ++i) {
            const screen_commission_profile_t *p = &s_ui.config.profiles[i];
            const int n = snprintf(options + used, sizeof(options) - used, "%s%s - %s%s",
                                   i ? "\n" : "", p->manufacturer, p->model,
                                   p->deferred_this_phase ? " [parked]" : "");
            if (n < 0 || (size_t)n >= sizeof(options) - used) break;
            used += (size_t)n;
        }
        if (used == 0U) snprintf(options, sizeof(options), "No profiles available");
        s_ui.profile = dropdown_field(form, "Inverter profile", options, profile_selected(v->profile_id));
    }
    button(form, "Save device", save_device_clicked, NULL);
}

static void render_channel(void)
{
    lv_obj_t *form = form_container();
    heading(form, "Communication channel", "This release commissions Modbus TCP only. Native Modbus RTU remains unavailable until the RS-485 runtime is implemented and physically qualified; the HMI will not fake a Ready verdict.");
    device_selector(form);
    if (s_ui.device_kind == 0U) {
        screen_commission_meter_t *m = &s_ui.config.meters[s_ui.meter_index];
        s_ui.host = field(form, "TCP IP / host", m->host, false);
        s_ui.port = integer_field(form, "TCP port", m->port ? m->port : 502U);
        s_ui.unit_id = integer_field(form, "Unit ID", m->unit_id ? m->unit_id : 1U);
        s_ui.timeout_ms = integer_field(form, "Timeout (ms)", m->timeout_ms ? m->timeout_ms : 1000U);
    } else {
        screen_commission_inverter_t *v = &s_ui.config.inverters[s_ui.inverter_index];
        s_ui.host = field(form, "TCP IP / host", v->host, false);
        s_ui.port = integer_field(form, "TCP port", v->port ? v->port : 502U);
        s_ui.unit_id = integer_field(form, "Unit ID", v->unit_id ? v->unit_id : 1U);
        s_ui.timeout_ms = integer_field(form, "Timeout (ms)", v->timeout_ms ? v->timeout_ms : 1000U);
    }
    button(form, "Save TCP channel", save_channel_clicked, NULL);
}

static void render_modbus(void)
{
    lv_obj_t *form = form_container();
    heading(form, "Modbus tuning", "Meter decode is commissioned explicitly. Inverter register maps are profile-owned so this page never lets a local UI invent a manufacturer register address.");
    s_ui.device_kind = 0U;
    device_selector(form);
    screen_commission_meter_t *m = &s_ui.config.meters[s_ui.meter_index];
    s_ui.function_code = dropdown_field(form, "Active power function", "FC03\nFC04", m->function_code == 4U ? 1U : 0U);
    s_ui.address = integer_field(form, "Active power PDU address", m->active_power_address);
    s_ui.data_type = dropdown_field(form, "Data type", "UINT16\nINT16\nUINT32\nINT32\nFLOAT32", m->data_type <= 4U ? m->data_type : 0U);
    s_ui.word_order = dropdown_field(form, "Word order", "ABCD\nCDAB\nBADC\nDCBA", m->word_order <= 3U ? m->word_order : 0U);
    s_ui.scale = number_field(form, "Scale to kW", m->scale, 6U);
    s_ui.poll_ms = integer_field(form, "Poll interval (ms)", m->poll_ms);
    button(form, "Save meter tuning", save_modbus_clicked, NULL);
}

static void render_plant(void)
{
    lv_obj_t *form = form_container();
    heading(form, "Plant control", "Every save forces automatic control disabled. Core validators remain authoritative; a value the Core refuses is not persisted by this screen.");
    screen_commission_plant_t *p = &s_ui.config.plant;
    s_ui.policy = dropdown_field(form, "Grid policy", "Zero export\nLimited export\nMinimum import", p->policy <= 2U ? p->policy : 0U);
    s_ui.orientation = dropdown_field(form, "Meter orientation", "Import positive\nExport positive", p->meter_orientation <= 1U ? p->meter_orientation : 0U);
    s_ui.export_limit = number_field(form, "Export limit (kW)", p->export_limit_kw, 2U);
    s_ui.minimum_import = number_field(form, "Minimum import (kW)", p->minimum_import_kw, 2U);
    s_ui.sharing = dropdown_field(form, "Generator load sharing", "Unset\nIsochronous\nBase load\nDroop (refused)", p->load_sharing_mode <= 3U ? p->load_sharing_mode : 0U);

    lv_obj_t *selector = lv_obj_create(form);
    lv_obj_remove_style_all(selector);
    lv_obj_set_width(selector, LV_PCT(100));
    lv_obj_set_height(selector, 40);
    lv_obj_set_layout(selector, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(selector, LV_FLEX_FLOW_ROW);
    button(selector, "< Generator", generator_prev_clicked, NULL);
    char slot[32]; snprintf(slot, sizeof(slot), "Generator %u/%u", (unsigned)(s_ui.generator_index + 1U), (unsigned)SCREEN_COMMISSIONING_MAX_GENERATORS);
    lv_obj_t *slot_label = lv_label_create(selector); lv_label_set_text(slot_label, slot); lv_obj_set_width(slot_label, 180);
    button(selector, "Generator >", generator_next_clicked, NULL);

    screen_commission_generator_t *g = &p->generators[s_ui.generator_index];
    s_ui.generator_enabled = checkbox_field(form, "Generator slot enabled", g->enabled);
    s_ui.generator_rated = number_field(form, "Generator rated kW", g->rated_kw, 2U);
    s_ui.generator_minimum = number_field(form, "Minimum loading (%)", g->minimum_loading_percent, 2U);
    s_ui.generator_reserve = number_field(form, "Reserve (kW)", g->reserve_kw, 2U);
    s_ui.generator_reverse = number_field(form, "Reverse-power margin (kW)", g->reverse_power_margin_kw, 2U);
    s_ui.generator_role = dropdown_field(form, "Base-load role", "Unset\nSwing\nBase load", g->role <= 2U ? g->role : 0U);
    s_ui.generator_base_load = number_field(form, "Base-load setpoint (kW)", g->base_load_kw, 2U);
    s_ui.base_tolerance_kw = number_field(form, "Base-load tolerance (kW)", p->base_load_tolerance_kw, 2U);
    s_ui.base_tolerance_percent = number_field(form, "Base-load tolerance (% rating)", p->base_load_tolerance_percent, 2U);

    s_ui.grid_import_target = number_field(form, "Grid import target (kW)", p->grid_import_target_kw, 2U);
    s_ui.deadband = number_field(form, "Control deadband (kW)", p->deadband_kw, 2U);
    s_ui.kp = number_field(form, "Kp", p->kp, 3U);
    s_ui.ki = number_field(form, "Ki", p->ki, 3U);
    s_ui.control_interval = integer_field(form, "Control interval (ms)", p->control_interval_ms);
    s_ui.stale_timeout = integer_field(form, "Meter stale timeout (ms)", p->meter_stale_timeout_ms);
    s_ui.grid_ramp_enabled = checkbox_field(form, "Grid ramp enabled", p->grid_ramp_enabled);
    s_ui.grid_ramp_up = number_field(form, "Grid ramp up (%/s)", p->grid_ramp_up_percent_per_second, 2U);
    s_ui.grid_ramp_down = number_field(form, "Grid ramp down (%/s)", p->grid_ramp_down_percent_per_second, 2U);
    s_ui.generator_ramp_enabled = checkbox_field(form, "Generator ramp enabled", p->generator_ramp_enabled);
    s_ui.generator_ramp_up = number_field(form, "Generator ramp up (%/s)", p->generator_ramp_up_percent_per_second, 2U);
    s_ui.generator_ramp_down = number_field(form, "Generator ramp down (%/s)", p->generator_ramp_down_percent_per_second, 2U);
    s_ui.urgent_fraction = number_field(form, "Urgent loading fraction (0..1)", p->urgent_loading_fraction, 3U);
    s_ui.urgent_multiplier = number_field(form, "Urgent ramp multiplier", p->urgent_ramp_multiplier, 2U);
    button(form, "Save plant control", save_plant_clicked, NULL);
}

static void status_line(lv_obj_t *form, const char *name, const char *value)
{
    lv_obj_t *row = lv_obj_create(form);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *a = lv_label_create(row); lv_label_set_text(a, name); lv_obj_set_width(a, 300);
    lv_obj_t *b = lv_label_create(row); lv_label_set_text(b, value); lv_obj_set_width(b, 330); lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
}

static void render_test(void)
{
    lv_obj_t *form = form_container();
    heading(form, "Connection qualification", "No Modbus I/O is performed inside the LVGL event handler. This screen watches the controller's background acquisition evidence; after a changed endpoint, restart first so the runtime uses the saved settings.");
    if (s_ui.config.restart_required) {
        status_line(form, "Configuration", "Restart required before qualification");
        button(form, "Restart controller now", restart_clicked, NULL);
    }
    char text[96];
    snprintf(text, sizeof(text), "%u enabled / %u online", (unsigned)s_ui.meters.enabled, (unsigned)s_ui.meters.online);
    status_line(form, "Meters", s_ui.meters.valid ? text : "Evidence unavailable");
    snprintf(text, sizeof(text), "%u enabled / %u online", (unsigned)s_ui.inverters.enabled, (unsigned)s_ui.inverters.online);
    status_line(form, "Inverters", s_ui.inverters.valid ? text : "Evidence unavailable");
    status_line(form, "RTU", "Unavailable in this release candidate");
    button(form, "Refresh evidence", refresh_clicked, NULL);
}

static void render_health(void)
{
    lv_obj_t *form = form_container();
    heading(form, "Controller health", "Health is read from the same cached Core status used by the operator UI. Unknown stays unknown.");
    status_line(form, "Network", !s_ui.status.valid ? "--" : s_ui.status.network_online ? "Online" : "Offline");
    status_line(form, "Controller", s_ui.status.valid && s_ui.status.controller_state[0] ? s_ui.status.controller_state : "--");
    status_line(form, "Monitoring ready", !s_ui.telemetry.valid ? "--" : s_ui.telemetry.monitoring_ready ? "Yes" : "No");
    status_line(form, "Command path ready", !s_ui.telemetry.valid ? "--" : s_ui.telemetry.command_path_ready ? "Yes" : "No");
    status_line(form, "Automatic control", !s_ui.telemetry.valid ? "--" : s_ui.telemetry.automatic_control_active ? "Active" : "Disabled");
    button(form, "Refresh health", refresh_clicked, NULL);
}

static void render_review(void)
{
    lv_obj_t *form = form_container();
    heading(form, "Review / finish", "The Core commissioning gate is the final authority. The HMI cannot override an unmet prerequisite.");
    if (!s_ui.gate.valid) {
        status_line(form, "Commissioning gate", "Unavailable");
    } else {
        status_line(form, "Commissioned", s_ui.gate.commissioned ? "YES" : "NO");
        status_line(form, "Scope", s_ui.gate.scope[0] ? s_ui.gate.scope : "--");
        char count[64];
        snprintf(count, sizeof(count), "%u/%u met (%u unmet)",
                 (unsigned)s_ui.gate.satisfied_count, (unsigned)s_ui.gate.prerequisite_count,
                 (unsigned)s_ui.gate.unmet_count);
        status_line(form, "Prerequisites", count);
        if (!s_ui.gate.commissioned) {
            status_line(form, "Next blocker", s_ui.gate.first_unmet_title[0] ? s_ui.gate.first_unmet_title : "--");
            lv_obj_t *detail = lv_label_create(form);
            lv_label_set_text(detail, s_ui.gate.first_unmet_detail[0] ? s_ui.gate.first_unmet_detail : s_ui.gate.inhibit_reason);
            lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(detail, LV_PCT(100));
            lv_obj_set_style_text_color(detail, lv_color_hex(0xF2B84B), LV_PART_MAIN);
        }
    }
    if (s_ui.config.restart_required) button(form, "Restart controller", restart_clicked, NULL);
    button(form, "DISARM automatic control", control_clicked, (void *)(uintptr_t)0U);
    lv_obj_t *arm = button(form, "ARM automatic control", control_clicked, (void *)(uintptr_t)1U);
    if (!s_ui.gate.valid || !s_ui.gate.commissioned) lv_obj_add_state(arm, LV_STATE_DISABLED);
    button(form, "Refresh gate/config", refresh_clicked, NULL);
}

static void render_step(void)
{
    switch (s_ui.step) {
    case 0: render_site(); break;
    case 1: render_devices(); break;
    case 2: render_channel(); break;
    case 3: render_modbus(); break;
    case 4: render_plant(); break;
    case 5: render_test(); break;
    case 6: render_health(); break;
    case 7: render_review(); break;
    default: s_ui.step = 0U; render_site(); break;
    }
}

static void render(void)
{
    if (!s_ui.root || !s_ui.body) return;
    lv_obj_clean(s_ui.body);
    clear_form_refs();
    s_ui.credential = NULL;

    if (!s_ui.backend_set || !s_ui.config.unlocked) {
        if (s_ui.step_label) lv_label_set_text(s_ui.step_label, "Commissioning · LOCKED");
        render_locked();
        return;
    }

    char progress[80];
    snprintf(progress, sizeof(progress), "Commissioning · Step %u/%u · %s",
             (unsigned)(s_ui.step + 1U), (unsigned)COMMISSION_STEP_COUNT, s_step_names[s_ui.step]);
    lv_label_set_text(s_ui.step_label, progress);
    render_step();
}

lv_obj_t *commissioning_screen_create(lv_obj_t *parent)
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
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_ui.step_label = lv_label_create(top);
    lv_obj_set_width(s_ui.step_label, 420);
    lv_obj_set_style_text_color(s_ui.step_label, lv_color_hex(0xF2F6FA), LV_PART_MAIN);
    button(top, "Back", back_clicked, NULL);
    button(top, "Next", next_clicked, NULL);
    button(top, "Lock", lock_clicked, NULL);

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
    lv_obj_set_size(s_ui.keyboard, LV_PCT(100), KEYBOARD_HEIGHT);
    lv_obj_align(s_ui.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_ui.keyboard, keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_ui.keyboard, keyboard_event, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);

    render();
    return s_ui.root;
}

void commissioning_screen_set_backend(const screen_commissioning_backend_t *backend)
{
    if (!backend) {
        memset(&s_ui.backend, 0, sizeof(s_ui.backend));
        s_ui.backend_set = false;
        memset(&s_ui.config, 0, sizeof(s_ui.config));
    } else {
        s_ui.backend = *backend;
        s_ui.backend_set = true;
    }
    render();
}

void commissioning_screen_apply_gate(const screen_commissioning_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_ui.gate = *snapshot;
    else memset(&s_ui.gate, 0, sizeof(s_ui.gate));
    if (s_ui.root && s_ui.config.unlocked && s_ui.step == 7U) render();
}

void commissioning_screen_apply_status(const screen_status_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_ui.status = *snapshot;
    else memset(&s_ui.status, 0, sizeof(s_ui.status));
}

void commissioning_screen_apply_meters(const screen_meters_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_ui.meters = *snapshot;
    else memset(&s_ui.meters, 0, sizeof(s_ui.meters));
}

void commissioning_screen_apply_inverters(const screen_inverters_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_ui.inverters = *snapshot;
    else memset(&s_ui.inverters, 0, sizeof(s_ui.inverters));
}

void commissioning_screen_apply_telemetry(const screen_telemetry_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_ui.telemetry = *snapshot;
    else memset(&s_ui.telemetry, 0, sizeof(s_ui.telemetry));
}

void commissioning_screen_show_unavailable(void)
{
    memset(&s_ui.gate, 0, sizeof(s_ui.gate));
    memset(&s_ui.status, 0, sizeof(s_ui.status));
    memset(&s_ui.meters, 0, sizeof(s_ui.meters));
    memset(&s_ui.inverters, 0, sizeof(s_ui.inverters));
    memset(&s_ui.telemetry, 0, sizeof(s_ui.telemetry));
}
