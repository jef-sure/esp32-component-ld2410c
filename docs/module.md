# HLK-LD2410C module reference

Translated and condensed from the vendor user manual *HLK-LD2410C 人体存在感应模组
说明书*, V1.09 (2024-10-08), Shenzhen Hi-Link Electronic Co., Ltd. Only the parts
that matter when wiring, mounting and configuring the module are kept. The
serial protocol is in [protocol.md](protocol.md).

## What the module does

The LD2410C is a 24 GHz FMCW radar that senses human presence indoors. It
detects moving people and people who are still or only slightly moving (sitting,
lying), and reports the distance to the target. It reports through one GPIO
(OUT) and a UART; both the range and the sensitivity per distance interval are
configurable.

## Pins

| Pin | Symbol | Function |
| --- | --- | --- |
| 1 | UART_Tx | serial transmit |
| 2 | UART_Rx | serial receive |
| 3 | OUT | target state: high when a person is present, low when no one is |
| 4 | GND | ground |
| 5 | VCC | power input, 5 to 12 V (5 V recommended) |

The protocol document V1.09 numbers the same pins differently (OUT 1, UART_Tx 2,
UART_Rx 3); go by the marking on the board.

The board is 16 mm x 22 mm with five pin holes of 0.9 mm diameter at 2.54 mm
pitch.

## Electrical and performance data

| Parameter | Value |
| --- | --- |
| Frequency band | 24 GHz to 24.25 GHz |
| Modulation | FMCW, 250 MHz sweep bandwidth |
| Supply | DC 5 V, source able to deliver more than 200 mA |
| Average current | 79 mA |
| Interfaces | one GPIO (3.3 V level), one UART |
| UART default | 256000 baud, 1 stop bit, no parity |
| Detection distance | 0.75 m to 6 m, adjustable |
| Detection angle | ±60° |
| Distance resolution | 0.75 m |
| Ambient temperature | -40 to 85 °C |

## Configuration parameters

The parameters are set over the UART (or Bluetooth) and are kept when power is
removed.

**Farthest detection distance.** Only targets within this distance are detected
and reported. It is set in distance gates of 0.75 m each, separately for moving
and for stationary detection. For example, with the farthest gate set to 2 only
a person within 1.5 m is detected. The manual gives the range as 1 to 8; the
protocol document gives 2 to 8 for the command, which is what the driver
accepts.

**Sensitivity.** A target counts as present when its energy (0 to 100) is above
the sensitivity value (0 to 100). Every gate has its own sensitivity, so
detection can be tuned per distance interval, for example to filter out a source
of interference in one place. A sensitivity of 100 switches a gate off. For
example, with gates 3 and 4 at 20 and all others at 100, only people between
2.25 m and 3.75 m from the module are detected.

**No-one duration.** When the result changes from "someone" to "no one", the
module keeps reporting "someone" for this many seconds. If nobody is detected
during that time it reports "no one"; if someone is detected, the time starts
again. It is the delay between a person leaving and the state changing.

The vendor's PC tool can read and write the parameters only while its data
display is stopped.

## Mounting

- Ceiling mounting: the manual shows the detection range for a height of 3 m.
- Wall mounting: height 1.5 to 2 m, range up to 6 m.

If the radar is behind a cover, the cover must pass 24 GHz well and must not
contain metal or other material that shields electromagnetic waves.

Detection is degraded when:

- something other than a person keeps moving in the sensed area: animals,
  curtains that sway, large plants in front of an air outlet;
- a large, strongly reflecting surface faces the antenna;
- on a wall mount, an air conditioner or a fan on the ceiling is in view.

When installing:

- point the antenna at the area to cover and keep its surroundings clear;
- fix the sensor firmly: movement of the radar itself affects detection;
- make sure nothing moves or vibrates behind the radar. Radar waves pass through
  material, and the back lobe of the antenna can pick up movement behind the
  module. A metal shield or back plate reduces this;
- expect the reported distance and the farthest distance to vary somewhat with
  the size, posture and reflectivity of the target. The distance accuracy is
  computed on top of a physical resolution of 0.75 m.

### Cover (radome) guidelines

- Height from the antenna to the inner surface of the cover: 1 or 1.5
  wavelengths where space allows, that is 12.4 mm or 18.6 mm at 24.125 GHz,
  within ±1.2 mm.
- Cover thickness: half a wavelength in the material, within ±20 %. If that is
  not possible, use a material with a low dielectric constant and a thickness of
  1/8 wavelength or less.
- Inhomogeneous or layered materials need to be tried out.

## Bluetooth

- Bluetooth is on by default. The app is *HLKRadarTools* (Android and iOS); the
  module advertises as `HLK-LD2410_xxxx`, where `xxxx` is the last four digits
  of the MAC address. Keep the phone within about 4 m.
- The first connection asks for a password. The default is `HiLink`; it is
  always 6 bytes and can be changed in the app or with the serial command.
  Passwords and OTA updates need firmware V1.07.22091516 or newer.
- The module is the peripheral and accepts one connection. After the password
  check it forwards the radar data over Bluetooth in the serial protocol format.

| Characteristic UUID | Access | Direction |
| --- | --- | --- |
| `0000fff1-0000-1000-8000-00805f9b34fb` | Read / Notify | module to app |
| `0000fff2-0000-1000-8000-00805f9b34fb` | Write Without Response | app to module |

- Bluetooth can be switched off and on with the serial command. If it is off and
  the serial port cannot be used, switching the module's power off and on more
  than five times in a row within 2 to 3 seconds switches Bluetooth on again.
- An OTA update takes 1 to 3 minutes and must be done within 3 m of the module.
  Do not cut the power, restart the module or close the app during it: after a
  failed update the radar function does not work until an update succeeds. To
  retry, restart the module and reconnect; the app lists it as waiting for an
  update.
