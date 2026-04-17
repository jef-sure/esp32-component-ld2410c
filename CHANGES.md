# Changelog

## 0.0.2 — 2026-04-17

- Added `ld2410c_deinit()` to free the driver handle and NULL the caller's pointer.
- Added `ld2410c_read_data_frame()` to read a raw data frame from UART.
- Added bounds check in `build_frame()` to prevent buffer overflow when value exceeds `MAX_FRAME_SIZE`.
- Added validation in `recv_ack()` to guard against out-of-bounds read from malformed UART data.
- Removed unused `CMD_BT_PASSWORD` enum value.

## 0.0.1 — 2026-04-17

- Initial release.
- UART command interface for HLK-LD2410C radar module (protocol V1.07).
- Low-level commands: enable/end config, set/read parameters, gate sensitivity,
  engineering mode, firmware version, baud rate, factory reset, restart,
  Bluetooth control, MAC address, distance resolution, auxiliary control,
  noise detection.
- Data frame parsers: `ld2410c_parse_target_data()`, `ld2410c_parse_engineering_data()`.
- High-level convenience functions: `ld2410c_configure_detection()`,
  `ld2410c_get_firmware_string()`, `ld2410c_get_full_config()`,
  `ld2410c_factory_reset_and_restart()`, `ld2410c_auto_calibrate()`.
