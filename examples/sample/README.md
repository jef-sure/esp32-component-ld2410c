# LD2410C Sample

Demonstrates basic usage of the LD2410C radar driver: reading firmware version,
configuring detection parameters, and continuously monitoring presence via
UART data frames and the OUT pin.

## Wiring

| ESP32 Pin | LD2410C Pin |
|-----------|-------------|
| GPIO 17   | RX          |
| GPIO 16   | TX          |
| GPIO 4    | OUT         |
| 3.3V      | VCC         |
| GND       | GND         |

## Build and Flash

```bash
idf.py build flash monitor
```

## What It Does

1. Prints the module's firmware version.
2. Reads and logs the current configuration (gate sensitivities, resolution).
3. Configures detection: 6 gates, 10 s no-one timeout, sensitivity 40/30.
4. Starts a FreeRTOS task that continuously reads data frames, parses target
   data, and logs state changes and distances.
5. Monitors the OUT pin via GPIO interrupt for presence/no-presence edges.
