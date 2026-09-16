# Gate A acceptance

Gate A is accepted only when:

- branch ancestry starts from current `dev` (`25d2f9da5cd8291a701ef4b63af05224a43fd775` at branch creation),
- the qualified display-driver blobs match donor tree `694da42045d9ce377536f807f205640c6bfbc9a6`,
- the product target embeds no Wi-Fi credentials,
- no legacy board UI/commissioning backend is compiled,
- current `dev` Product Core remains the control/safety authority,
- the isolated target configures successfully and compiles under ESP-IDF 6.0.x.

Gate B adds the new `pvdg_ui_native` component and current-dev host bindings.
