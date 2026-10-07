#include "ld2410c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ld2410c";

enum
{
    CMD_HEADER_0 = 0xFD,
    CMD_HEADER_1 = 0xFC,
    CMD_HEADER_2 = 0xFB,
    CMD_HEADER_3 = 0xFA,
    CMD_TAIL_0   = 0x04,
    CMD_TAIL_1   = 0x03,
    CMD_TAIL_2   = 0x02,
    CMD_TAIL_3   = 0x01,

    DATA_HEADER_0 = 0xF4,
    DATA_HEADER_1 = 0xF3,
    DATA_HEADER_2 = 0xF2,
    DATA_HEADER_3 = 0xF1,
    DATA_TAIL_0   = 0xF8,
    DATA_TAIL_1   = 0xF7,
    DATA_TAIL_2   = 0xF6,
    DATA_TAIL_3   = 0xF5,

    CMD_ENABLE_CONFIG   = 0x00FF,
    CMD_END_CONFIG      = 0x00FE,
    CMD_SET_MAX_GATE    = 0x0060,
    CMD_READ_PARAMS     = 0x0061,
    CMD_ENABLE_ENG      = 0x0062,
    CMD_DISABLE_ENG     = 0x0063,
    CMD_SET_GATE_SENS   = 0x0064,
    CMD_READ_FW_VER     = 0x00A0,
    CMD_SET_BAUD        = 0x00A1,
    CMD_FACTORY_RESET   = 0x00A2,
    CMD_RESTART         = 0x00A3,
    CMD_SET_BT          = 0x00A4,
    CMD_GET_MAC         = 0x00A5,
    CMD_SET_BT_PASS     = 0x00A9,
    CMD_SET_RESOLUTION  = 0x00AA,
    CMD_GET_RESOLUTION  = 0x00AB,
    CMD_SET_AUX_CTRL    = 0x00AD,
    CMD_GET_AUX_CTRL    = 0x00AE,
    CMD_START_NOISE_DET = 0x000B,
    CMD_QUERY_NOISE_DET = 0x001B,

    ACK_BIT        = 0x0100,
    MAX_FRAME_SIZE = 256,

    FRAME_OVERHEAD      = 10, /* header(4) + len(2) + tail(4) */
    MIN_REPORT_DATA_LEN = 13, /* data_type(1) + head(1) + target(9) + tail(1) + check(1) */
    /* Engineering data length without the per-gate bytes: basic part(11) + max gates(2)
       + photosensitive(1) + out(1) + tail(1) + check(1). Each gate adds moving(1) + stationary(1). */
    ENG_DATA_OVERHEAD = 17,
    MIN_ENG_GATES     = 1,
    MAX_FAILED_POLLS    = 3, /* auto calibration gives up after this many unanswered status polls in a row */
    REPORT_HEAD         = 0xAA,
    REPORT_TAIL         = 0x55,
    REPORT_CHECK        = 0x00,

    MIN_CONFIG_GATE = 2,
    MAX_GATE        = LD2410C_MAX_DISTANCE_GATES - 1,
    ALL_GATES       = 0xFFFF,
    MAX_SENSITIVITY = 100,
};

#define CHECK_ARG(cond)                          \
    do {                                         \
        if (!(cond)) return ESP_ERR_INVALID_ARG; \
    } while (0)

static const uint8_t cmd_header[]  = {CMD_HEADER_0, CMD_HEADER_1, CMD_HEADER_2, CMD_HEADER_3};
static const uint8_t cmd_tail[]    = {CMD_TAIL_0, CMD_TAIL_1, CMD_TAIL_2, CMD_TAIL_3};
static const uint8_t data_header[] = {DATA_HEADER_0, DATA_HEADER_1, DATA_HEADER_2, DATA_HEADER_3};
static const uint8_t data_tail[]   = {DATA_TAIL_0, DATA_TAIL_1, DATA_TAIL_2, DATA_TAIL_3};

/* ---------- low-level helpers ---------- */

static size_t frame_begin(uint8_t *buf, size_t pos)
{
    buf[pos + 0] = CMD_HEADER_0;
    buf[pos + 1] = CMD_HEADER_1;
    buf[pos + 2] = CMD_HEADER_2;
    buf[pos + 3] = CMD_HEADER_3;
    return pos + 4;
}

static size_t frame_end(uint8_t *buf, size_t pos)
{
    buf[pos + 0] = CMD_TAIL_0;
    buf[pos + 1] = CMD_TAIL_1;
    buf[pos + 2] = CMD_TAIL_2;
    buf[pos + 3] = CMD_TAIL_3;
    return pos + 4;
}

static size_t put_u16(uint8_t *buf, size_t pos, uint16_t val)
{
    buf[pos + 0] = val & 0xFF;
    buf[pos + 1] = (val >> 8) & 0xFF;
    return pos + 2;
}

static size_t put_u32(uint8_t *buf, size_t pos, uint32_t val)
{
    buf[pos + 0] = val & 0xFF;
    buf[pos + 1] = (val >> 8) & 0xFF;
    buf[pos + 2] = (val >> 16) & 0xFF;
    buf[pos + 3] = (val >> 24) & 0xFF;
    return pos + 4;
}

static uint16_t get_u16(const uint8_t *buf, size_t off)
{
    return buf[off] | ((uint16_t)buf[off + 1] << 8);
}

static uint32_t get_u32(const uint8_t *buf, size_t off)
{
    return buf[off] | ((uint32_t)buf[off + 1] << 8) | ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24);
}

static size_t build_frame(uint8_t *buf, uint16_t cmd, const uint8_t *value, size_t value_len)
{
    /* header(4) + len(2) + cmd(2) + value + tail(4) */
    size_t total = 4 + 2 + 2 + value_len + 4;
    if (total > MAX_FRAME_SIZE) {
        ESP_LOGE(TAG, "Frame too large: %u > %u", (unsigned)total, MAX_FRAME_SIZE);
        return 0;
    }

    size_t pos = frame_begin(buf, 0);
    uint16_t data_len = 2 + value_len; /* cmd word + value */
    pos = put_u16(buf, pos, data_len);
    pos = put_u16(buf, pos, cmd);
    if (value && value_len > 0) {
        memcpy(&buf[pos], value, value_len);
        pos += value_len;
    }
    pos = frame_end(buf, pos);
    return pos;
}

static esp_err_t send_frame(ld2410c_handle_t *handle, const uint8_t *frame, size_t len)
{
    int written = uart_write_bytes(handle->uart_port, frame, len);
    if (written != (int)len) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* Ticks remaining until start + timeout, 0 once the deadline has passed */
static TickType_t ticks_left(TickType_t start, TickType_t timeout)
{
    TickType_t elapsed = xTaskGetTickCount() - start;
    return elapsed < timeout ? timeout - elapsed : 0;
}

/* Handle timeout in ticks, at least one: a sub-tick timeout must not make every read non-blocking */
static TickType_t handle_timeout_ticks(const ld2410c_handle_t *handle)
{
    TickType_t ticks = pdMS_TO_TICKS(handle->timeout_ms);
    return ticks > 0 ? ticks : 1;
}

/* Put bytes back at the front of the stream so the next read sees them first */
static void rx_unread(ld2410c_handle_t *handle, const uint8_t *data, size_t len)
{
    /* A frame never exceeds MAX_FRAME_SIZE, so the pending bytes always fit; clamp anyway */
    size_t room = LD2410C_RX_PENDING_SIZE - handle->rx_pending_len;
    if (len > room) len = room;
    memmove(&handle->rx_pending[len], handle->rx_pending, handle->rx_pending_len);
    memcpy(handle->rx_pending, data, len);
    handle->rx_pending_len += len;
}

/* Drop everything received so far, including the bytes put back by rx_unread() */
static void rx_flush(ld2410c_handle_t *handle)
{
    handle->rx_pending_len = 0;
    uart_flush_input(handle->uart_port);
}

/*
 * Read up to len bytes: first the ones put back by rx_unread(), then from the UART until the
 * deadline. Returns how many bytes were read, which is less than len only on timeout.
 * Sets *from_uart when at least one byte came from the UART itself (not from the put-back ones).
 */
static size_t read_some(ld2410c_handle_t *handle, uint8_t *buf, size_t len, TickType_t start, TickType_t timeout, bool *from_uart)
{
    size_t got = 0;
    if (handle->rx_pending_len > 0) {
        got = handle->rx_pending_len < len ? handle->rx_pending_len : len;
        memcpy(buf, handle->rx_pending, got);
        handle->rx_pending_len -= got;
        memmove(handle->rx_pending, &handle->rx_pending[got], handle->rx_pending_len);
    }
    if (got == len) return got;

    int n = uart_read_bytes(handle->uart_port, buf + got, len - got, ticks_left(start, timeout));
    if (n > 0) {
        *from_uart = true;
        got += (size_t)n;
    }
    return got;
}

/*
 * Read one complete frame (header + len + data + tail) to the start of buf,
 * bounded by the deadline start + timeout.
 *
 * Consumes exactly that frame: bytes preceding the header are dropped, bytes
 * following the tail stay available for the next call. When a candidate frame
 * turns out to be damaged (bad length or bad tail), only its first byte is
 * dropped and the rest is scanned again, so a good frame that began inside the
 * damaged one is still found. Frames longer than the buffer or than
 * MAX_FRAME_SIZE are treated as damaged. On timeout the part of a frame that
 * was already received is kept for the next call.
 */
static esp_err_t read_frame_until(ld2410c_handle_t *handle, const uint8_t *header, const uint8_t *tail, uint8_t *buf, size_t buf_size, size_t *frame_len,
                                  TickType_t start, TickType_t timeout)
{
    size_t max_total = buf_size < MAX_FRAME_SIZE ? buf_size : MAX_FRAME_SIZE;
    size_t have      = 0;
    bool   got_any   = false;

    for (;;) {
        /* Sync on the header one byte at a time (header bytes are all distinct) */
        uint8_t b;
        if (read_some(handle, &b, 1, start, timeout, &got_any) < 1) {
            if (have > 0) rx_unread(handle, buf, have); /* a partial header */
            break;
        }
        if (b != header[have]) have = 0;
        if (b != header[have]) continue;
        buf[have++] = b;
        if (have < 4) continue;
        have = 0;

        size_t got = read_some(handle, &buf[4], 2, start, timeout, &got_any);
        if (got < 2) {
            rx_unread(handle, buf, 4 + got);
            break;
        }
        size_t data_len = get_u16(buf, 4);
        size_t total    = FRAME_OVERHEAD + data_len;
        if (total > max_total) {
            rx_unread(handle, &buf[1], 5); /* corrupted length: resync after the first header byte */
            continue;
        }

        got = read_some(handle, &buf[6], data_len + 4, start, timeout, &got_any);
        if (got < data_len + 4) {
            rx_unread(handle, buf, 6 + got);
            break;
        }
        if (memcmp(&buf[6 + data_len], tail, 4) != 0) {
            rx_unread(handle, &buf[1], total - 1); /* damaged tail: the next frame may start inside */
            continue;
        }

        *frame_len = total;
        return ESP_OK;
    }

    return got_any ? ESP_ERR_NOT_FOUND : ESP_ERR_TIMEOUT;
}

static esp_err_t read_frame(ld2410c_handle_t *handle, const uint8_t *header, const uint8_t *tail, uint8_t *buf, size_t buf_size, size_t *frame_len)
{
    return read_frame_until(handle, header, tail, buf, buf_size, frame_len, xTaskGetTickCount(), handle_timeout_ticks(handle));
}

static esp_err_t recv_ack(ld2410c_handle_t *handle, uint16_t expected_cmd, uint8_t *out_buf, size_t out_buf_size, size_t *out_len)
{
    uint8_t    buf[MAX_FRAME_SIZE];
    size_t     frame_len;
    TickType_t start    = xTaskGetTickCount();
    TickType_t timeout  = handle_timeout_ticks(handle);
    esp_err_t  late_err = ESP_ERR_TIMEOUT; /* reported if the ACK we wait for never arrives */

    for (;;) {
        if (read_frame_until(handle, cmd_header, cmd_tail, buf, sizeof(buf), &frame_len, start, timeout) != ESP_OK) {
            if (late_err == ESP_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "Timeout waiting for ACK to cmd 0x%04X", expected_cmd);
            } else {
                ESP_LOGE(TAG, "No ACK for cmd 0x%04X, got ACK for another command", expected_cmd);
            }
            return late_err;
        }

        /* A late ACK of an earlier, timed-out command may still be in front of ours: skip it */
        uint16_t data_len = get_u16(buf, 4);
        if (data_len >= 2) {
            uint16_t ack_cmd = get_u16(buf, 6);
            if (ack_cmd != (expected_cmd | ACK_BIT)) {
                ESP_LOGW(TAG, "Skipping ACK 0x%04X while waiting for 0x%04X", ack_cmd, expected_cmd | ACK_BIT);
                late_err = ESP_ERR_INVALID_RESPONSE;
                if (ticks_left(start, timeout) == 0) return late_err;
                continue;
            }
        }
        break;
    }

    /* ACK data is at least cmd(2) + status(2) */
    uint16_t data_len = get_u16(buf, 4);
    if (data_len < 4) {
        ESP_LOGE(TAG, "ACK too short: %u", data_len);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Check status */
    uint16_t status = get_u16(buf, 8);
    if (status != 0) {
        ESP_LOGE(TAG, "Command 0x%04X failed, status: %d", expected_cmd, status);
        return ESP_FAIL;
    }

    /* Copy return value data (after cmd_word + status) */
    if (out_buf && out_buf_size > 0) {
        size_t val_len  = data_len - 4;
        size_t copy_len = val_len < out_buf_size ? val_len : out_buf_size;
        memcpy(out_buf, &buf[10], copy_len);
        if (out_len) *out_len = copy_len;
    }

    return ESP_OK;
}

static esp_err_t send_command(ld2410c_handle_t *handle, uint16_t cmd, const uint8_t *value, size_t value_len, uint8_t *ret_buf, size_t ret_buf_size,
                              size_t *ret_len)
{
    CHECK_ARG(handle);

    uint8_t frame[MAX_FRAME_SIZE];
    size_t  frame_len = build_frame(frame, cmd, value, value_len);

    if (frame_len == 0) return ESP_ERR_INVALID_SIZE;

    /* Flush RX before sending */
    rx_flush(handle);

    esp_err_t err = send_frame(handle, frame, frame_len);
    if (err != ESP_OK) return err;

    return recv_ack(handle, cmd, ret_buf, ret_buf_size, ret_len);
}

static esp_err_t send_simple_command(ld2410c_handle_t *handle, uint16_t cmd)
{
    return send_command(handle, cmd, NULL, 0, NULL, 0, NULL);
}

/* ---------- public API ---------- */

ld2410c_handle_t *ld2410c_init(uart_port_t port, int timeout_ms)
{
    if ((unsigned)port >= UART_NUM_MAX || timeout_ms <= 0) {
        return NULL;
    }

    ld2410c_handle_t *handle = malloc(sizeof(ld2410c_handle_t));
    if (!handle) {
        return NULL;
    }
    handle->uart_port      = port;
    handle->timeout_ms     = timeout_ms;
    handle->rx_pending_len = 0;
    return handle;
}

void ld2410c_deinit(ld2410c_handle_t **handle)
{
    if (!handle) return;
    free(*handle);
    *handle = NULL;
}

esp_err_t ld2410c_enable_config(ld2410c_handle_t *handle)
{
    uint8_t value[2] = {0x01, 0x00};
    uint8_t ret[4];
    size_t  ret_len = 0;
    return send_command(handle, CMD_ENABLE_CONFIG, value, sizeof(value), ret, sizeof(ret), &ret_len);
}

esp_err_t ld2410c_end_config(ld2410c_handle_t *handle)
{
    return send_simple_command(handle, CMD_END_CONFIG);
}

esp_err_t ld2410c_set_max_gate_and_duration(ld2410c_handle_t *handle, uint8_t max_moving_gate, uint8_t max_stationary_gate, uint16_t no_one_duration_s)
{
    CHECK_ARG(max_moving_gate >= MIN_CONFIG_GATE && max_moving_gate <= MAX_GATE);
    CHECK_ARG(max_stationary_gate >= MIN_CONFIG_GATE && max_stationary_gate <= MAX_GATE);

    uint8_t value[18];
    /* max motion distance gate word + value */
    size_t pos = put_u16(value, 0, 0x0000);
    pos = put_u32(value, pos, max_moving_gate);
    /* max stationary distance gate word + value */
    pos = put_u16(value, pos, 0x0001);
    pos = put_u32(value, pos, max_stationary_gate);
    /* no-one duration word + value */
    pos = put_u16(value, pos, 0x0002);
    pos = put_u32(value, pos, no_one_duration_s);

    return send_command(handle, CMD_SET_MAX_GATE, value, pos, NULL, 0, NULL);
}

esp_err_t ld2410c_read_params(ld2410c_handle_t *handle, ld2410c_params_t *params)
{
    CHECK_ARG(params);

    uint8_t   ret[64];
    size_t    ret_len = 0;
    esp_err_t err     = send_command(handle, CMD_READ_PARAMS, NULL, 0, ret, sizeof(ret), &ret_len);
    if (err != ESP_OK) return err;

    /* ret[0] = 0xAA header, ret[1] = max distance gate N */
    if (ret_len < 4 || ret[0] != 0xAA || ret[1] > MAX_GATE) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t max_gate = ret[1];

    uint8_t num_gates = max_gate + 1;
    size_t  expected  = 4 + num_gates + num_gates + 2; /* header+gates+mov_sens+stat_sens+duration */
    if (ret_len < expected) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Only the upper bounds are checked: the vendor document also gives 1 as a valid max gate (section 1.2.2) */
    if (ret[2] > max_gate || ret[3] > max_gate) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    for (int i = 0; i < 2 * num_gates; i++) {
        if (ret[4 + i] > MAX_SENSITIVITY) return ESP_ERR_INVALID_RESPONSE;
    }

    params->max_moving_gate     = ret[2];
    params->max_stationary_gate = ret[3];

    memset(params->moving_sensitivity, 0, sizeof(params->moving_sensitivity));
    memset(params->stationary_sensitivity, 0, sizeof(params->stationary_sensitivity));
    for (int i = 0; i < num_gates; i++) {
        params->moving_sensitivity[i] = ret[4 + i];
    }
    for (int i = 0; i < num_gates; i++) {
        params->stationary_sensitivity[i] = ret[4 + num_gates + i];
    }
    params->no_one_duration = get_u16(ret, 4 + num_gates + num_gates);

    return ESP_OK;
}

esp_err_t ld2410c_set_gate_sensitivity(ld2410c_handle_t *handle, uint16_t gate, uint8_t moving_sensitivity, uint8_t stationary_sensitivity)
{
    CHECK_ARG(gate <= MAX_GATE || gate == ALL_GATES);
    CHECK_ARG(moving_sensitivity <= MAX_SENSITIVITY && stationary_sensitivity <= MAX_SENSITIVITY);

    uint8_t value[18];
    size_t pos = put_u16(value, 0, 0x0000);
    pos = put_u32(value, pos, gate);
    pos = put_u16(value, pos, 0x0001);
    pos = put_u32(value, pos, moving_sensitivity);
    pos = put_u16(value, pos, 0x0002);
    pos = put_u32(value, pos, stationary_sensitivity);

    return send_command(handle, CMD_SET_GATE_SENS, value, pos, NULL, 0, NULL);
}

esp_err_t ld2410c_set_all_gate_sensitivity(ld2410c_handle_t *handle, uint8_t moving_sensitivity, uint8_t stationary_sensitivity)
{
    return ld2410c_set_gate_sensitivity(handle, ALL_GATES, moving_sensitivity, stationary_sensitivity);
}

esp_err_t ld2410c_enable_engineering_mode(ld2410c_handle_t *handle)
{
    return send_simple_command(handle, CMD_ENABLE_ENG);
}

esp_err_t ld2410c_disable_engineering_mode(ld2410c_handle_t *handle)
{
    return send_simple_command(handle, CMD_DISABLE_ENG);
}

esp_err_t ld2410c_read_firmware_version(ld2410c_handle_t *handle, ld2410c_firmware_ver_t *ver)
{
    CHECK_ARG(ver);

    uint8_t   ret[8];
    size_t    ret_len = 0;
    esp_err_t err     = send_command(handle, CMD_READ_FW_VER, NULL, 0, ret, sizeof(ret), &ret_len);
    if (err != ESP_OK) return err;

    if (ret_len < 8) return ESP_ERR_INVALID_SIZE;

    /* type(2) + version word(2, LE: minor then major) + build(4, LE), all BCD-style */
    ver->firmware_type = get_u16(ret, 0);
    ver->minor         = ret[2];
    ver->major         = ret[3];
    ver->patch         = get_u32(ret, 4);

    return ESP_OK;
}

esp_err_t ld2410c_set_baud_rate(ld2410c_handle_t *handle, ld2410c_baud_t baud)
{
    CHECK_ARG(baud >= LD2410C_BAUD_9600 && baud <= LD2410C_BAUD_460800);

    uint8_t value[2];
    size_t pos = put_u16(value, 0, (uint16_t)baud);
    return send_command(handle, CMD_SET_BAUD, value, pos, NULL, 0, NULL);
}

esp_err_t ld2410c_factory_reset(ld2410c_handle_t *handle)
{
    return send_simple_command(handle, CMD_FACTORY_RESET);
}

esp_err_t ld2410c_restart(ld2410c_handle_t *handle)
{
    return send_simple_command(handle, CMD_RESTART);
}

esp_err_t ld2410c_set_bluetooth(ld2410c_handle_t *handle, bool enable)
{
    uint8_t value[2];
    size_t pos = put_u16(value, 0, enable ? 0x0001 : 0x0000);
    return send_command(handle, CMD_SET_BT, value, pos, NULL, 0, NULL);
}

esp_err_t ld2410c_get_mac_address(ld2410c_handle_t *handle, uint8_t mac[6])
{
    CHECK_ARG(mac);

    uint8_t   value[2] = {0x01, 0x00};
    uint8_t   ret[8];
    size_t    ret_len = 0;
    esp_err_t err     = send_command(handle, CMD_GET_MAC, value, sizeof(value), ret, sizeof(ret), &ret_len);
    if (err != ESP_OK) return err;

    /* Return value is the 6-byte MAC (big-endian) right after the ACK status */
    if (ret_len < 6) return ESP_ERR_INVALID_SIZE;

    memcpy(mac, ret, 6);
    return ESP_OK;
}

esp_err_t ld2410c_get_bluetooth(ld2410c_handle_t *handle, bool *enabled)
{
    CHECK_ARG(enabled);

    /* The module reports this address instead of its own while Bluetooth is off */
    static const uint8_t no_mac[6] = {0x08, 0x05, 0x04, 0x03, 0x02, 0x01};

    uint8_t   mac[6];
    esp_err_t err = ld2410c_get_mac_address(handle, mac);
    if (err != ESP_OK) return err;

    *enabled = memcmp(mac, no_mac, sizeof(mac)) != 0;
    return ESP_OK;
}

esp_err_t ld2410c_set_bluetooth_password(ld2410c_handle_t *handle, const char password[6])
{
    CHECK_ARG(password);

    return send_command(handle, CMD_SET_BT_PASS, (const uint8_t *)password, 6, NULL, 0, NULL);
}

esp_err_t ld2410c_set_distance_resolution(ld2410c_handle_t *handle, ld2410c_resolution_t res)
{
    CHECK_ARG((unsigned)res <= LD2410C_RESOLUTION_020M);

    uint8_t value[2];
    size_t pos = put_u16(value, 0, (uint16_t)res);
    return send_command(handle, CMD_SET_RESOLUTION, value, pos, NULL, 0, NULL);
}

esp_err_t ld2410c_get_distance_resolution(ld2410c_handle_t *handle, ld2410c_resolution_t *res)
{
    CHECK_ARG(res);

    uint8_t   ret[4];
    size_t    ret_len = 0;
    esp_err_t err     = send_command(handle, CMD_GET_RESOLUTION, NULL, 0, ret, sizeof(ret), &ret_len);
    if (err != ESP_OK) return err;

    if (ret_len < 2) return ESP_ERR_INVALID_SIZE;
    uint16_t value = get_u16(ret, 0);
    if (value > LD2410C_RESOLUTION_020M) return ESP_ERR_INVALID_RESPONSE;
    *res = (ld2410c_resolution_t)value;
    return ESP_OK;
}

esp_err_t ld2410c_set_aux_control(ld2410c_handle_t *handle, const ld2410c_aux_ctrl_t *ctrl)
{
    CHECK_ARG(ctrl);
    CHECK_ARG((unsigned)ctrl->mode <= LD2410C_LIGHT_CTRL_ABOVE_THRESH);
    CHECK_ARG((unsigned)ctrl->out_default <= LD2410C_OUT_DEFAULT_HIGH);

    uint8_t value[4] = {
        (uint8_t)ctrl->mode,
        ctrl->threshold,
        (uint8_t)ctrl->out_default,
        0x00,
    };
    return send_command(handle, CMD_SET_AUX_CTRL, value, sizeof(value), NULL, 0, NULL);
}

esp_err_t ld2410c_get_aux_control(ld2410c_handle_t *handle, ld2410c_aux_ctrl_t *ctrl)
{
    CHECK_ARG(ctrl);

    uint8_t   ret[4];
    size_t    ret_len = 0;
    esp_err_t err     = send_command(handle, CMD_GET_AUX_CTRL, NULL, 0, ret, sizeof(ret), &ret_len);
    if (err != ESP_OK) return err;

    if (ret_len < 3) return ESP_ERR_INVALID_SIZE;
    if (ret[0] > LD2410C_LIGHT_CTRL_ABOVE_THRESH || ret[2] > LD2410C_OUT_DEFAULT_HIGH) return ESP_ERR_INVALID_RESPONSE;
    ctrl->mode        = (ld2410c_light_ctrl_mode_t)ret[0];
    ctrl->threshold   = ret[1];
    ctrl->out_default = (ld2410c_out_level_t)ret[2];
    return ESP_OK;
}

esp_err_t ld2410c_start_noise_detection(ld2410c_handle_t *handle, uint16_t duration_s)
{
    uint8_t value[2];
    size_t pos = put_u16(value, 0, duration_s);
    return send_command(handle, CMD_START_NOISE_DET, value, pos, NULL, 0, NULL);
}

esp_err_t ld2410c_query_noise_detection_status(ld2410c_handle_t *handle, ld2410c_noise_status_t *status)
{
    CHECK_ARG(status);

    uint8_t   ret[4];
    size_t    ret_len = 0;
    esp_err_t err     = send_command(handle, CMD_QUERY_NOISE_DET, NULL, 0, ret, sizeof(ret), &ret_len);
    if (err != ESP_OK) return err;

    if (ret_len < 2) return ESP_ERR_INVALID_SIZE;
    uint16_t value = get_u16(ret, 0);
    if (value > LD2410C_NOISE_COMPLETED) return ESP_ERR_INVALID_RESPONSE;
    *status = (ld2410c_noise_status_t)value;
    return ESP_OK;
}

/* ---------- data frame parsing ---------- */

static int find_data_frame(const uint8_t *buf, size_t len, size_t *frame_start, size_t *frame_len)
{
    for (size_t i = 0; i + 10 <= len; i++) {
        if (memcmp(&buf[i], data_header, 4) != 0) continue;

        uint16_t data_len = get_u16(buf, i + 4);
        size_t   total    = 4 + 2 + data_len + 4;
        if (i + total > len) continue;

        if (memcmp(&buf[i + 4 + 2 + data_len], data_tail, 4) != 0) continue;

        *frame_start = i;
        *frame_len   = total;
        return 0;
    }
    return -1;
}

/*
 * Locate a data frame in buf and validate its intra-frame envelope
 * (minimum length, 0xAA head, 0x55 tail, 0x00 check). On success *d points
 * at the data type byte and *data_len holds the intra-frame data length.
 */
static esp_err_t find_report(const uint8_t *buf, size_t len, const uint8_t **d, uint16_t *data_len)
{
    size_t start, flen;
    if (find_data_frame(buf, len, &start, &flen) != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    *d        = &buf[start + 6]; /* skip header(4) + length(2) */
    *data_len = get_u16(buf, start + 4);

    if (*data_len < MIN_REPORT_DATA_LEN) return ESP_ERR_INVALID_SIZE;

    if ((*d)[1] != REPORT_HEAD || (*d)[*data_len - 2] != REPORT_TAIL || (*d)[*data_len - 1] != REPORT_CHECK) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

static void parse_basic(const uint8_t *d, ld2410c_target_data_t *data)
{
    data->state                  = (ld2410c_target_state_t)d[2];
    data->moving_distance_cm     = get_u16(d, 3);
    data->moving_energy          = d[5];
    data->stationary_distance_cm = get_u16(d, 6);
    data->stationary_energy      = d[8];
    data->detection_distance_cm  = get_u16(d, 9);
}

esp_err_t ld2410c_parse_target_data(const uint8_t *frame, size_t len, ld2410c_target_data_t *data)
{
    CHECK_ARG(frame && data);

    const uint8_t *d;
    uint16_t       data_len;
    esp_err_t      err = find_report(frame, len, &d, &data_len);
    if (err != ESP_OK) return err;

    uint8_t data_type = d[0];
    if (data_type != 0x02 && data_type != 0x01) {
        return ESP_ERR_INVALID_ARG;
    }

    parse_basic(d, data);

    return ESP_OK;
}

esp_err_t ld2410c_parse_engineering_data(const uint8_t *frame, size_t len, ld2410c_engineering_data_t *data)
{
    CHECK_ARG(frame && data);

    const uint8_t *d;
    uint16_t       data_len;
    esp_err_t      err = find_report(frame, len, &d, &data_len);
    if (err != ESP_OK) return err;

    if (d[0] != 0x01) {
        return ESP_ERR_INVALID_ARG; /* not engineering mode data */
    }

    /*
     * Engineering extra data starts at d[11]: max moving gate, max stationary gate,
     * moving energy per gate, stationary energy per gate, photosensitive, OUT,
     * then tail(1) + check(1).
     *
     * The number of gates comes from the frame length, not from the max gate
     * bytes: the module sends the energies of all nine gates whatever those
     * bytes say (the ESPHome ld2410 component reads them at fixed offsets too).
     */
    if (data_len < ENG_DATA_OVERHEAD + 2 * MIN_ENG_GATES) return ESP_ERR_INVALID_SIZE;
    size_t gate_bytes = data_len - ENG_DATA_OVERHEAD;
    if (gate_bytes % 2 != 0 || gate_bytes / 2 > LD2410C_MAX_DISTANCE_GATES) return ESP_ERR_INVALID_SIZE;
    size_t num_gates = gate_bytes / 2;

    size_t off = 11;
    uint8_t max_moving_gate     = d[off++];
    uint8_t max_stationary_gate = d[off++];
    if (max_moving_gate > MAX_GATE || max_stationary_gate > MAX_GATE) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(data, 0, sizeof(*data));
    parse_basic(d, &data->basic);
    data->max_moving_gate     = max_moving_gate;
    data->max_stationary_gate = max_stationary_gate;

    for (size_t i = 0; i < num_gates; i++) {
        data->moving_gate_energy[i] = d[off++];
    }
    for (size_t i = 0; i < num_gates; i++) {
        data->stationary_gate_energy[i] = d[off++];
    }

    data->photosensitive = d[off++];
    data->out_pin_state  = d[off++];

    return ESP_OK;
}

esp_err_t ld2410c_read_data_frame(ld2410c_handle_t *handle, uint8_t *buf, size_t buf_size, size_t *out_len)
{
    CHECK_ARG(handle && buf);
    if (out_len) *out_len = 0;
    if (buf_size < FRAME_OVERHEAD + MIN_REPORT_DATA_LEN) return ESP_ERR_INVALID_SIZE;

    size_t    frame_len;
    esp_err_t err = read_frame(handle, data_header, data_tail, buf, buf_size, &frame_len);
    if (err != ESP_OK) return err;

    if (out_len) *out_len = frame_len;
    return ESP_OK;
}

esp_err_t ld2410c_flush_input(ld2410c_handle_t *handle)
{
    CHECK_ARG(handle);
    handle->rx_pending_len = 0;
    return uart_flush_input(handle->uart_port);
}

/* ---------- high-level functions ---------- */

/*
 * Enter config mode. If the ACK timed out, the module may still have entered
 * config mode, where it stops sending data frames, so leave it again (best effort).
 */
static esp_err_t config_begin(ld2410c_handle_t *handle)
{
    esp_err_t err = ld2410c_enable_config(handle);
    if (err == ESP_ERR_TIMEOUT) {
        ld2410c_end_config(handle);
    }
    return err;
}

/* Leave config mode, retrying once: a module stuck in config mode sends no data frames */
static esp_err_t config_end(ld2410c_handle_t *handle)
{
    esp_err_t err = ld2410c_end_config(handle);
    if (err != ESP_OK) {
        err = ld2410c_end_config(handle);
    }
    return err;
}

/* Helper: run a block of commands inside enable_config / end_config */
#define CONFIG_BEGIN(h)                                 \
    do {                                                \
        esp_err_t _err = config_begin(h);               \
        if (_err != ESP_OK) return _err;                \
    } while (0)

#define CONFIG_END(h)                                   \
    do {                                                \
        esp_err_t _err2 = config_end(h);                \
        if (_err2 != ESP_OK) return _err2;              \
    } while (0)

esp_err_t ld2410c_configure_detection(ld2410c_handle_t *handle,
                                      uint8_t max_moving_gate,
                                      uint8_t max_stationary_gate,
                                      uint16_t no_one_duration_s,
                                      uint8_t moving_sensitivity,
                                      uint8_t stationary_sensitivity)
{
    /* Checked here as well, so that invalid arguments do not put the module into config mode first */
    CHECK_ARG(max_moving_gate >= MIN_CONFIG_GATE && max_moving_gate <= MAX_GATE);
    CHECK_ARG(max_stationary_gate >= MIN_CONFIG_GATE && max_stationary_gate <= MAX_GATE);
    CHECK_ARG(moving_sensitivity <= MAX_SENSITIVITY && stationary_sensitivity <= MAX_SENSITIVITY);

    CONFIG_BEGIN(handle);

    esp_err_t err = ld2410c_set_max_gate_and_duration(handle, max_moving_gate,
                                                      max_stationary_gate, no_one_duration_s);
    if (err != ESP_OK) {
        config_end(handle);
        return err;
    }

    err = ld2410c_set_all_gate_sensitivity(handle, moving_sensitivity, stationary_sensitivity);
    if (err != ESP_OK) {
        config_end(handle);
        return err;
    }

    CONFIG_END(handle);
    return ESP_OK;
}

esp_err_t ld2410c_get_firmware_string(ld2410c_handle_t *handle, char *buf, size_t buf_size)
{
    CHECK_ARG(buf && buf_size > 0);

    CONFIG_BEGIN(handle);

    ld2410c_firmware_ver_t ver;
    esp_err_t err = ld2410c_read_firmware_version(handle, &ver);
    if (err != ESP_OK) {
        config_end(handle);
        return err;
    }

    CONFIG_END(handle);

    /* Version fields are BCD-style, so print them as hex: e.g. "V1.07.22091516" */
    int len = snprintf(buf, buf_size, "V%X.%02X.%08lX",
                       ver.major, ver.minor, (unsigned long)ver.patch);
    if (len < 0 || (size_t)len >= buf_size) return ESP_ERR_INVALID_SIZE; /* truncated */
    return ESP_OK;
}

esp_err_t ld2410c_get_full_config(ld2410c_handle_t *handle,
                                  ld2410c_params_t *params,
                                  ld2410c_resolution_t *resolution)
{
    CHECK_ARG(params && resolution);

    CONFIG_BEGIN(handle);

    esp_err_t err = ld2410c_read_params(handle, params);
    if (err != ESP_OK) {
        config_end(handle);
        return err;
    }

    err = ld2410c_get_distance_resolution(handle, resolution);
    if (err != ESP_OK) {
        config_end(handle);
        return err;
    }

    CONFIG_END(handle);
    return ESP_OK;
}

esp_err_t ld2410c_factory_reset_and_restart(ld2410c_handle_t *handle)
{
    CONFIG_BEGIN(handle);

    esp_err_t err = ld2410c_factory_reset(handle);
    if (err != ESP_OK) {
        config_end(handle);
        return err;
    }

    /* A restart leaves config mode by itself, and the restarting module would not answer end_config */
    err = ld2410c_restart(handle);
    if (err != ESP_OK) {
        config_end(handle);
    }
    return err;
}

esp_err_t ld2410c_auto_calibrate(ld2410c_handle_t *handle, uint16_t duration_s, uint32_t poll_interval_ms)
{
    CHECK_ARG(poll_interval_ms > 0);

    CONFIG_BEGIN(handle);

    esp_err_t err = ld2410c_start_noise_detection(handle, duration_s);
    if (err != ESP_OK) {
        config_end(handle);
        return err;
    }

    CONFIG_END(handle);

    /* Timeout: detection duration + 15s headroom (10s startup + margin) */
    uint32_t total_ms = ((uint32_t)duration_s + 15) * 1000;
    if (poll_interval_ms > total_ms) poll_interval_ms = total_ms;
    uint32_t max_polls = total_ms / poll_interval_ms + (total_ms % poll_interval_ms != 0);

    /* Poll until detection completes */
    TickType_t poll_ticks = pdMS_TO_TICKS(poll_interval_ms);
    if (poll_ticks == 0) poll_ticks = 1;

    int failed_polls = 0;
    for (uint32_t i = 0; i < max_polls; i++) {
        vTaskDelay(poll_ticks);

        ld2410c_noise_status_t status = LD2410C_NOISE_NOT_IN_PROGRESS;
        err = config_begin(handle);
        if (err == ESP_OK) {
            err = ld2410c_query_noise_detection_status(handle, &status);
            esp_err_t end_err = config_end(handle);

            /* Report the query's own error first, not the one from leaving config mode */
            if (err == ESP_OK && end_err != ESP_OK) return end_err;
        }

        if (err != ESP_OK) {
            /* A lost or garbled ACK says nothing about the calibration: ask again at the next poll */
            bool transient = err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_RESPONSE;
            if (!transient || ++failed_polls >= MAX_FAILED_POLLS) return err;
            continue;
        }
        failed_polls = 0;

        if (status == LD2410C_NOISE_COMPLETED) {
            ESP_LOGI(TAG, "Noise calibration completed");
            return ESP_OK;
        }
        if (status == LD2410C_NOISE_NOT_IN_PROGRESS) {
            ESP_LOGE(TAG, "Noise calibration not in progress (unexpected)");
            return ESP_FAIL;
        }
    }

    ESP_LOGE(TAG, "Noise calibration timed out");
    return ESP_ERR_TIMEOUT;
}
