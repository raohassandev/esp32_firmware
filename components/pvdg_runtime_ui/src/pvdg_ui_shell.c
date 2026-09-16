#include "pvdg_ui_shell.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pvdg_ui_theme.h"

typedef struct { lv_obj_t *root,*content,*network_badge,*alarm_badge,*time_label,*date_label,*nav[6]; pvdg_ui_shell_callbacks_t callbacks; } shell_ui_t;
static shell_ui_t s;
static const pvdg_ui_route_t routes[]={PVDG_UI_ROUTE_OVERVIEW,PVDG_UI_ROUTE_GRID,PVDG_UI_ROUTE_SOLAR,PVDG_UI_ROUTE_GENERATOR,PVDG_UI_ROUTE_LOAD,PVDG_UI_ROUTE_REPORTS};
static const char *names[]={"Overview","Grid","Solar","Generator","Load","Reports"};

static lv_obj_t *lbl(lv_obj_t *p,const char *t,uint32_t c,const lv_font_t *f){ lv_obj_t *o=lv_label_create(p); lv_label_set_text(o,t?t:""); pvdg_ui_style_label(o,lv_color_hex(c),f); return o; }
static void route_event(lv_event_t *e){ if(s.callbacks.on_route) s.callbacks.on_route((pvdg_ui_route_t)(intptr_t)lv_event_get_user_data(e),s.callbacks.user); }
static lv_obj_t *top_button(lv_obj_t *p,const char *symbol,pvdg_ui_route_t r){ lv_obj_t *b=lv_button_create(p); lv_obj_set_size(b,PVDG_UI_TOUCH_MIN,PVDG_UI_TOUCH_MIN); lv_obj_set_style_bg_color(b,lv_color_hex(PVDG_UI_COLOR_SURFACE),LV_PART_MAIN); lv_obj_set_style_border_width(b,1,LV_PART_MAIN); lv_obj_set_style_border_color(b,lv_color_hex(PVDG_UI_COLOR_BORDER),LV_PART_MAIN); lv_obj_set_style_radius(b,PVDG_UI_RADIUS,LV_PART_MAIN); lv_obj_t *i=lbl(b,symbol,PVDG_UI_COLOR_TEXT,PVDG_UI_FONT_TITLE); lv_obj_center(i); lv_obj_add_event_cb(b,route_event,LV_EVENT_CLICKED,(void *)(intptr_t)r); return b; }
static lv_obj_t *nav_button(lv_obj_t *p,const char *name,pvdg_ui_route_t r){ lv_obj_t *b=lv_button_create(p); lv_obj_set_width(b,LV_PCT(100)); lv_obj_set_height(b,55); lv_obj_set_style_bg_color(b,lv_color_hex(PVDG_UI_COLOR_SIDEBAR),LV_PART_MAIN); lv_obj_set_style_border_width(b,0,LV_PART_MAIN); lv_obj_set_style_radius(b,PVDG_UI_RADIUS,LV_PART_MAIN); lv_obj_set_style_pad_all(b,4,LV_PART_MAIN); lv_obj_t *t=lbl(b,name,PVDG_UI_COLOR_MUTED,PVDG_UI_FONT_BODY); lv_obj_center(t); lv_obj_add_event_cb(b,route_event,LV_EVENT_CLICKED,(void *)(intptr_t)r); return b; }

pvdg_ui_shell_t pvdg_ui_shell_create(lv_obj_t *parent,const pvdg_ui_shell_callbacks_t *callbacks){
    memset(&s,0,sizeof(s)); if(callbacks)s.callbacks=*callbacks; pvdg_ui_theme_init();
    s.root=lv_obj_create(parent); pvdg_ui_style_root(s.root); lv_obj_set_size(s.root,800,480); lv_obj_remove_flag(s.root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *top=lv_obj_create(s.root); lv_obj_set_pos(top,0,0); lv_obj_set_size(top,800,PVDG_UI_TOPBAR_H); lv_obj_remove_flag(top,LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_bg_color(top,lv_color_hex(PVDG_UI_COLOR_TOPBAR),LV_PART_MAIN); lv_obj_set_style_bg_opa(top,LV_OPA_COVER,LV_PART_MAIN); lv_obj_set_style_border_width(top,0,LV_PART_MAIN); lv_obj_set_style_pad_left(top,12,LV_PART_MAIN); lv_obj_set_style_pad_right(top,8,LV_PART_MAIN); lv_obj_set_style_pad_top(top,0,LV_PART_MAIN); lv_obj_set_style_pad_bottom(top,0,LV_PART_MAIN);
    lv_obj_t *product=lbl(top,"PV-DG Energy Controller",PVDG_UI_COLOR_TEXT,PVDG_UI_FONT_TITLE); lv_obj_align(product,LV_ALIGN_LEFT_MID,0,0);
    s.network_badge=pvdg_ui_make_badge(top,"Offline",lv_color_hex(PVDG_UI_COLOR_DANGER)); lv_obj_align(s.network_badge,LV_ALIGN_LEFT_MID,230,0);
    lv_obj_t *wifi=top_button(top,LV_SYMBOL_WIFI,PVDG_UI_ROUTE_WIFI); lv_obj_align(wifi,LV_ALIGN_RIGHT_MID,-200,0);
    lv_obj_t *clock=lv_obj_create(top); pvdg_ui_style_root(clock); lv_obj_set_size(clock,105,40); lv_obj_set_layout(clock,LV_LAYOUT_FLEX); lv_obj_set_flex_flow(clock,LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(clock,0,LV_PART_MAIN); s.time_label=lbl(clock,"--:--",PVDG_UI_COLOR_TEXT,PVDG_UI_FONT_BODY); s.date_label=lbl(clock,"-- --- ----",PVDG_UI_COLOR_MUTED,PVDG_UI_FONT_BODY); lv_obj_align(clock,LV_ALIGN_RIGHT_MID,-86,0);
    s.alarm_badge=pvdg_ui_make_badge(top,"0",lv_color_hex(PVDG_UI_COLOR_SUCCESS)); lv_obj_align(s.alarm_badge,LV_ALIGN_RIGHT_MID,-48,0);
    lv_obj_t *gear=top_button(top,LV_SYMBOL_SETTINGS,PVDG_UI_ROUTE_ENGINEERING); lv_obj_align(gear,LV_ALIGN_RIGHT_MID,0,0);
    lv_obj_t *sidebar=lv_obj_create(s.root); lv_obj_set_pos(sidebar,0,PVDG_UI_TOPBAR_H); lv_obj_set_size(sidebar,PVDG_UI_SIDEBAR_W,480-PVDG_UI_TOPBAR_H); lv_obj_set_style_bg_color(sidebar,lv_color_hex(PVDG_UI_COLOR_SIDEBAR),LV_PART_MAIN); lv_obj_set_style_bg_opa(sidebar,LV_OPA_COVER,LV_PART_MAIN); lv_obj_set_style_border_width(sidebar,0,LV_PART_MAIN); lv_obj_set_style_pad_all(sidebar,5,LV_PART_MAIN); lv_obj_set_layout(sidebar,LV_LAYOUT_FLEX); lv_obj_set_flex_flow(sidebar,LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(sidebar,5,LV_PART_MAIN); lv_obj_remove_flag(sidebar,LV_OBJ_FLAG_SCROLLABLE);
    for(size_t i=0;i<6;i++)s.nav[i]=nav_button(sidebar,names[i],routes[i]);
    s.content=lv_obj_create(s.root); pvdg_ui_style_root(s.content); lv_obj_set_pos(s.content,PVDG_UI_SIDEBAR_W,PVDG_UI_TOPBAR_H); lv_obj_set_size(s.content,800-PVDG_UI_SIDEBAR_W,480-PVDG_UI_TOPBAR_H); lv_obj_remove_flag(s.content,LV_OBJ_FLAG_SCROLLABLE);
    pvdg_ui_shell_set_active_route(PVDG_UI_ROUTE_OVERVIEW); return (pvdg_ui_shell_t){.root=s.root,.content=s.content};
}
void pvdg_ui_shell_apply_model(const pvdg_ui_model_t *m){ if(!s.root)return; if(!m){pvdg_ui_badge_set(s.network_badge,"Offline",lv_color_hex(PVDG_UI_COLOR_DANGER));pvdg_ui_badge_set(s.alarm_badge,"--",lv_color_hex(PVDG_UI_COLOR_INACTIVE));return;} pvdg_ui_badge_set(s.network_badge,m->network.online?"Online":"Offline",lv_color_hex(m->network.online?PVDG_UI_COLOR_SUCCESS:PVDG_UI_COLOR_DANGER)); char a[16]; snprintf(a,sizeof(a),"%u",(unsigned)m->alarm_count); pvdg_ui_badge_set(s.alarm_badge,a,lv_color_hex(m->alarm_count?PVDG_UI_COLOR_DANGER:PVDG_UI_COLOR_SUCCESS)); }
void pvdg_ui_shell_set_clock(const char *t,const char *d){ if(!s.root)return; pvdg_ui_label_set_if_changed(s.time_label,t?t:"--:--"); pvdg_ui_label_set_if_changed(s.date_label,d?d:"-- --- ----"); }
void pvdg_ui_shell_set_active_route(pvdg_ui_route_t r){ if(!s.root)return; for(size_t i=0;i<6;i++){ bool active=routes[i]==r; lv_obj_set_style_bg_color(s.nav[i],lv_color_hex(active?PVDG_UI_COLOR_SURFACE_RAISED:PVDG_UI_COLOR_SIDEBAR),LV_PART_MAIN); lv_obj_set_style_border_width(s.nav[i],active?1:0,LV_PART_MAIN); lv_obj_set_style_border_color(s.nav[i],lv_color_hex(PVDG_UI_COLOR_GRID),LV_PART_MAIN); } }
