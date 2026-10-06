# HLK-LD2410C serial protocol reference

Translated and condensed from the vendor document *HLK-LD2410C 人体存在感应模组
串口通信协议*, V1.09 (2025-06-09), Shenzhen Hi-Link Electronic Co., Ltd.
([PDF](https://r0.hlktech.com/download/HLK-LD2410C-24G/1/LD2410C%20%E4%B8%B2%E5%8F%A3%E9%80%9A%E4%BF%A1%E5%8D%8F%E8%AE%AE%20V1.09.pdf)).
The Chinese original is the reference: the English edition V1.07 (2024-08-05)
has mistakes in its examples that the original no longer has. All byte values
and examples are the vendor's. Mistakes that remain in V1.09 are listed in
[Errata](#errata-in-the-vendor-document), the differences from the English
V1.07 in [Changes since V1.07](#changes-since-the-english-v107).

Everything the module sends over UART is also sent over Bluetooth after the
password check, in the same format.

## Serial port

- TTL level, 3.3 V.
- Default 256000 baud, 8 data bits, 1 stop bit, no parity.
- Multi-byte values are little-endian. All bytes below are hexadecimal.

## Command and ACK frames

| Header | Data length | Data | Tail |
| --- | --- | --- | --- |
| `FD FC FB FA` | 2 bytes | see below | `04 03 02 01` |

- Command data: command word (2 bytes) + command value (N bytes).
- ACK data: command word OR `0x0100` (2 bytes) + return value (N bytes). The
  return value starts with a 2-byte status: 0 = success, 1 = failure.

A command is only accepted between "enable configuration" and "end
configuration". The sequence is: send enable configuration, wait for its ACK,
send the commands one at a time waiting for each ACK, send end configuration,
wait for its ACK. No ACK, or an ACK with status 1, means the command was not
executed.

## Commands

| Word | Command | Value | Return value after the status | Kept after power-off | Needs restart |
| --- | --- | --- | --- | --- | --- |
| `0x00FF` | Enable configuration | `0x0001` | protocol version (2, `0x0001`) + buffer size (2, `0x0040`) | | |
| `0x00FE` | End configuration | none | | | |
| `0x0060` | Set max gates and no-one duration | 18 bytes, see below | | yes | |
| `0x0061` | Read parameters | none | see below | | |
| `0x0062` | Enable engineering mode | none | | no | |
| `0x0063` | Disable engineering mode | none | | | |
| `0x0064` | Set gate sensitivity | 18 bytes, see below | | yes | |
| `0x00A0` | Read firmware version | none | type (2) + version (2) + build (4) | | |
| `0x00A1` | Set baud rate | index (2) | | yes | yes |
| `0x00A2` | Restore factory settings | none | | | yes |
| `0x00A3` | Restart | none | | | |
| `0x00A4` | Bluetooth on/off | `01 00` on, `00 00` off | | yes | yes |
| `0x00A5` | Get MAC address | `0x0001` | 6 bytes, big-endian | | |
| `0x00A8` | Get Bluetooth permission | password (6) | answered over Bluetooth only | | |
| `0x00A9` | Set Bluetooth password | password (6) | | yes | yes |
| `0x00AA` | Set distance resolution | index (2) | | yes | yes |
| `0x00AB` | Get distance resolution | none | index (2) | | |
| `0x00AD` | Set auxiliary control | 4 bytes, see below | | | |
| `0x00AE` | Get auxiliary control | none | 4 bytes, same layout | | |
| `0x000B` | Start background noise detection | duration in seconds (2) | | | |
| `0x001B` | Query noise detection status | none | status (2) | | |

An empty cell in the last two columns means the vendor document does not say.

### 0x00FF Enable configuration

```
send  FD FC FB FA  04 00  FF 00  01 00  04 03 02 01
ack   FD FC FB FA  08 00  FF 01  00 00  01 00  40 00  04 03 02 01
```

### 0x00FE End configuration

The module returns to working mode.

```
send  FD FC FB FA  02 00  FE 00  04 03 02 01
ack   FD FC FB FA  04 00  FE 01  00 00  04 03 02 01
```

### 0x0060 Set max gates and no-one duration

Sets the farthest gate for moving and for stationary detection (2 to 8) and the
no-one duration (0 to 65535 seconds).

The value is three pairs of a 2-byte parameter word and a 4-byte parameter:

| Parameter word | Parameter |
| --- | --- |
| `0x0000` | max moving distance gate |
| `0x0001` | max stationary distance gate |
| `0x0002` | no-one duration, seconds |

Example: both max gates 8, duration 5 s.

```
send  FD FC FB FA  14 00  60 00
      00 00  08 00 00 00   01 00  08 00 00 00   02 00  05 00 00 00
      04 03 02 01
ack   FD FC FB FA  04 00  60 01  00 00  04 03 02 01
```

### 0x0061 Read parameters

Return value after the status:

| Bytes | Content |
| --- | --- |
| 1 | head `0xAA` |
| 1 | max distance gate N (`0x08`) |
| 1 | configured max moving gate |
| 1 | configured max stationary gate |
| N + 1 | moving sensitivity of gates 0 to N |
| N + 1 | stationary sensitivity of gates 0 to N |
| 2 | no-one duration, seconds |

Example: N = 8, both configured max gates 8, moving sensitivity 20 (`0x14`) and
stationary sensitivity 25 (`0x19`) for all gates, duration 5 s.

```
send  FD FC FB FA  02 00  61 00  04 03 02 01
ack   FD FC FB FA  1C 00  61 01  00 00
      AA  08  08  08
      14 14 14 14 14 14 14 14 14
      19 19 19 19 19 19 19 19 19
      05 00
      04 03 02 01
```

### 0x0062 / 0x0063 Engineering mode

In engineering mode every report also carries the energy of each distance gate
(see [Engineering mode data](#engineering-mode-data)). The mode is off after
power-up.

```
send  FD FC FB FA  02 00  62 00  04 03 02 01      (0x0063 to disable)
ack   FD FC FB FA  04 00  62 01  00 00  04 03 02 01
```

### 0x0064 Set gate sensitivity

Sets the moving and stationary sensitivity (0 to 100) of one gate, or of all
gates at once with gate value `0xFFFF`.

| Parameter word | Parameter |
| --- | --- |
| `0x0000` | distance gate, or `0xFFFF` for all |
| `0x0001` | moving sensitivity |
| `0x0002` | stationary sensitivity |

Example: gate 3, moving 40, stationary 40.

```
send  FD FC FB FA  14 00  64 00
      00 00  03 00 00 00   01 00  28 00 00 00   02 00  28 00 00 00
      04 03 02 01
ack   FD FC FB FA  04 00  64 01  00 00  04 03 02 01
```

All gates: the same frame with `FF FF 00 00` as the gate value.

### 0x00A0 Read firmware version

Return value after the status: firmware type (2 bytes, `00 01`), version word
(2 bytes), build (4 bytes). The fields read as a version when printed in hex.

```
send  FD FC FB FA  02 00  A0 00  04 03 02 01
ack   FD FC FB FA  0C 00  A0 01  00 00  00 01  02 01  16 24 06 22  04 03 02 01
```

`02 01` and `16 24 06 22` give V1.02.22062416.

### 0x00A1 Set baud rate

| Index | Baud rate |
| --- | --- |
| `0x0001` | 9600 |
| `0x0002` | 19200 |
| `0x0003` | 38400 |
| `0x0004` | 57600 |
| `0x0005` | 115200 |
| `0x0006` | 230400 |
| `0x0007` | 256000 (factory default) |
| `0x0008` | 460800 |

```
send  FD FC FB FA  04 00  A1 00  07 00  04 03 02 01
ack   FD FC FB FA  04 00  A1 01  00 00  04 03 02 01
```

### 0x00A2 Restore factory settings

Takes effect after a restart.

```
send  FD FC FB FA  02 00  A2 00  04 03 02 01
ack   FD FC FB FA  04 00  A2 01  00 00  04 03 02 01
```

Factory defaults:

| Setting | Default |
| --- | --- |
| Max moving distance gate | 8 |
| Max stationary distance gate | 8 |
| No-one duration | 5 s |
| Baud rate | 256000 |
| Distance resolution | 0.75 m |

| Gate | Moving sensitivity | Stationary sensitivity |
| --- | --- | --- |
| 0 | 50 | not settable |
| 1 | 50 | not settable |
| 2 | 40 | 40 |
| 3 | 30 | 40 |
| 4 | 20 | 30 |
| 5 | 15 | 30 |
| 6 | 15 | 20 |
| 7 | 15 | 20 |
| 8 | 15 | 20 |

### 0x00A3 Restart

The module restarts after it has sent the ACK. The vendor document gives no
start-up time; the ESPHome `ld2410` component waits 1 second before it sends
the next command (`LD2410C_RESTART_DELAY_MS` in the driver).

```
send  FD FC FB FA  02 00  A3 00  04 03 02 01
ack   FD FC FB FA  04 00  A3 01  00 00  04 03 02 01
```

### 0x00A4 Bluetooth on/off

Bluetooth is on by default.

```
send  FD FC FB FA  04 00  A4 00  01 00  04 03 02 01      (on)
ack   FD FC FB FA  04 00  A4 01  00 00  04 03 02 01
```

### 0x00A5 Get MAC address

```
send  FD FC FB FA  04 00  A5 00  01 00  04 03 02 01
ack   FD FC FB FA  0A 00  A5 01  00 00  8F 27 2E B8 0F 65  04 03 02 01
```

The MAC address is 8F:27:2E:B8:0F:65.

Not in the vendor document: with Bluetooth switched off the module answers with
the placeholder `08 05 04 03 02 01`. The ESPHome `ld2410` component uses this to
tell whether Bluetooth is on, and so does `ld2410c_get_bluetooth()`.

### 0x00A8 Get Bluetooth permission

Sent by the app over Bluetooth with the 6-byte password; after a successful
check the module starts forwarding radar data over Bluetooth. The answer goes
to Bluetooth only, never to the serial port. The default password is `HiLink`
(`48 69 4C 69 6E 6B`).

### 0x00A9 Set Bluetooth password

The value is the 6 password bytes in text order.

```
send  FD FC FB FA  08 00  A9 00  48 69 4C 69 6E 6B  04 03 02 01
ack   FD FC FB FA  04 00  A9 01  00 00  04 03 02 01
```

### 0x00AA / 0x00AB Distance resolution

The length of one distance gate. There are 8 gates at either resolution.

| Index | Resolution |
| --- | --- |
| `0x0000` | 0.75 m (factory default) |
| `0x0001` | 0.2 m |

```
send  FD FC FB FA  04 00  AA 00  01 00  04 03 02 01      (set 0.2 m)
ack   FD FC FB FA  04 00  AA 01  00 00  04 03 02 01

send  FD FC FB FA  02 00  AB 00  04 03 02 01             (query)
ack   FD FC FB FA  06 00  AB 01  00 00  01 00  04 03 02 01   (0.2 m)
```

### 0x00AD / 0x00AE Auxiliary control (light sensor and OUT level)

The module has a photodiode; its reading is part of the engineering mode data.
With the auxiliary control enabled, OUT goes from "no one" to "someone" only
when the radar detects a person and the light condition is met. OUT goes back
to "no one" when the radar detects no one, whatever the light.

| Byte | Meaning |
| --- | --- |
| 1 | `0x00` light control off (default); `0x01` condition met when light < threshold; `0x02` condition met when light > threshold |
| 2 | light threshold, `0x00` to `0xFF`, default `0x80` |
| 3 | OUT default level: `0x00` low when idle, high on a target (default); `0x01` high when idle, low on a target |
| 4 | `0x00` |

```
send  FD FC FB FA  06 00  AD 00  01 60 00 00  04 03 02 01
ack   FD FC FB FA  04 00  AD 01  00 00  04 03 02 01

send  FD FC FB FA  02 00  AE 00  04 03 02 01
ack   FD FC FB FA  08 00  AE 01  00 00  01 60 01 00  04 03 02 01
```

The first example enables "light below threshold `0x60`", OUT default low; the
query answer shows the same with OUT default high.

### 0x000B Start background noise detection

The module enters noise detection mode and starts measuring 10 seconds later.
Everyone must leave the detection range within those 10 seconds and stay out
until the detection ends. The module records the energy of every gate with no
one present and then sets the sensitivity of every gate from it.

During the detection the target state in the reported frames shows its progress
(states `0x04` to `0x06` below).

```
send  FD FC FB FA  04 00  0B 00  0A 00  04 03 02 01      (10 s)
ack   FD FC FB FA  04 00  0B 01  00 00  04 03 02 01
```

### 0x001B Query noise detection status

| Status | Meaning |
| --- | --- |
| `0x0000` | not in progress |
| `0x0001` | in progress |
| `0x0002` | completed |

```
send  FD FC FB FA  02 00  1B 00  04 03 02 01
ack   FD FC FB FA  06 00  1B 01  00 00  01 00  04 03 02 01   (in progress)
```

## Reported data

| Header | Data length | Data | Tail |
| --- | --- | --- | --- |
| `F4 F3 F2 F1` | 2 bytes | see below | `F8 F7 F6 F5` |

Data:

| Data type | Head | Target data | Tail | Check |
| --- | --- | --- | --- | --- |
| 1 byte: `0x01` engineering mode, `0x02` basic target data | `0xAA` | see below | `0x55` | `0x00` |

### Basic target data

| Bytes | Content |
| --- | --- |
| 1 | target state |
| 2 | moving target distance, cm |
| 1 | moving target energy |
| 2 | stationary target distance, cm |
| 1 | stationary target energy |
| 2 | detection distance, cm |

| Target state | Meaning |
| --- | --- |
| `0x00` | no target |
| `0x01` | moving target |
| `0x02` | stationary target |
| `0x03` | moving and stationary target |
| `0x04` | background noise detection in progress |
| `0x05` | background noise detection succeeded |
| `0x06` | background noise detection failed |

States `0x04` to `0x06` appear only while the noise detection function runs.

Example:

```
F4 F3 F2 F1  0D 00  02 AA  02  51 00  00  00 00  3B  00 00  55 00  F8 F7 F6 F5
```

Basic data, stationary target, moving distance 81 cm with energy 0, stationary
distance 0 cm with energy 59, detection distance 0 cm.

### Engineering mode data

Added after the basic target data:

| Bytes | Content |
| --- | --- |
| 1 | max moving distance gate N |
| 1 | max stationary distance gate N |
| N + 1 | moving energy of gates 0 to N |
| N + 1 | stationary energy of gates 0 to N |
| 1 | light sensor value, 0 to 255 |
| 1 | OUT pin state: 0 no one, 1 someone |

Example (data length `0x23` = 35 bytes):

```
F4 F3 F2 F1  23 00
01 AA  03  1E 00  3C  00 00  39  00 00
08 08
3C 22 05 03 03 04 03 06 05
00 00 39 10 13 06 06 08 04
60 01
55 00
F8 F7 F6 F5
```

| Bytes | Meaning |
| --- | --- |
| `01 AA` | engineering mode data, head |
| `03` | moving and stationary target |
| `1E 00`, `3C` | moving target at 30 cm, energy 60 |
| `00 00`, `39` | stationary target at 0 cm, energy 57 |
| `00 00` | detection distance 0 cm |
| `08 08` | max moving gate, max stationary gate |
| `3C 22 05 03 03 04 03 06 05` | moving energy, gates 0 to 8 |
| `00 00 39 10 13 06 06 08 04` | stationary energy, gates 0 to 8 |
| `60`, `01` | light sensor value 96, OUT state "someone" |
| `55 00` | tail, check |

The vendor document does not say what the two max gate bytes hold when fewer
than 8 gates are configured. The driver does not rely on them: it takes the
number of gates from the data length (`17 + 2 * gates`). The ESPHome `ld2410`
component reads the same layout at fixed offsets for nine gates.

## Errata in the vendor document

These remain in V1.09:

- Section 1.2.2 gives the range of the farthest gate as 1 to 8; the command
  description (2.2.3) gives 2 to 8. The driver follows the command description.
- 2.2.12: the text gives the Bluetooth-on value as `0x0100`; the example sends
  `01 00`.
- 2.2.13: the text describes the return value as "1 byte fixed type + 3 bytes
  MAC"; the example carries a 6-byte MAC.
- 2.2.8: the firmware type is given as `0x0001`, the example shows the bytes
  `00 01`, which is `0x0100` in the little-endian order used everywhere else.
- The pin table numbers OUT as pin 1, UART_Tx as 2 and UART_Rx as 3, while the
  user manual V1.09 numbers UART_Tx 1, UART_Rx 2 and OUT 3. Go by the marking on
  the board.
- The revision list dates V1.09 2024-06-09; the cover says 2025-06-09.

## Changes since the English V1.07

- 2.2.8: the example ACK now has its `A0 01` command word, and its bytes match
  the caption (V1.02.22062416). V1.07 showed `07 01 16 15 09 22`, that is
  V1.07.22091516, under the same caption.
- 2.2.12, 2.2.15: the Bluetooth switch and the Bluetooth password are stated to
  be kept after power-off and to take effect after a restart.
- 2.2.14, 2.2.15: the examples no longer repeat `48 69` after the password.
- 2.2.16: the default is index `0x0000` (0.75 m), and the example ACK carries
  `AA 01`. V1.07 said "0x0001, which is 0.75m" and showed `A1 01`.
- 2.3.2: the two bytes after the gate energies are named: light sensor value
  (0 to 255) and OUT pin state (0 no one, 1 someone). The example frame ends
  with `60 01 55 00` and its breakdown matches it; V1.07 had `03 05 55 00` and
  a breakdown that did not match.
- No command was added or changed.
