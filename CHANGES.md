# Changelog

## 0.1.0 — 2026-10-05

### Behavior changes (read before upgrading from 0.0.x)

Code written against 0.0.x compiles unchanged, but the following calls behave
differently:

- **`ld2410c_read_data_frame()`**
  - The buffer now holds exactly one frame starting at `buf[0]`. Before, it
    held everything read so far: possibly garbage before the frame and extra
    bytes after it.
  - `out_len` is the length of that frame, and 0 on failure. Before, it was
    the total number of bytes read, also on failure.
  - Bytes following the frame stay in the UART driver for the next call
    instead of being returned and dropped.
  - The handle timeout bounds the whole call. Before, every internal UART read
    got the full timeout, so a call could block for several timeouts.
  - `buf_size` below 23 returns `ESP_ERR_INVALID_SIZE`; a frame larger than
    the buffer is skipped (engineering frames need 45 bytes).
- **`ld2410c_firmware_ver_t` / `ld2410c_read_firmware_version()`**: `major`
  and `minor` now hold the values that were previously in each other's place,
  and all fields are meant to be printed as hex. Code that formatted the
  version itself must switch from `%u` to `%X`.
- **Command functions return as soon as the ACK arrives.** Before, each
  command blocked for the full handle timeout. Code that relied on that delay
  as a pause between commands needs its own delay.
- **Argument validation.** Calls that used to be sent to the module (or crash)
  now return `ESP_ERR_INVALID_ARG` without touching the UART:
  - NULL handle or NULL output pointer;
  - `ld2410c_set_max_gate_and_duration()` / `ld2410c_configure_detection()`
    with a max gate outside 2-8 (notably 0 and 1 are rejected);
  - `ld2410c_set_gate_sensitivity()` with a gate other than 0-8 or 0xFFFF, or
    a sensitivity above 100;
  - unknown baud rate, resolution, light control mode or OUT level values;
  - `ld2410c_auto_calibrate()` with `poll_interval_ms == 0`.
- **`ld2410c_init()`** returns NULL for an invalid UART port or a timeout
  that is not positive.
- **Parsers are stricter.** `ld2410c_parse_target_data()` and
  `ld2410c_parse_engineering_data()` return `ESP_ERR_INVALID_RESPONSE` for a
  frame with a wrong `0xAA` head, `0x55` tail or `0x00` check byte, and
  `ESP_ERR_INVALID_SIZE` for a frame too short for its fields; such frames
  were previously parsed. Engineering frames reporting a max gate above 8 are
  rejected instead of being parsed with misaligned offsets.
- **`ld2410c_read_params()`** returns `ESP_ERR_INVALID_RESPONSE` when the
  module reports more than 9 gates.
- **A malformed ACK no longer fails fast.** An ACK with a bad tail is skipped
  and the command ends with `ESP_ERR_TIMEOUT` instead of
  `ESP_ERR_INVALID_RESPONSE`.
- **Minimum ESP-IDF version is declared as 5.3**, matching the
  `esp_driver_uart` / `esp_driver_gpio` components the build already required.

### Fixed

- Firmware version decoding: major/minor bytes were swapped and the build number
  was printed in decimal. The protocol example now yields `V1.07.22091516`
  (was `V7.01.571020566`). `ld2410c_firmware_ver_t` fields are BCD-style and
  must be printed as hex.
- `ld2410c_get_mac_address()` expected a 7-byte response and skipped the first
  byte; the module returns 6 bytes, so every valid response failed with
  `ESP_ERR_INVALID_SIZE`.
- `ld2410c_parse_engineering_data()` read target fields before checking the
  frame length (out-of-bounds read on short frames), and could take the
  photosensitive/OUT values from the frame's tail bytes.
- Parsers now verify the intra-frame `0xAA` head, `0x55` tail and `0x00` check
  byte and return `ESP_ERR_INVALID_RESPONSE` for corrupted frames.
- `ld2410c_auto_calibrate()` divided by zero for `poll_interval_ms == 0` and
  timed out immediately when the interval exceeded the overall timeout.
- ACK and data frame reads are bounded by the handle timeout as a whole.
  Previously every UART read got the full timeout, and each command waited out
  the full timeout even when the ACK had already arrived.
- A partial UART write is reported as `ESP_FAIL`.
- ACK frames shorter than command word + status are rejected.

### Added

- Host unit tests (`make -C test/host`) covering the protocol examples,
  malformed frames, fragmented UART input and parameter boundaries.
- Thread-safety documentation: the driver does no locking.

### Example and packaging

- The sample reads frames with `ld2410c_read_data_frame()` instead of parsing
  raw UART chunks, and checks queue, handle and task creation.
- The sample declares its dependency on the component, so it builds from the
  repository and from the registry.
- Wiring documentation: the module needs a 5 V supply (>200 mA), not 3.3 V.
- README installs `jef-sure/ld2410c` (the registry name) at `^0.1.0`.
- The package now includes the example's CMake, manifest and README files.

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
