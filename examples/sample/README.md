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
| 5V (VIN)  | VCC         |
| GND       | GND         |

The module must be powered from 5 V with a supply able to deliver more than
200 mA; it does not work reliably from the 3.3 V rail. Its UART and OUT pins use
3.3 V logic levels, so they connect to the ESP32 GPIOs directly.

## Build and Flash

```bash
idf.py build flash monitor
```

The sample pulls the driver from the repository it lives in (`override_path`
in [main/idf_component.yml](main/idf_component.yml)). ESP-IDF names a local
component after its directory, so when building from a git checkout the
checkout directory must be named `ld2410c`:

```bash
git clone https://github.com/jef-sure/esp32-component-ld2410c.git ld2410c
cd ld2410c/examples/sample
```

Alternatively, create the project from the registry:

```bash
idf.py create-project-from-example "jef-sure/ld2410c:sample"
```

## What It Does

1. Prints the module's firmware version.
2. Reads and logs the current configuration (gate sensitivities, resolution).
3. Configures detection: max gate 6 (about 4.5 m), 10 s no-one timeout,
   sensitivity 40/30.
4. Starts a FreeRTOS task that continuously reads data frames, parses target
   data, and logs state changes and distances.
5. Monitors the OUT pin via GPIO interrupt for presence/no-presence edges.

## Engineering Mode

Set `EXAMPLE_ENGINEERING_MODE` to `1` in [main/main.c](main/main.c) to switch
the module to engineering mode. The monitor task then also logs, for every
report, the energy of each of the nine distance gates (moving and stationary),
the light sensor value and the OUT pin level, which helps when tuning the gate
sensitivities. The mode is lost on power cycle.
