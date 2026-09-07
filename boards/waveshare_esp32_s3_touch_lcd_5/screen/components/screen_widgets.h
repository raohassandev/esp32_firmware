#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------
 * Design tokens -- the industrial-UI palette/scale, defined once.
 *
 * Before this, every page hardcoded its own hex colors and pixel spacing,
 * so there was no single place to see or change "what this app looks
 * like", and no consistent visual hierarchy (one font size everywhere,
 * no accent color, no active/inactive distinction anywhere). These tokens
 * are that single place. Pages should use them (or the helpers below that
 * apply them) instead of writing a new lv_color_hex(...) literal.
 * ------------------------------------------------------------------------ */

/* Layered dark surfaces: app background is the darkest, each layer on top
 * of it gets progressively lighter so panels-on-panels (e.g. a card inside
 * a page) read as physically stacked, not just outlined boxes. */
#define SCREEN_COLOR_BG_APP        0x0A0E14u
#define SCREEN_COLOR_SURFACE       0x141B24u
#define SCREEN_COLOR_SURFACE_RAISED 0x1C2530u
#define SCREEN_COLOR_BORDER        0x2A3644u

/* Text. */
#define SCREEN_COLOR_TEXT_PRIMARY   0xF5F8FAu
#define SCREEN_COLOR_TEXT_SECONDARY 0x8B98A8u

/* One brand/accent color, used for active state, primary emphasis and
 * focus -- distinct from the status colors below, which mean "healthy/
 * degraded/faulted" and must never be reused for "this is selected". */
#define SCREEN_COLOR_ACCENT    0x2FB8C6u
#define SCREEN_COLOR_ACCENT_ON 0x06171Au /* text/icon color drawn on top of an accent fill */

/* Status semantics -- kept close to the original palette's hues so this is
 * a refinement, not a re-skin the operator has to relearn. */
#define SCREEN_COLOR_SUCCESS 0x34D399u
#define SCREEN_COLOR_WARNING 0xFBBF24u
#define SCREEN_COLOR_DANGER  0xF87171u

/* Spacing scale (px). Use these instead of an arbitrary number so two
 * pages that are "the same kind of gap" actually use the same gap. */
#define SCREEN_SPACE_XS 4
#define SCREEN_SPACE_SM 8
#define SCREEN_SPACE_MD 12
#define SCREEN_SPACE_LG 16
#define SCREEN_SPACE_XL 24

#define SCREEN_RADIUS_SM 6
#define SCREEN_RADIUS_MD 10

/* Larger weights for headings/hero values -- see sdkconfig.defaults,
 * CONFIG_LV_FONT_MONTSERRAT_20/24 (declared by LVGL itself in
 * src/font/lv_font.h once those options are on; not redeclared here). The
 * app previously shipped only size 14, so a page title and its body text
 * were visually identical. */
#define SCREEN_FONT_BODY (&lv_font_montserrat_14)
#define SCREEN_FONT_TITLE (&lv_font_montserrat_20)
#define SCREEN_FONT_HERO (&lv_font_montserrat_24)

lv_obj_t *screen_ui_panel(lv_obj_t *parent);
/* Same as screen_ui_panel() but on the next surface layer up with a subtle
 * accent-tinted top border, for content that should read as "raised" above
 * its parent panel -- e.g. a hero metric card sitting inside a page. */
lv_obj_t *screen_ui_card(lv_obj_t *parent);
lv_obj_t *screen_ui_title(lv_obj_t *parent, const char *text);
lv_obj_t *screen_ui_muted_label(lv_obj_t *parent, const char *text);
lv_obj_t *screen_ui_value_label(lv_obj_t *parent, const char *text);
lv_obj_t *screen_ui_row(lv_obj_t *parent, const char *name, lv_obj_t **value_out);

/* A small rounded, filled badge for a state word ("Online", "Fault",
 * "Standby") -- replaces coloring the text itself, which reads as an
 * error state on a dark background more often than it reads as emphasis. */
lv_obj_t *screen_ui_badge(lv_obj_t *parent, const char *text, bool healthy);
void screen_ui_set_badge(lv_obj_t *badge, const char *text, bool healthy);

/* Live pages should not invalidate an LVGL label when its visible text did not
 * change.  The helpers below keep that policy in one place so steady refreshes
 * do not create avoidable layout/draw work on the RGB panel. */
bool screen_ui_set_text_if_changed(lv_obj_t *label, const char *text);
bool screen_ui_set_text_fmt_if_changed(lv_obj_t *label, const char *format, ...);

void screen_ui_set_kw(lv_obj_t *label, bool available, double value);
void screen_ui_set_state_text(lv_obj_t *label, const char *text, bool healthy);
const char *screen_ui_safe_text(const char *text, const char *fallback);

/* Shared Wi-Fi signal indicator so the persistent nav-bar indicator and the
 * Network settings page render identically for the same online/rssi state --
 * one scale, defined once. Sets both the icon+dBm text and the strength
 * color on the label in a single call; "online" false always renders the
 * offline state regardless of a stale/last-known rssi value. Uses LVGL's
 * built-in LV_SYMBOL_WIFI glyph (part of the default compiled-in font's
 * symbol range) rather than raw Unicode block characters or plain ASCII --
 * a real icon, and one guaranteed to actually be present in the font. */
void screen_ui_apply_wifi_indicator(lv_obj_t *label, bool online, int rssi);

#ifdef __cplusplus
}
#endif
