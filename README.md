# ESP-IDF LD2410C Driver

ESP-IDF driver component for the HLK-LD2410C human presence sensing radar module.

## Features

- Full implementation of LD2410C serial communication protocol V1.07
- Low-level command functions with manual configuration mode control
- High-level convenience wrappers for common operations
- Data frame parsers for target detection and engineering data
- Support for all distance gates (0-8)
- Configurable sensitivity for moving and stationary targets
- Background noise detection capability
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
ld2410c_handle_t *ld = ld2410c_init(UART_NUM_1);

// Read basic target data
ld2410c_basic_target_data_t target_data;
if (ld2410c_read_target_data(ld, &target_data) == ESP_OK) {
    ESP_LOGI(TAG, "Target: %d, Distance: %d cm", 
             target_data.target_state, target_data.moving_target_distance);
}

// Configure sensitivity (requires enable_config/end_config)
ld2410c_enable_config(ld);
ld2410c_set_gate_sensitivity(ld, 0, 50, 50); // Gate 0, mov=50, stat=50
ld2410c_end_config(ld);

// Cleanup
ld2410c_deinit(ld);
```

See the [example](examples/sample) for a complete working demonstration.

## API Documentation

The driver provides three levels of API:

### Low-Level Functions
Require manual `ld2410c_enable_config()` and `ld2410c_end_config()` calls:
- `ld2410c_enable_config()` - Enter configuration mode
- `ld2410c_end_config()` - Exit configuration mode
- `ld2410c_set_gate_sensitivity()` - Configure gate sensitivity
- `ld2410c_set_max_distances()` - Set detection distances
- And more...

### High-Level Wrappers
Automatically handle configuration mode:
- `ld2410c_set_gate_sensitivity_hl()` - Configure with auto config mode
- `ld2410c_set_max_distances_hl()` - Set distances with auto config mode
- And more...

### Data Frame Parsers
- `ld2410c_read_target_data()` - Read basic target detection data
- `ld2410c_read_engineering_data()` - Read detailed engineering data

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
