idf_component_register(
    SRCS
        "src/pvdg_ui_adapter.c"
        "src/pvdg_ui_diagnostics.c"
        "src/pvdg_ui_em500_history.c"
        "src/pvdg_ui_engineering.c"
        "src/pvdg_ui_export_control.c"
        "src/pvdg_ui_grid.c"
        "src/pvdg_ui_inverter_config.c"
        "src/pvdg_ui_inverter_setup.c"
        "src/pvdg_ui_meter_config.c"
        "src/pvdg_ui_meter_setup.c"
        "src/pvdg_ui_model.c"
        "src/pvdg_ui_overview.c"
        "src/pvdg_ui_pages.c"
        "src/pvdg_ui_restart_prompt.c"
        "src/pvdg_ui_shell.c"
        "src/pvdg_ui_snapshot.c"
        "src/pvdg_ui_solar.c"
        "src/pvdg_ui_source_config.c"
        "src/pvdg_ui_source_setup.c"
        "src/pvdg_ui_theme.c"
        "src/pvdg_ui_wifi.c"
        "src/pvdg_ui_wifi_config.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
)

# Vendored from PV-DG-UI-Screens feature/runtime-ui-v1 at
# 1513dca1c9a5458d0e04b9e799c1b59d59707e4c.
# Deliberately no direct dependency on control_engine, meter_manager,
# inverter_manager, network_manager, safety_manager or web_server. The product
# host owns those bindings and feeds this component through model/callback
# contracts.
