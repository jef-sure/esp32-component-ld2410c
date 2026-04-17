# ESP-IDF LD2410C Driver

ESP-IDF driver component for the HLK-LD2410C human presence sensing radar module.

## Features

- Full implementation of LD2410C serial communication protocol V1.07
- Low-level command functions with manual configuration mode control
- High-level convenience wrappers for common operations
- Data frame reader and parsers for target detection and engineering data
- Support for all distance gates (0-8)
- Configurable sensitivity for moving and stationary targets
- Background noise detection and auto-calibration
- Light-sensing auxiliary control for OUT pin
- Multiple baud rate support (9600 - 460800)
- Two distance resolution modes (0.75m and 0.20m)

## Hardware Specifications

- **Default UART Settings**: 256000 baud, 8N1, no flow control
- **Interface**: UART communication
- **Detection Range**: Up to 6 meters (configurable via distance gates)
- **OUT Pin**: Configurable presence detection signal

## Installation

Add this component to your ESP-IDF project using the IDF Component Manager:

```bash
idf.py add-dependency "jef-sure/esp32-component-ld2410c"
```

Or manually add to your project's `idf_component.yml`:

```yaml
dependencies:
  jef-sure/esp32-component-ld2410c: "^0.0.1"
```

## Usage Example

```c
#include "ld2410c.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "example";

// Initialize UART
uart_config_t uart_config = {
    .baud_rate = 256000,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
};

ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 256, 0, 0, NULL, 0));
ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, TX_PIN, RX_PIN,
                              UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

// Initialize LD2410C handle
ld2410c_handle_t *ld = ld2410c_init(UART_NUM_1, 1000);

// Read and parse target data
uint8_t buf[64];
size_t len;
if (ld2410c_read_data_frame(ld, buf, sizeof(buf), &len) == ESP_OK) {
    ld2410c_target_data_t target;
    if (ld2410c_parse_target_data(buf, len, &target) == ESP_OK) {
        ESP_LOGI(TAG, "State: %d, Moving: %d cm, Stationary: %d cm",
                 target.state, target.moving_distance_cm,
                 target.stationary_distance_cm);
    }
}

// Configure sensitivity (requires enable_config/end_config)
ld2410c_enable_config(ld);
ld2410c_set_gate_sensitivity(ld, 0, 50, 50); // Gate 0, mov=50, stat=50
ld2410c_end_config(ld);

// Or use the high-level wrapper
ld2410c_configure_detection(ld, 8, 8, 5, 50, 50);

// Cleanup
ld2410c_deinit(&ld);
```

See the [example](examples/sample) for a complete working demonstration.

## API Documentation

The driver provides three levels of API:

### Lifecycle
- `ld2410c_init()` - Allocate and initialize a driver handle
- `ld2410c_deinit()` - Free the handle and NULL the pointer

### Low-Level Commands
Require manual `ld2410c_enable_config()` / `ld2410c_end_config()` wrapping:
- `ld2410c_set_max_gate_and_duration()` - Set detection range and no-one timeout
- `ld2410c_read_params()` - Read current configuration parameters
- `ld2410c_set_gate_sensitivity()` - Configure per-gate sensitivity
- `ld2410c_set_all_gate_sensitivity()` - Uniform sensitivity for all gates
- `ld2410c_enable_engineering_mode()` / `ld2410c_disable_engineering_mode()`
- `ld2410c_read_firmware_version()` - Read firmware version
- `ld2410c_set_baud_rate()` - Change UART baud rate
- `ld2410c_factory_reset()` / `ld2410c_restart()`
- `ld2410c_set_bluetooth()` / `ld2410c_get_mac_address()` / `ld2410c_set_bluetooth_password()`
- `ld2410c_set_distance_resolution()` / `ld2410c_get_distance_resolution()`
- `ld2410c_set_aux_control()` / `ld2410c_get_aux_control()`
- `ld2410c_start_noise_detection()` / `ld2410c_query_noise_detection_status()`

### High-Level Wrappers
Automatically handle configuration mode:
- `ld2410c_configure_detection()` - Set range, timeout, and sensitivity in one call
- `ld2410c_get_firmware_string()` - Read firmware version as a formatted string
- `ld2410c_get_full_config()` - Read all parameters and resolution
- `ld2410c_factory_reset_and_restart()` - Reset and restart in one call
- `ld2410c_auto_calibrate()` - Run noise calibration and block until complete

### Data Frame Reader & Parsers
- `ld2410c_read_data_frame()` - Read a raw data frame from UART
- `ld2410c_parse_target_data()` - Parse basic target detection data
- `ld2410c_parse_engineering_data()` - Parse detailed engineering mode data

## Requirements

- ESP-IDF v5.0 or higher
- UART peripheral
- GPIO for OUT pin monitoring (optional)

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Author

Anton Petrusevich <anton.petrusevich.mobile@gmail.com>

## References

- [HLK-LD2410C Product Page](http://www.hlktech.net/index.php?id=988)
- Protocol Version: V1.07
