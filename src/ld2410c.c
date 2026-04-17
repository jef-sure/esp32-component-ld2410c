#include "ld2410c.h"
#include "esp_log.h"
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
    CMD_BT_PASSWORD     = 0x00A8,
    CMD_SET_BT_PASS     = 0x00A9,
    CMD_SET_RESOLUTION  = 0x00AA,
    CMD_GET_RESOLUTION  = 0x00AB,
    CMD_SET_AUX_CTRL    = 0x00AD,
    CMD_GET_AUX_CTRL    = 0x00AE,
    CMD_START_NOISE_DET = 0x000B,
    CMD_QUERY_NOISE_DET = 0x001B,

    ACK_BIT        = 0x0100,
    MAX_FRAME_SIZE = 256,
};

/* ---------- low-level helpers ---------- */

static void frame_begin(uint8_t *buf, size_t *pos)
{
    buf[(*pos)++] = CMD_HEADER_0;
    buf[(*pos)++] = CMD_HEADER_1;
    buf[(*pos)++] = CMD_HEADER_2;
    buf[(*pos)++] = CMD_HEADER_3;
}

static void frame_end(uint8_t *buf, size_t *pos)
{
    buf[(*pos)++] = CMD_TAIL_0;
    buf[(*pos)++] = CMD_TAIL_1;
    buf[(*pos)++] = CMD_TAIL_2;
    buf[(*pos)++] = CMD_TAIL_3;
}

static void put_u16(uint8_t *buf, size_t *pos, uint16_t val)
{
    buf[(*pos)++] = val & 0xFF;
    buf[(*pos)++] = (val >> 8) & 0xFF;
}

static void put_u32(uint8_t *buf, size_t *pos, uint32_t val)
{
    buf[(*pos)++] = val & 0xFF;
    buf[(*pos)++] = (val >> 8) & 0xFF;
    buf[(*pos)++] = (val >> 16) & 0xFF;
    buf[(*pos)++] = (val >> 24) & 0xFF;
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
    size_t pos = 0;
    frame_begin(buf, &pos);
    uint16_t data_len = 2 + value_len; /* cmd word + value */
    put_u16(buf, &pos, data_len);
    put_u16(buf, &pos, cmd);
    if (value && value_len > 0) {
        memcpy(&buf[pos], value, value_len);
        pos += value_len;
    }
    frame_end(buf, &pos);
    return pos;
}

static esp_err_t send_frame(ld2410c_handle_t *handle, const uint8_t *frame, size_t len)
{
    int written = uart_write_bytes(handle->uart_port, frame, len);
    if (written < 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t recv_ack(ld2410c_handle_t *handle, uint16_t expected_cmd, uint8_t *out_buf, size_t out_buf_size, size_t *out_len)
{
    uint8_t    buf[MAX_FRAME_SIZE];
    int        total   = 0;
    TickType_t timeout = pdMS_TO_TICKS(handle->timeout_ms);

    /* Read until we get a complete frame or timeout */
    while (total < (int)sizeof(buf)) {
        int n = uart_read_bytes(handle->uart_port, &buf[total], sizeof(buf) - total, timeout);
        if (n <= 0) {
            break;
        }
        total += n;

        /* Check if we have a complete frame */
        if (total >= 10) {
            /* Find header */
            int hdr = -1;
            for (int i = 0; i <= total - 4; i++) {
                if (buf[i] == CMD_HEADER_0 && buf[i + 1] == CMD_HEADER_1 && buf[i + 2] == CMD_HEADER_2 && buf[i + 3] == CMD_HEADER_3) {
                    hdr = i;
                    break;
                }
            }
            if (hdr < 0) continue;

            int remaining = total - hdr;
            if (remaining < 10) continue;

            uint16_t data_len   = get_u16(buf, hdr + 4);
            int      frame_size = 4 + 2 + data_len + 4; /* header + len + data + tail */

            if (remaining < frame_size) continue;

            /* Verify tail */
            int tail_off = hdr + 4 + 2 + data_len;
            if (buf[tail_off] != CMD_TAIL_0 || buf[tail_off + 1] != CMD_TAIL_1 || buf[tail_off + 2] != CMD_TAIL_2 || buf[tail_off + 3] != CMD_TAIL_3) {
                ESP_LOGE(TAG, "Invalid tail in ACK");
                return ESP_ERR_INVALID_RESPONSE;
            }

            /* Verify ACK command word */
            uint16_t ack_cmd = get_u16(buf, hdr + 6);
            if (ack_cmd != (expected_cmd | ACK_BIT)) {
                ESP_LOGE(TAG, "Unexpected ACK cmd: 0x%04X (expected 0x%04X)", ack_cmd, expected_cmd | ACK_BIT);
                return ESP_ERR_INVALID_RESPONSE;
            }

            /* Check status */
            uint16_t status = get_u16(buf, hdr + 8);
            if (status != 0) {
                ESP_LOGE(TAG, "Command 0x%04X failed, status: %d", expected_cmd, status);
                return ESP_FAIL;
            }

            /* Copy return value data (after cmd_word + status) */
            if (out_buf && out_buf_size > 0) {
                size_t val_len  = data_len > 4 ? data_len - 4 : 0; /* subtract cmd(2) + status(2) */
                size_t copy_len = val_len < out_buf_size ? val_len : out_buf_size;
                memcpy(out_buf, &buf[hdr + 10], copy_len);
                if (out_len) *out_len = copy_len;
            }

            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "Timeout waiting for ACK to cmd 0x%04X", expected_cmd);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t send_command(ld2410c_handle_t *handle, uint16_t cmd, const uint8_t *value, size_t value_len, uint8_t *ret_buf, size_t ret_buf_size,
                              size_t *ret_len)
{
    uint8_t frame[MAX_FRAME_SIZE];
    size_t  frame_len = build_frame(frame, cmd, value, value_len);

    /* Flush RX before sending */
    uart_flush_input(handle->uart_port);

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
    ld2410c_handle_t *handle = malloc(sizeof(ld2410c_handle_t));
    if (!handle) {
        return NULL;
    }
    handle->uart_port  = port;
    handle->timeout_ms = timeout_ms;
    return handle;
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
    uint8_t value[18];
    size_t  pos = 0;
    /* max motion distance gate word + value */
    put_u16(value, &pos, 0x0000);
    put_u32(value, &pos, max_moving_gate);
    /* max stationary distance gate word + value */
    put_u16(value, &pos, 0x0001);
    put_u32(value, &pos, max_stationary_gate);
    /* no-one duration word + value */
    put_u16(value, &pos, 0x0002);
    put_u32(value, &pos, no_one_duration_s);

    return send_command(handle, CMD_SET_MAX_GATE, value, pos, NULL, 0, NULL);
}

esp_err_t ld2410c_read_params(ld2410c_handle_t *handle, ld2410c_params_t *params)
{
    uint8_t   ret[64];
    size_t    ret_len = 0;
    esp_err_t err     = send_command(handle, CMD_READ_PARAMS, NULL, 0, ret, sizeof(ret), &ret_len);
    if (err != ESP_OK) return err;

    /* ret[0] = 0xAA header, ret[1] = max distance gate N */
    if (ret_len < 4 || ret[0] != 0xAA) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t max_gate            = ret[1];
    params->max_moving_gate     = ret[2];
    params->max_stationary_gate = ret[3];

    uint8_t num_gates = max_gate + 1;
    size_t  expected  = 4 + num_gates + num_gates + 2; /* header+gates+mov_sens+stat_sens+duration */
    if (ret_len < expected) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (int i = 0; i < num_gates && i < LD2410C_MAX_DISTANCE_GATES; i++) {
        params->moving_sensitivity[i] = ret[4 + i];
    }
    for (int i = 0; i < num_gates && i < LD2410C_MAX_DISTANCE_GATES; i++) {
        params->stationary_sensitivity[i] = ret[4 + num_gates + i];
    }
    params->no_one_duration = get_u16(ret, 4 + num_gates + num_gates);

    return ESP_OK;
}

esp_err_t ld2410c_set_gate_sensitivity(ld2410c_handle_t *handle, uint16_t gate, uint8_t moving_sensitivity, uint8_t stationary_sensitivity)
{
    uint8_t value[18];
    size_t  pos = 0;
    put_u16(value, &pos, 0x0000);
    put_u32(value, &pos, gate);
    put_u16(value, &pos, 0x0001);
    put_u32(value, &pos, moving_sensitivity);
    put_u16(value, &pos, 0x0002);
    put_u32(value, &pos, stationary_sensitivity);

    return send_command(handle, CMD_SET_GATE_SENS, value, pos, NULL, 0, NULL);
}

esp_err_t ld2410c_set_all_gate_sensitivity(ld2410c_handle_t *handle, uint8_t moving_sensitivity, uint8_t stationary_sensitivity)
{
    return ld2410c_set_gate_sensitivity(handle, 0xFFFF, moving_sensitivity, stationary_sensitivity);
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
    uint8_t   ret[8];
    size_t    ret_len = 0;
    esp_err_t err     = send_command(handle, CMD_READ_FW_VER, NULL, 0, ret, sizeof(ret), &ret_len);
    if (err != ESP_OK) return err;

    if (ret_len < 8) return ESP_ERR_INVALID_SIZE;

    ver->firmware_type = get_u16(ret, 0);
    ver->major         = ret[2];
    ver->minor         = ret[3];
    ver->patch         = get_u32(ret, 4);

    return ESP_OK;
}

esp_err_t ld2410c_set_baud_rate(ld2410c_handle_t *handle, ld2410c_baud_t baud)
{
    uint8_t value[2];
    size_t  pos = 0;
    put_u16(value, &pos, (uint16_t)baud);
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
    size_t  pos = 0;
    put_u16(value, &pos, enable ? 0x0001 : 0x0000);
    return send_command(handle, CMD_SET_BT, value, pos, NULL, 0, NULL);
}

esp_err_t ld2410c_get_mac_address(ld2410c_handle_t *handle, uint8_t mac[6])
{
    uint8_t   value[2] = {0x01, 0x00};
    uint8_t   ret[8];
    size_t    ret_len = 0;
    esp_err_t err     = send_command(handle, CMD_GET_MAC, value, sizeof(value), ret, sizeof(ret), &ret_len);
    if (err != ESP_OK) return err;

    if (ret_len < 7) return ESP_ERR_INVALID_SIZE;

    /* ret[0] = type (0x00), ret[1..6] = MAC (big-endian) */
    memcpy(mac, &ret[1], 6);
    return ESP_OK;
}

esp_err_t ld2410c_set_bluetooth_password(ld2410c_handle_t *handle, const char password[6])
{
    return send_command(handle, CMD_SET_BT_PASS, (const uint8_t *)password, 6, NULL, 0, NULL);
}

esp_err_t ld2410c_set_distance_resolution(ld2410c_handle_t *handle, ld2410c_resolution_t res)
{
    uint8_t value[2];
    size_t  pos = 0;
    put_u16(value, &pos, (uint16_t)res);
    return send_command(handle, CMD_SET_RESOLUTION, value, pos, NULL, 0, NULL);
}

esp_err_t ld2410c_get_distance_resolution(ld2410c_handle_t *handle, ld2410c_resolution_t *res)
{
    uint8_t   ret[4];
    size_t    ret_len = 0;
    esp_err_t err     = send_command(handle, CMD_GET_RESOLUTION, NULL, 0, ret, sizeof(ret), &ret_len);
    if (err != ESP_OK) return err;

    if (ret_len < 2) return ESP_ERR_INVALID_SIZE;
    *res = (ld2410c_resolution_t)get_u16(ret, 0);
    return ESP_OK;
}

esp_err_t ld2410c_set_aux_control(ld2410c_handle_t *handle, const ld2410c_aux_ctrl_t *ctrl)
{
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
    uint8_t   ret[4];
    size_t    ret_len = 0;
    esp_err_t err     = send_command(handle, CMD_GET_AUX_CTRL, NULL, 0, ret, sizeof(ret), &ret_len);
    if (err != ESP_OK) return err;

    if (ret_len < 3) return ESP_ERR_INVALID_SIZE;
    ctrl->mode        = (ld2410c_light_ctrl_mode_t)ret[0];
    ctrl->threshold   = ret[1];
    ctrl->out_default = (ld2410c_out_level_t)ret[2];
    return ESP_OK;
}

esp_err_t ld2410c_start_noise_detection(ld2410c_handle_t *handle, uint16_t duration_s)
{
    uint8_t value[2];
    size_t  pos = 0;
    put_u16(value, &pos, duration_s);
    return send_command(handle, CMD_START_NOISE_DET, value, pos, NULL, 0, NULL);
}

esp_err_t ld2410c_query_noise_detection_status(ld2410c_handle_t *handle, ld2410c_noise_status_t *status)
{
    uint8_t   ret[4];
    size_t    ret_len = 0;
    esp_err_t err     = send_command(handle, CMD_QUERY_NOISE_DET, NULL, 0, ret, sizeof(ret), &ret_len);
    if (err != ESP_OK) return err;

    if (ret_len < 2) return ESP_ERR_INVALID_SIZE;
    *status = (ld2410c_noise_status_t)get_u16(ret, 0);
    return ESP_OK;
}

/* ---------- data frame parsing ---------- */

static const uint8_t data_header[] = {DATA_HEADER_0, DATA_HEADER_1, DATA_HEADER_2, DATA_HEADER_3};
static const uint8_t data_tail[]   = {DATA_TAIL_0, DATA_TAIL_1, DATA_TAIL_2, DATA_TAIL_3};

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

esp_err_t ld2410c_parse_target_data(const uint8_t *frame, size_t len, ld2410c_target_data_t *data)
{
    size_t start, flen;
    if (find_data_frame(frame, len, &start, &flen) != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const uint8_t *d        = &frame[start + 6]; /* skip header(4) + length(2) */
    uint16_t       data_len = get_u16(frame, start + 4);

    /* data_type(1) + head(1) + target(9) + tail(1) + check(1) = 13 */
    if (data_len < 13) return ESP_ERR_INVALID_SIZE;

    uint8_t data_type = d[0];
    if (data_type != 0x02 && data_type != 0x01) {
        return ESP_ERR_INVALID_ARG;
    }

    /* d[1] = 0xAA head */
    data->state                  = (ld2410c_target_state_t)d[2];
    data->moving_distance_cm     = get_u16(d, 3);
    data->moving_energy          = d[5];
    data->stationary_distance_cm = get_u16(d, 6);
    data->stationary_energy      = d[8];
    data->detection_distance_cm  = get_u16(d, 9);

    return ESP_OK;
}

esp_err_t ld2410c_parse_engineering_data(const uint8_t *frame, size_t len, ld2410c_engineering_data_t *data)
{
    size_t start, flen;
    if (find_data_frame(frame, len, &start, &flen) != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const uint8_t *d        = &frame[start + 6];
    uint16_t       data_len = get_u16(frame, start + 4);

    if (d[0] != 0x01) {
        return ESP_ERR_INVALID_ARG; /* not engineering mode data */
    }

    /* Parse basic target data first */
    data->basic.state                  = (ld2410c_target_state_t)d[2];
    data->basic.moving_distance_cm     = get_u16(d, 3);
    data->basic.moving_energy          = d[5];
    data->basic.stationary_distance_cm = get_u16(d, 6);
    data->basic.stationary_energy      = d[8];
    data->basic.detection_distance_cm  = get_u16(d, 9);

    /* Engineering extra data starts at d[11] */
    size_t off = 11;
    if (data_len < off + 2) return ESP_ERR_INVALID_SIZE;

    data->max_moving_gate     = d[off++];
    data->max_stationary_gate = d[off++];

    uint8_t num_gates = data->max_moving_gate + 1;
    if (num_gates > LD2410C_MAX_DISTANCE_GATES) {
        num_gates = LD2410C_MAX_DISTANCE_GATES;
    }

    if (data_len < off + num_gates * 2 + 2) return ESP_ERR_INVALID_SIZE;

    for (int i = 0; i < num_gates; i++) {
        data->moving_gate_energy[i] = d[off++];
    }
    for (int i = 0; i < num_gates; i++) {
        data->stationary_gate_energy[i] = d[off++];
    }

    data->photosensitive = d[off++];
    data->out_pin_state  = d[off++];

    return ESP_OK;
}

/* ---------- high-level functions ---------- */

/* Helper: run a block of commands inside enable_config / end_config */
#define CONFIG_BEGIN(h)                                 \
    do {                                                \
        esp_err_t _err = ld2410c_enable_config(h);      \
        if (_err != ESP_OK) return _err;                \
    } while (0)

#define CONFIG_END(h)                                   \
    do {                                                \
        esp_err_t _err2 = ld2410c_end_config(h);        \
        if (_err2 != ESP_OK) return _err2;              \
    } while (0)

esp_err_t ld2410c_configure_detection(ld2410c_handle_t *handle,
                                      uint8_t max_moving_gate,
                                      uint8_t max_stationary_gate,
                                      uint16_t no_one_duration_s,
                                      uint8_t moving_sensitivity,
                                      uint8_t stationary_sensitivity)
{
    CONFIG_BEGIN(handle);

    esp_err_t err = ld2410c_set_max_gate_and_duration(handle, max_moving_gate,
                                                      max_stationary_gate, no_one_duration_s);
    if (err != ESP_OK) {
        ld2410c_end_config(handle);
        return err;
    }

    err = ld2410c_set_all_gate_sensitivity(handle, moving_sensitivity, stationary_sensitivity);
    if (err != ESP_OK) {
        ld2410c_end_config(handle);
        return err;
    }

    CONFIG_END(handle);
    return ESP_OK;
}

esp_err_t ld2410c_get_firmware_string(ld2410c_handle_t *handle, char *buf, size_t buf_size)
{
    CONFIG_BEGIN(handle);

    ld2410c_firmware_ver_t ver;
    esp_err_t err = ld2410c_read_firmware_version(handle, &ver);
    if (err != ESP_OK) {
        ld2410c_end_config(handle);
        return err;
    }

    CONFIG_END(handle);

    /* Format: V<major>.<minor>.<patch_as_decimal> e.g. "V1.07.22091516" */
    snprintf(buf, buf_size, "V%u.%02u.%08lu",
             ver.major, ver.minor, (unsigned long)ver.patch);
    return ESP_OK;
}

esp_err_t ld2410c_get_full_config(ld2410c_handle_t *handle,
                                  ld2410c_params_t *params,
                                  ld2410c_resolution_t *resolution)
{
    CONFIG_BEGIN(handle);

    esp_err_t err = ld2410c_read_params(handle, params);
    if (err != ESP_OK) {
        ld2410c_end_config(handle);
        return err;
    }

    err = ld2410c_get_distance_resolution(handle, resolution);
    if (err != ESP_OK) {
        ld2410c_end_config(handle);
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
        ld2410c_end_config(handle);
        return err;
    }

    err = ld2410c_restart(handle);
    ld2410c_end_config(handle);  /* best-effort, module is restarting */
    return err;
}

esp_err_t ld2410c_auto_calibrate(ld2410c_handle_t *handle, uint16_t duration_s, uint32_t poll_interval_ms)
{
    CONFIG_BEGIN(handle);

    esp_err_t err = ld2410c_start_noise_detection(handle, duration_s);
    if (err != ESP_OK) {
        ld2410c_end_config(handle);
        return err;
    }

    CONFIG_END(handle);

    /* Poll until detection completes */
    TickType_t poll_ticks = pdMS_TO_TICKS(poll_interval_ms);
    /* Timeout: detection duration + 15s headroom (10s startup + margin) */
    uint32_t max_polls = ((uint32_t)duration_s + 15) * 1000 / poll_interval_ms;

    for (uint32_t i = 0; i < max_polls; i++) {
        vTaskDelay(poll_ticks);

        CONFIG_BEGIN(handle);
        ld2410c_noise_status_t status;
        err = ld2410c_query_noise_detection_status(handle, &status);
        CONFIG_END(handle);

        if (err != ESP_OK) return err;

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
