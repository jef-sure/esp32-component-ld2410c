/*
 * Host unit tests for the LD2410C driver.
 *
 * Frames marked "protocol example" are copied from the HLK-LD2410C serial
 * communication protocol V1.07.
 */
#include "ld2410c.h"
#include "mock_uart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_checks, s_failures;

#define CHECK(cond)                                                    \
    do {                                                               \
        s_checks++;                                                    \
        if (!(cond)) {                                                 \
            s_failures++;                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                              \
    } while (0)

#define CHECK_EQ(actual, expected)                                                          \
    do {                                                                                    \
        long _a = (long)(actual), _e = (long)(expected);                                    \
        s_checks++;                                                                         \
        if (_a != _e) {                                                                     \
            s_failures++;                                                                   \
            printf("  FAIL %s:%d: %s = %ld (0x%lX), expected %s = %ld (0x%lX)\n", __FILE__, \
                   __LINE__, #actual, _a, (unsigned long)_a, #expected, _e, (unsigned long)_e); \
        }                                                                                   \
    } while (0)

#define RUN(test)                \
    do {                         \
        printf("%s\n", #test);   \
        mock_reset();            \
        test();                  \
    } while (0)

enum { TIMEOUT_MS = 1000 };

/* ---------- frames ---------- */

static const uint8_t ACK_ENABLE[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x01, 0x00, 0x40, 0x00, 0x04, 0x03, 0x02, 0x01};
static const uint8_t ACK_END[]    = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFE, 0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};

/* Protocol example, 2.2.8: V1.07.22091516 */
static const uint8_t ACK_FIRMWARE[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x0C, 0x00, 0xA0, 0x01, 0x00, 0x00, 0x00, 0x01,
                                       0x07, 0x01, 0x16, 0x15, 0x09, 0x22, 0x04, 0x03, 0x02, 0x01};

/* Protocol example, 2.2.13: MAC 8F 27 2E B8 0F 65 */
static const uint8_t ACK_MAC[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x0A, 0x00, 0xA5, 0x01, 0x00, 0x00,
                                  0x8F, 0x27, 0x2E, 0xB8, 0x0F, 0x65, 0x04, 0x03, 0x02, 0x01};

/* Protocol example, 2.2.4: gates 8/8/8, moving sensitivity 20, stationary 25, duration 5 s */
static const uint8_t ACK_PARAMS[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x1C, 0x00, 0x61, 0x01, 0x00, 0x00, 0xAA, 0x08, 0x08,
                                     0x08, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x19, 0x19, 0x19,
                                     0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x05, 0x00, 0x04, 0x03, 0x02, 0x01};

static const uint8_t ACK_GATE_SENS[]   = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0x64, 0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
static const uint8_t ACK_NOISE_START[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0x0B, 0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
/* Protocol example, 2.2.21: detection in progress */
static const uint8_t ACK_NOISE_BUSY[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x06, 0x00, 0x1B, 0x01, 0x00, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
static const uint8_t ACK_NOISE_DONE[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x06, 0x00, 0x1B, 0x01, 0x00, 0x00, 0x02, 0x00, 0x04, 0x03, 0x02, 0x01};

/* Protocol example, 2.3.2: data reported in normal operating mode */
static const uint8_t FRAME_BASIC[] = {0xF4, 0xF3, 0xF2, 0xF1, 0x0D, 0x00, 0x02, 0xAA, 0x02, 0x51, 0x00, 0x00,
                                      0x00, 0x00, 0x3B, 0x00, 0x00, 0x55, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};

/* Protocol example, 2.3.2: data reported in engineering mode */
static const uint8_t FRAME_ENG[] = {0xF4, 0xF3, 0xF2, 0xF1, 0x23, 0x00, 0x01, 0xAA, 0x03, 0x1E, 0x00, 0x3C, 0x00, 0x00, 0x39,
                                    0x00, 0x00, 0x08, 0x08, 0x3C, 0x22, 0x05, 0x03, 0x03, 0x04, 0x03, 0x06, 0x05, 0x00, 0x00,
                                    0x39, 0x10, 0x13, 0x06, 0x06, 0x08, 0x04, 0x03, 0x05, 0x55, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};

#define QUEUE(frame) mock_queue_response(frame, sizeof(frame))

static ld2410c_handle_t *new_handle(void)
{
    ld2410c_handle_t *ld = ld2410c_init(UART_NUM_1, TIMEOUT_MS);
    if (!ld) {
        printf("ld2410c_init failed\n");
        exit(1);
    }
    return ld;
}

/* Copy to an exact-size heap block so AddressSanitizer catches any overread */
static uint8_t *heap_copy(const uint8_t *src, size_t len)
{
    uint8_t *p = malloc(len);
    memcpy(p, src, len);
    return p;
}

/*
 * Engineering frame with gate_bytes bytes of per-gate energies (2 * gates for a
 * well-formed frame): energy byte i is 0x10 + i, photosensitive 0x77, OUT 0x01.
 * The basic part comes from the protocol example. Returns the frame length.
 */
static size_t make_eng_frame(uint8_t *f, size_t gate_bytes, uint8_t max_moving, uint8_t max_stationary)
{
    size_t data_len = 17 + gate_bytes;

    memcpy(f, FRAME_ENG, 4);
    f[4] = (uint8_t)data_len;
    f[5] = 0x00;
    memcpy(&f[6], &FRAME_ENG[6], 11); /* data type, head, basic target data */
    f[17] = max_moving;
    f[18] = max_stationary;
    for (size_t i = 0; i < gate_bytes; i++) {
        f[19 + i] = (uint8_t)(0x10 + i);
    }
    size_t pos = 19 + gate_bytes;
    f[pos++]   = 0x77;
    f[pos++]   = 0x01;
    f[pos++]   = 0x55;
    f[pos++]   = 0x00;
    memcpy(&f[pos], &FRAME_ENG[sizeof(FRAME_ENG) - 4], 4);
    return pos + 4;
}

/* Parse from an exact-size heap block so AddressSanitizer catches any overread */
static esp_err_t parse_eng_exact(const uint8_t *f, size_t len, ld2410c_engineering_data_t *e)
{
    uint8_t  *p   = heap_copy(f, len);
    esp_err_t err = ld2410c_parse_engineering_data(p, len, e);
    free(p);
    return err;
}

/* ---------- protocol examples ---------- */

static void test_firmware_version(void)
{
    ld2410c_handle_t      *ld = new_handle();
    ld2410c_firmware_ver_t ver;

    QUEUE(ACK_FIRMWARE);
    CHECK_EQ(ld2410c_read_firmware_version(ld, &ver), ESP_OK);
    CHECK_EQ(ver.major, 0x01);
    CHECK_EQ(ver.minor, 0x07);
    CHECK_EQ(ver.patch, 0x22091516);

    char str[32];
    mock_reset();
    QUEUE(ACK_ENABLE);
    QUEUE(ACK_FIRMWARE);
    QUEUE(ACK_END);
    CHECK_EQ(ld2410c_get_firmware_string(ld, str, sizeof(str)), ESP_OK);
    CHECK(strcmp(str, "V1.07.22091516") == 0);

    ld2410c_deinit(&ld);
    CHECK(ld == NULL);
}

static void test_mac_address(void)
{
    ld2410c_handle_t *ld = new_handle();
    uint8_t           mac[6];
    const uint8_t     expected[6] = {0x8F, 0x27, 0x2E, 0xB8, 0x0F, 0x65};

    QUEUE(ACK_MAC);
    CHECK_EQ(ld2410c_get_mac_address(ld, mac), ESP_OK);
    CHECK(memcmp(mac, expected, sizeof(mac)) == 0);

    ld2410c_deinit(&ld);
}

static void test_read_params(void)
{
    ld2410c_handle_t *ld = new_handle();
    ld2410c_params_t  params;

    QUEUE(ACK_PARAMS);
    CHECK_EQ(ld2410c_read_params(ld, &params), ESP_OK);
    CHECK_EQ(params.max_moving_gate, 8);
    CHECK_EQ(params.max_stationary_gate, 8);
    for (int i = 0; i < LD2410C_MAX_DISTANCE_GATES; i++) {
        CHECK_EQ(params.moving_sensitivity[i], 20);
        CHECK_EQ(params.stationary_sensitivity[i], 25);
    }
    CHECK_EQ(params.no_one_duration, 5);

    ld2410c_deinit(&ld);
}

static void test_command_encoding(void)
{
    /* Protocol example, 2.2.7: gate 3, motion sensitivity 40, stationary sensitivity 40 */
    static const uint8_t expected[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x14, 0x00, 0x64, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01,
                                       0x00, 0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x28, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    ld2410c_handle_t    *ld         = new_handle();

    QUEUE(ACK_GATE_SENS);
    CHECK_EQ(ld2410c_set_gate_sensitivity(ld, 3, 40, 40), ESP_OK);

    size_t         tx_len;
    const uint8_t *tx = mock_tx(&tx_len);
    CHECK_EQ(tx_len, sizeof(expected));
    CHECK(tx_len == sizeof(expected) && memcmp(tx, expected, sizeof(expected)) == 0);

    ld2410c_deinit(&ld);
}

static void test_parse_basic(void)
{
    ld2410c_target_data_t t;

    CHECK_EQ(ld2410c_parse_target_data(FRAME_BASIC, sizeof(FRAME_BASIC), &t), ESP_OK);
    CHECK_EQ(t.state, LD2410C_TARGET_STATIONARY);
    CHECK_EQ(t.moving_distance_cm, 81);
    CHECK_EQ(t.moving_energy, 0);
    CHECK_EQ(t.stationary_distance_cm, 0);
    CHECK_EQ(t.stationary_energy, 59);
    CHECK_EQ(t.detection_distance_cm, 0);

    /* A basic frame is not engineering data */
    ld2410c_engineering_data_t e;
    CHECK_EQ(ld2410c_parse_engineering_data(FRAME_BASIC, sizeof(FRAME_BASIC), &e), ESP_ERR_INVALID_ARG);
}

static void test_parse_engineering(void)
{
    static const uint8_t       moving[]     = {0x3C, 0x22, 0x05, 0x03, 0x03, 0x04, 0x03, 0x06, 0x05};
    static const uint8_t       stationary[] = {0x00, 0x00, 0x39, 0x10, 0x13, 0x06, 0x06, 0x08, 0x04};
    ld2410c_engineering_data_t e;

    CHECK_EQ(ld2410c_parse_engineering_data(FRAME_ENG, sizeof(FRAME_ENG), &e), ESP_OK);
    CHECK_EQ(e.basic.state, LD2410C_TARGET_BOTH);
    CHECK_EQ(e.basic.moving_distance_cm, 30);
    CHECK_EQ(e.basic.moving_energy, 60);
    CHECK_EQ(e.basic.stationary_distance_cm, 0);
    CHECK_EQ(e.basic.stationary_energy, 57);
    CHECK_EQ(e.basic.detection_distance_cm, 0);
    CHECK_EQ(e.max_moving_gate, 8);
    CHECK_EQ(e.max_stationary_gate, 8);
    CHECK(memcmp(e.moving_gate_energy, moving, sizeof(moving)) == 0);
    CHECK(memcmp(e.stationary_gate_energy, stationary, sizeof(stationary)) == 0);
    CHECK_EQ(e.photosensitive, 0x03);
    CHECK_EQ(e.out_pin_state, 0x05);

    /* The basic part of an engineering frame is also readable on its own */
    ld2410c_target_data_t t;
    CHECK_EQ(ld2410c_parse_target_data(FRAME_ENG, sizeof(FRAME_ENG), &t), ESP_OK);
    CHECK_EQ(t.state, LD2410C_TARGET_BOTH);
}

static void test_parse_engineering_gate_count(void)
{
    uint8_t                    f[64];
    ld2410c_engineering_data_t e;
    size_t                     len;

    /* All nine gates while the module is configured for 6: the arrays must not be cut after gate 6 */
    len = make_eng_frame(f, 18, 6, 6);
    CHECK_EQ(len, 45);
    CHECK_EQ(parse_eng_exact(f, len, &e), ESP_OK);
    CHECK_EQ(e.max_moving_gate, 6);
    CHECK_EQ(e.max_stationary_gate, 6);
    for (int i = 0; i < LD2410C_MAX_DISTANCE_GATES; i++) {
        CHECK_EQ(e.moving_gate_energy[i], 0x10 + i);
        CHECK_EQ(e.stationary_gate_energy[i], 0x19 + i);
    }
    CHECK_EQ(e.photosensitive, 0x77);
    CHECK_EQ(e.out_pin_state, 0x01);

    /* A frame carrying 7 gates (max gate + 1): parsed as far as it goes, the rest is 0 */
    len = make_eng_frame(f, 14, 6, 6);
    CHECK_EQ(parse_eng_exact(f, len, &e), ESP_OK);
    for (int i = 0; i < 7; i++) {
        CHECK_EQ(e.moving_gate_energy[i], 0x10 + i);
        CHECK_EQ(e.stationary_gate_energy[i], 0x17 + i);
    }
    for (int i = 7; i < LD2410C_MAX_DISTANCE_GATES; i++) {
        CHECK_EQ(e.moving_gate_energy[i], 0);
        CHECK_EQ(e.stationary_gate_energy[i], 0);
    }
    CHECK_EQ(e.photosensitive, 0x77);
    CHECK_EQ(e.out_pin_state, 0x01);

    /* A single gate is the smallest valid frame */
    len = make_eng_frame(f, 2, 2, 2);
    CHECK_EQ(parse_eng_exact(f, len, &e), ESP_OK);
    CHECK_EQ(e.moving_gate_energy[0], 0x10);
    CHECK_EQ(e.stationary_gate_energy[0], 0x11);
    CHECK_EQ(e.moving_gate_energy[1], 0);
    CHECK_EQ(e.photosensitive, 0x77);

    /* Lengths that do not fit 1 to 9 gates: none, half a gate, an odd byte count, ten gates */
    static const size_t bad_gate_bytes[] = {0, 1, 19, 20};
    for (size_t i = 0; i < sizeof(bad_gate_bytes) / sizeof(bad_gate_bytes[0]); i++) {
        len = make_eng_frame(f, bad_gate_bytes[i], 8, 8);
        CHECK_EQ(parse_eng_exact(f, len, &e), ESP_ERR_INVALID_SIZE);
    }
}

/* ---------- malformed frames ---------- */

static void test_parse_short_frames(void)
{
    ld2410c_target_data_t      t;
    ld2410c_engineering_data_t e;

    /* Correctly framed packets whose data is too short for the fields */
    for (uint8_t data_len = 0; data_len < 13; data_len++) {
        size_t   len = 10 + data_len;
        uint8_t *f   = calloc(1, len);
        memcpy(f, FRAME_BASIC, 4);
        f[4] = data_len;
        if (data_len > 0) f[6] = 0x01;
        memcpy(&f[6 + data_len], &FRAME_BASIC[sizeof(FRAME_BASIC) - 4], 4);

        CHECK_EQ(ld2410c_parse_target_data(f, len, &t), ESP_ERR_INVALID_SIZE);
        CHECK_EQ(ld2410c_parse_engineering_data(f, len, &e), ESP_ERR_INVALID_SIZE);
        free(f);
    }

    /* Engineering type with only the basic payload: no room for the gate data */
    uint8_t *f = heap_copy(FRAME_BASIC, sizeof(FRAME_BASIC));
    f[6]       = 0x01;
    CHECK_EQ(ld2410c_parse_engineering_data(f, sizeof(FRAME_BASIC), &e), ESP_ERR_INVALID_SIZE);
    free(f);

    /* Engineering frame whose max moving gate byte is out of range */
    f     = heap_copy(FRAME_ENG, sizeof(FRAME_ENG));
    f[17] = 0x09;
    CHECK_EQ(ld2410c_parse_engineering_data(f, sizeof(FRAME_ENG), &e), ESP_ERR_INVALID_RESPONSE);
    free(f);

    /* Truncated buffers hold no complete frame */
    for (size_t len = 0; len < sizeof(FRAME_ENG); len++) {
        f = heap_copy(FRAME_ENG, len);
        CHECK_EQ(ld2410c_parse_engineering_data(f, len, &e), ESP_ERR_NOT_FOUND);
        CHECK_EQ(ld2410c_parse_target_data(f, len, &t), ESP_ERR_NOT_FOUND);
        free(f);
    }
}

static void test_parse_corrupted_envelope(void)
{
    ld2410c_target_data_t      t;
    ld2410c_engineering_data_t e;
    uint8_t                    f[sizeof(FRAME_ENG)];

    /* Offsets of head / tail / check byte in the basic frame */
    static const size_t basic_off[] = {7, 17, 18};
    for (size_t i = 0; i < 3; i++) {
        memcpy(f, FRAME_BASIC, sizeof(FRAME_BASIC));
        f[basic_off[i]] ^= 0xFF;
        CHECK_EQ(ld2410c_parse_target_data(f, sizeof(FRAME_BASIC), &t), ESP_ERR_INVALID_RESPONSE);
    }

    static const size_t eng_off[] = {7, 39, 40};
    for (size_t i = 0; i < 3; i++) {
        memcpy(f, FRAME_ENG, sizeof(FRAME_ENG));
        f[eng_off[i]] ^= 0xFF;
        CHECK_EQ(ld2410c_parse_engineering_data(f, sizeof(FRAME_ENG), &e), ESP_ERR_INVALID_RESPONSE);
        CHECK_EQ(ld2410c_parse_target_data(f, sizeof(FRAME_ENG), &t), ESP_ERR_INVALID_RESPONSE);
    }

    /* Unknown data type */
    memcpy(f, FRAME_BASIC, sizeof(FRAME_BASIC));
    f[6] = 0x07;
    CHECK_EQ(ld2410c_parse_target_data(f, sizeof(FRAME_BASIC), &t), ESP_ERR_INVALID_ARG);

    /* Broken frame tail */
    memcpy(f, FRAME_BASIC, sizeof(FRAME_BASIC));
    f[sizeof(FRAME_BASIC) - 1] = 0x00;
    CHECK_EQ(ld2410c_parse_target_data(f, sizeof(FRAME_BASIC), &t), ESP_ERR_NOT_FOUND);
}

static void test_malformed_ack(void)
{
    ld2410c_handle_t      *ld = new_handle();
    ld2410c_firmware_ver_t ver;
    uint8_t                mac[6];

    /* ACK for a different command */
    QUEUE(ACK_END);
    CHECK_EQ(ld2410c_read_firmware_version(ld, &ver), ESP_ERR_INVALID_RESPONSE);

    /* ACK without a status word */
    static const uint8_t no_status[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xA0, 0x01, 0x04, 0x03, 0x02, 0x01};
    mock_reset();
    QUEUE(no_status);
    CHECK_EQ(ld2410c_read_firmware_version(ld, &ver), ESP_ERR_INVALID_RESPONSE);

    /* Failure status */
    static const uint8_t failed[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xA0, 0x01, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    mock_reset();
    QUEUE(failed);
    CHECK_EQ(ld2410c_read_firmware_version(ld, &ver), ESP_FAIL);

    /* Successful ACK with a short return value */
    static const uint8_t short_mac[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x07, 0x00, 0xA5, 0x01, 0x00,
                                        0x00, 0x8F, 0x27, 0x2E, 0x04, 0x03, 0x02, 0x01};
    mock_reset();
    QUEUE(short_mac);
    CHECK_EQ(ld2410c_get_mac_address(ld, mac), ESP_ERR_INVALID_SIZE);

    /* Length field larger than any ACK, then nothing: bounded by the timeout */
    static const uint8_t huge[] = {0xFD, 0xFC, 0xFB, 0xFA, 0xFF, 0xFF, 0xA0, 0x01, 0x00, 0x00};
    mock_reset();
    QUEUE(huge);
    uint32_t start = mock_now();
    CHECK_EQ(ld2410c_read_firmware_version(ld, &ver), ESP_ERR_TIMEOUT);
    CHECK(mock_now() - start <= TIMEOUT_MS);

    /* No answer at all */
    mock_reset();
    start = mock_now();
    CHECK_EQ(ld2410c_read_firmware_version(ld, &ver), ESP_ERR_TIMEOUT);
    CHECK_EQ(mock_now() - start, TIMEOUT_MS);

    ld2410c_deinit(&ld);
}

/* ---------- UART behaviour ---------- */

static void test_ack_skips_data_frames_and_returns_promptly(void)
{
    ld2410c_handle_t      *ld = new_handle();
    ld2410c_firmware_ver_t ver;

    /* A report frame still in flight when the command is sent precedes the ACK */
    uint8_t resp[sizeof(FRAME_BASIC) + sizeof(ACK_FIRMWARE)];
    memcpy(resp, FRAME_BASIC, sizeof(FRAME_BASIC));
    memcpy(&resp[sizeof(FRAME_BASIC)], ACK_FIRMWARE, sizeof(ACK_FIRMWARE));
    mock_queue_response(resp, sizeof(resp));

    uint32_t start = mock_now();
    CHECK_EQ(ld2410c_read_firmware_version(ld, &ver), ESP_OK);
    CHECK_EQ(ver.patch, 0x22091516);
    /* The ACK was already there: no waiting for the timeout */
    CHECK_EQ(mock_now() - start, 0);

    ld2410c_deinit(&ld);
}

static void test_partial_write(void)
{
    ld2410c_handle_t *ld = new_handle();

    QUEUE(ACK_END);
    mock_set_write_limit(5);
    CHECK_EQ(ld2410c_end_config(ld), ESP_FAIL);

    ld2410c_deinit(&ld);
}

static void test_read_data_frame_fragmented(void)
{
    static const uint8_t  garbage[] = {0x00, 0xF4, 0xF3, 0x55, 0xF4};
    ld2410c_handle_t     *ld        = new_handle();
    ld2410c_target_data_t t;
    uint8_t               buf[64];
    size_t                len;
    const size_t          split = 10;

    /* garbage, two complete frames, and the first part of a third */
    mock_rx_feed(garbage, sizeof(garbage));
    mock_rx_feed(FRAME_BASIC, sizeof(FRAME_BASIC));
    mock_rx_feed(FRAME_ENG, sizeof(FRAME_ENG));
    mock_rx_feed(FRAME_BASIC, split);

    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_OK);
    CHECK_EQ(len, sizeof(FRAME_BASIC));
    CHECK(memcmp(buf, FRAME_BASIC, sizeof(FRAME_BASIC)) == 0);
    CHECK_EQ(ld2410c_parse_target_data(buf, len, &t), ESP_OK);

    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_OK);
    CHECK_EQ(len, sizeof(FRAME_ENG));
    CHECK(memcmp(buf, FRAME_ENG, sizeof(FRAME_ENG)) == 0);

    /* The rest of the third frame arrives later: nothing was lost in between */
    mock_rx_feed(&FRAME_BASIC[split], sizeof(FRAME_BASIC) - split);
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_OK);
    CHECK_EQ(len, sizeof(FRAME_BASIC));
    CHECK(memcmp(buf, FRAME_BASIC, sizeof(FRAME_BASIC)) == 0);

    ld2410c_deinit(&ld);
}

static void test_read_data_frame_timeout(void)
{
    ld2410c_handle_t *ld = new_handle();
    uint8_t           buf[64];
    size_t            len = 99;

    /* Silence */
    uint32_t start = mock_now();
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_ERR_TIMEOUT);
    CHECK_EQ(len, 0);
    CHECK_EQ(mock_now() - start, TIMEOUT_MS);

    /* Plenty of bytes but no frame: still a single timeout, not one per read */
    uint8_t noise[600];
    memset(noise, 0xA5, sizeof(noise));
    mock_rx_feed(noise, sizeof(noise));
    mock_rx_feed(FRAME_BASIC, sizeof(FRAME_BASIC) - 1);
    start = mock_now();
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_ERR_NOT_FOUND);
    CHECK_EQ(len, 0);
    CHECK_EQ(mock_now() - start, TIMEOUT_MS);

    /* A frame with a corrupted tail is skipped, the next one is returned.
       The read above ended in the middle of a frame, which the handle keeps: start the stream afresh. */
    uint8_t bad[sizeof(FRAME_BASIC)];
    memcpy(bad, FRAME_BASIC, sizeof(bad));
    bad[sizeof(bad) - 2] = 0x00;
    mock_reset();
    ld->rx_pending_len = 0;
    mock_rx_feed(bad, sizeof(bad));
    mock_rx_feed(FRAME_ENG, sizeof(FRAME_ENG));
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_OK);
    CHECK_EQ(len, sizeof(FRAME_ENG));

    /* A frame larger than the buffer is skipped, not written past the end */
    uint8_t *small = malloc(sizeof(FRAME_BASIC));
    mock_reset();
    mock_rx_feed(FRAME_ENG, sizeof(FRAME_ENG));
    mock_rx_feed(FRAME_BASIC, sizeof(FRAME_BASIC));
    CHECK_EQ(ld2410c_read_data_frame(ld, small, sizeof(FRAME_BASIC), &len), ESP_OK);
    CHECK_EQ(len, sizeof(FRAME_BASIC));
    CHECK(memcmp(small, FRAME_BASIC, sizeof(FRAME_BASIC)) == 0);
    free(small);

    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(FRAME_BASIC) - 1, &len), ESP_ERR_INVALID_SIZE);

    ld2410c_deinit(&ld);
}

static void test_read_data_frame_resync_after_damaged_frame(void)
{
    ld2410c_handle_t *ld = new_handle();
    uint8_t           buf[64];
    size_t            len;

    /* A frame that lost a byte on the wire: reading its declared length swallows the first byte of the next frame */
    uint8_t lost[sizeof(FRAME_BASIC) - 1];
    memcpy(lost, FRAME_BASIC, 10);
    memcpy(&lost[10], &FRAME_BASIC[11], sizeof(FRAME_BASIC) - 11);
    mock_rx_feed(lost, sizeof(lost));
    mock_rx_feed(FRAME_BASIC, sizeof(FRAME_BASIC));
    mock_rx_feed(FRAME_ENG, sizeof(FRAME_ENG));

    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_OK);
    CHECK_EQ(len, sizeof(FRAME_BASIC));
    CHECK(memcmp(buf, FRAME_BASIC, sizeof(FRAME_BASIC)) == 0);
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_OK);
    CHECK_EQ(len, sizeof(FRAME_ENG));
    CHECK(memcmp(buf, FRAME_ENG, sizeof(FRAME_ENG)) == 0);
    CHECK_EQ(ld->rx_pending_len, 0);

    /* A length field that is too large (but still fits the buffer): the frames behind it are found too,
       even those that were already read past the end of the first one */
    mock_reset();
    uint8_t bad_len[sizeof(FRAME_BASIC)];
    memcpy(bad_len, FRAME_BASIC, sizeof(bad_len));
    bad_len[4] = 0x30;
    mock_rx_feed(bad_len, sizeof(bad_len));
    mock_rx_feed(FRAME_BASIC, sizeof(FRAME_BASIC));
    mock_rx_feed(FRAME_ENG, sizeof(FRAME_ENG));

    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_OK);
    CHECK_EQ(len, sizeof(FRAME_BASIC));
    CHECK(memcmp(buf, FRAME_BASIC, sizeof(FRAME_BASIC)) == 0);
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_OK);
    CHECK_EQ(len, sizeof(FRAME_ENG));
    CHECK(memcmp(buf, FRAME_ENG, sizeof(FRAME_ENG)) == 0);
    CHECK_EQ(ld->rx_pending_len, 0);

    /* The data ends in the middle of the damaged frame: nothing is lost, the frames behind it
       are returned once the rest arrives */
    mock_reset();
    mock_rx_feed(bad_len, sizeof(bad_len));
    mock_rx_feed(FRAME_BASIC, sizeof(FRAME_BASIC));
    uint32_t start = mock_now();
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_ERR_NOT_FOUND);
    CHECK_EQ(len, 0);
    CHECK_EQ(mock_now() - start, TIMEOUT_MS);
    CHECK(ld->rx_pending_len > 0);

    mock_rx_feed(FRAME_ENG, sizeof(FRAME_ENG));
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_OK);
    CHECK_EQ(len, sizeof(FRAME_BASIC));
    CHECK(memcmp(buf, FRAME_BASIC, sizeof(FRAME_BASIC)) == 0);
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_OK);
    CHECK_EQ(len, sizeof(FRAME_ENG));
    CHECK_EQ(ld->rx_pending_len, 0);

    /* A partial header at the end of the data is kept as well, and completed later */
    mock_rx_feed(FRAME_BASIC, 2);
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_ERR_NOT_FOUND);
    mock_rx_feed(&FRAME_BASIC[2], sizeof(FRAME_BASIC) - 2);
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_OK);
    CHECK(memcmp(buf, FRAME_BASIC, sizeof(FRAME_BASIC)) == 0);

    /* Silence after that is a plain timeout again, not "data without a frame" */
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_ERR_TIMEOUT);

    /* A command starts from a clean slate: bytes held back from earlier frames are dropped with the UART input */
    mock_reset();
    mock_rx_feed(bad_len, sizeof(bad_len));
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_ERR_NOT_FOUND);
    CHECK(ld->rx_pending_len > 0);
    mock_reset();
    QUEUE(ACK_END);
    CHECK_EQ(ld2410c_end_config(ld), ESP_OK);
    CHECK_EQ(ld->rx_pending_len, 0);

    ld2410c_deinit(&ld);
}

static void test_stale_ack_is_skipped(void)
{
    ld2410c_handle_t      *ld = new_handle();
    ld2410c_firmware_ver_t ver;

    /* The late ACK of an earlier command precedes the one we wait for */
    uint8_t resp[sizeof(ACK_END) + sizeof(ACK_FIRMWARE)];
    memcpy(resp, ACK_END, sizeof(ACK_END));
    memcpy(&resp[sizeof(ACK_END)], ACK_FIRMWARE, sizeof(ACK_FIRMWARE));
    mock_queue_response(resp, sizeof(resp));

    uint32_t start = mock_now();
    CHECK_EQ(ld2410c_read_firmware_version(ld, &ver), ESP_OK);
    CHECK_EQ(ver.patch, 0x22091516);
    CHECK_EQ(mock_now() - start, 0);

    /* Only the wrong ACK ever arrives: the call waits for the right one, then reports it */
    mock_reset();
    QUEUE(ACK_END);
    start = mock_now();
    CHECK_EQ(ld2410c_read_firmware_version(ld, &ver), ESP_ERR_INVALID_RESPONSE);
    CHECK_EQ(mock_now() - start, TIMEOUT_MS);

    ld2410c_deinit(&ld);
}

static void test_timeout_shorter_than_one_tick(void)
{
    ld2410c_handle_t      *ld = ld2410c_init(UART_NUM_1, 5);
    ld2410c_firmware_ver_t ver;
    uint8_t                buf[64];
    size_t                 len;

    CHECK(ld != NULL);

    /* A tick is 10 ms (100 Hz): 5 ms rounds down to 0 ticks, but the wait must still last one tick */
    mock_ms_per_tick = 10;
    uint32_t start   = mock_now();
    CHECK_EQ(ld2410c_read_firmware_version(ld, &ver), ESP_ERR_TIMEOUT);
    CHECK_EQ(mock_now() - start, 1);

    start = mock_now();
    CHECK_EQ(ld2410c_read_data_frame(ld, buf, sizeof(buf), &len), ESP_ERR_TIMEOUT);
    CHECK_EQ(mock_now() - start, 1);

    mock_ms_per_tick = 1;
    ld2410c_deinit(&ld);
}

static void test_config_mode_is_left_again(void)
{
    ld2410c_handle_t *ld = new_handle();
    char              str[32];

    /* The ACK of enable_config is lost: the module may be in config mode, so end_config is sent anyway */
    CHECK_EQ(ld2410c_get_firmware_string(ld, str, sizeof(str)), ESP_ERR_TIMEOUT);
    CHECK_EQ(mock_write_count(), 2);
    size_t         tx_len;
    const uint8_t *tx = mock_tx(&tx_len);
    CHECK_EQ(tx_len, 14 + 12);
    CHECK(tx_len == 26 && tx[6] == 0xFF && tx[20] == 0xFE);

    /* The ACK of the final end_config is lost: it is sent once more */
    static const uint8_t junk[] = {0x00};
    mock_reset();
    QUEUE(ACK_ENABLE);
    QUEUE(ACK_FIRMWARE);
    QUEUE(junk);
    QUEUE(ACK_END);
    CHECK_EQ(ld2410c_get_firmware_string(ld, str, sizeof(str)), ESP_OK);
    CHECK_EQ(mock_write_count(), 4);

    /* If the second end_config fails as well, the error is returned */
    mock_reset();
    QUEUE(ACK_ENABLE);
    QUEUE(ACK_FIRMWARE);
    CHECK_EQ(ld2410c_get_firmware_string(ld, str, sizeof(str)), ESP_ERR_TIMEOUT);
    CHECK_EQ(mock_write_count(), 4);

    ld2410c_deinit(&ld);
}

/* ---------- parameter boundaries ---------- */

static void test_invalid_arguments(void)
{
    ld2410c_handle_t          *ld = new_handle();
    ld2410c_params_t           params;
    ld2410c_resolution_t       res;
    ld2410c_aux_ctrl_t         aux = {LD2410C_LIGHT_CTRL_OFF, 0x80, LD2410C_OUT_DEFAULT_LOW};
    ld2410c_target_data_t      t;
    ld2410c_engineering_data_t e;
    uint8_t                    buf[64];
    char                       str[32];

    CHECK(ld2410c_init(UART_NUM_1, 0) == NULL);
    CHECK(ld2410c_init(UART_NUM_1, -1) == NULL);
    CHECK(ld2410c_init(UART_NUM_MAX, TIMEOUT_MS) == NULL);
    CHECK(ld2410c_init(-1, TIMEOUT_MS) == NULL);
    ld2410c_deinit(NULL);

    /* NULL handle */
    CHECK_EQ(ld2410c_enable_config(NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_end_config(NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_read_params(NULL, &params), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_restart(NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_configure_detection(NULL, 8, 8, 5, 50, 50), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_get_firmware_string(NULL, str, sizeof(str)), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_get_full_config(NULL, &params, &res), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_factory_reset_and_restart(NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_auto_calibrate(NULL, 10, 1000), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_read_data_frame(NULL, buf, sizeof(buf), NULL), ESP_ERR_INVALID_ARG);

    /* NULL output pointers */
    CHECK_EQ(ld2410c_read_params(ld, NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_read_firmware_version(ld, NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_get_mac_address(ld, NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_set_bluetooth_password(ld, NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_get_distance_resolution(ld, NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_set_aux_control(ld, NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_get_aux_control(ld, NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_query_noise_detection_status(ld, NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_parse_target_data(NULL, 0, &t), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_parse_target_data(buf, sizeof(buf), NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_parse_engineering_data(NULL, 0, &e), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_parse_engineering_data(buf, sizeof(buf), NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_read_data_frame(ld, NULL, sizeof(buf), NULL), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_get_firmware_string(ld, NULL, sizeof(str)), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_get_firmware_string(ld, str, 0), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_get_full_config(ld, NULL, &res), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_get_full_config(ld, &params, NULL), ESP_ERR_INVALID_ARG);

    /* Gates: 2-8 for the maximum gates, 0-8 or 0xFFFF for sensitivity */
    CHECK_EQ(ld2410c_set_max_gate_and_duration(ld, 1, 8, 5), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_set_max_gate_and_duration(ld, 9, 8, 5), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_set_max_gate_and_duration(ld, 8, 1, 5), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_set_max_gate_and_duration(ld, 8, 9, 5), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_set_gate_sensitivity(ld, 9, 50, 50), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_set_gate_sensitivity(ld, 0xFFFE, 50, 50), ESP_ERR_INVALID_ARG);

    /* Sensitivity: 0-100 */
    CHECK_EQ(ld2410c_set_gate_sensitivity(ld, 0, 101, 50), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_set_gate_sensitivity(ld, 0, 50, 101), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_set_all_gate_sensitivity(ld, 255, 50), ESP_ERR_INVALID_ARG);

    /* Enums */
    CHECK_EQ(ld2410c_set_baud_rate(ld, (ld2410c_baud_t)0), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_set_baud_rate(ld, (ld2410c_baud_t)9), ESP_ERR_INVALID_ARG);
    CHECK_EQ(ld2410c_set_distance_resolution(ld, (ld2410c_resolution_t)2), ESP_ERR_INVALID_ARG);
    aux.mode = (ld2410c_light_ctrl_mode_t)3;
    CHECK_EQ(ld2410c_set_aux_control(ld, &aux), ESP_ERR_INVALID_ARG);
    aux.mode        = LD2410C_LIGHT_CTRL_OFF;
    aux.out_default = (ld2410c_out_level_t)2;
    CHECK_EQ(ld2410c_set_aux_control(ld, &aux), ESP_ERR_INVALID_ARG);

    CHECK_EQ(ld2410c_auto_calibrate(ld, 10, 0), ESP_ERR_INVALID_ARG);

    /* None of the rejected calls reached the UART */
    CHECK_EQ(mock_write_count(), 0);

    ld2410c_deinit(&ld);
}

static void test_boundary_values_accepted(void)
{
    ld2410c_handle_t *ld = new_handle();

    static const uint8_t ack_max_gate[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0x60, 0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    QUEUE(ack_max_gate);
    QUEUE(ack_max_gate);
    CHECK_EQ(ld2410c_set_max_gate_and_duration(ld, 2, 2, 0), ESP_OK);
    CHECK_EQ(ld2410c_set_max_gate_and_duration(ld, 8, 8, 65535), ESP_OK);

    mock_reset();
    QUEUE(ACK_GATE_SENS);
    mock_repeat_last_response();
    CHECK_EQ(ld2410c_set_gate_sensitivity(ld, 0, 0, 0), ESP_OK);
    CHECK_EQ(ld2410c_set_gate_sensitivity(ld, 8, 100, 100), ESP_OK);
    CHECK_EQ(ld2410c_set_all_gate_sensitivity(ld, 100, 100), ESP_OK);

    ld2410c_deinit(&ld);
}

/* ---------- auto calibration ---------- */

static void queue_calibration_start(void)
{
    QUEUE(ACK_ENABLE);
    QUEUE(ACK_NOISE_START);
    QUEUE(ACK_END);
}

static void test_auto_calibrate(void)
{
    ld2410c_handle_t *ld = new_handle();

    /* Completes on the second poll */
    queue_calibration_start();
    QUEUE(ACK_ENABLE);
    QUEUE(ACK_NOISE_BUSY);
    QUEUE(ACK_END);
    QUEUE(ACK_ENABLE);
    QUEUE(ACK_NOISE_DONE);
    QUEUE(ACK_END);
    uint32_t start = mock_now();
    CHECK_EQ(ld2410c_auto_calibrate(ld, 10, 1000), ESP_OK);
    CHECK_EQ(mock_now() - start, 2000);

    /* Interval longer than the whole timeout still polls once */
    mock_reset();
    queue_calibration_start();
    QUEUE(ACK_ENABLE);
    QUEUE(ACK_NOISE_DONE);
    QUEUE(ACK_END);
    start = mock_now();
    CHECK_EQ(ld2410c_auto_calibrate(ld, 1, 3600000), ESP_OK);
    CHECK_EQ(mock_now() - start, 16000);

    /* Never completes: times out after duration + 15 s */
    mock_reset();
    queue_calibration_start();
    for (int i = 0; i < 3; i++) {
        QUEUE(ACK_ENABLE);
        QUEUE(ACK_NOISE_BUSY);
        QUEUE(ACK_END);
    }
    start = mock_now();
    CHECK_EQ(ld2410c_auto_calibrate(ld, 0, 5000), ESP_ERR_TIMEOUT);
    CHECK_EQ(mock_now() - start, 15000);

    ld2410c_deinit(&ld);
}

static void test_auto_calibrate_errors(void)
{
    static const uint8_t ack_noise_failed[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0x1B, 0x01, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    static const uint8_t ack_noise_idle[]   = {0xFD, 0xFC, 0xFB, 0xFA, 0x06, 0x00, 0x1B, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    ld2410c_handle_t    *ld                 = new_handle();

    /* The query fails and so does leaving config mode: the error of the query is reported */
    queue_calibration_start();
    QUEUE(ACK_ENABLE);
    QUEUE(ack_noise_failed);
    CHECK_EQ(ld2410c_auto_calibrate(ld, 10, 1000), ESP_FAIL);

    /* The module says no detection is running */
    mock_reset();
    queue_calibration_start();
    QUEUE(ACK_ENABLE);
    QUEUE(ack_noise_idle);
    QUEUE(ACK_END);
    CHECK_EQ(ld2410c_auto_calibrate(ld, 10, 1000), ESP_FAIL);

    ld2410c_deinit(&ld);
}

int main(void)
{
    RUN(test_firmware_version);
    RUN(test_mac_address);
    RUN(test_read_params);
    RUN(test_command_encoding);
    RUN(test_parse_basic);
    RUN(test_parse_engineering);
    RUN(test_parse_engineering_gate_count);
    RUN(test_parse_short_frames);
    RUN(test_parse_corrupted_envelope);
    RUN(test_malformed_ack);
    RUN(test_ack_skips_data_frames_and_returns_promptly);
    RUN(test_partial_write);
    RUN(test_read_data_frame_fragmented);
    RUN(test_read_data_frame_timeout);
    RUN(test_read_data_frame_resync_after_damaged_frame);
    RUN(test_stale_ack_is_skipped);
    RUN(test_timeout_shorter_than_one_tick);
    RUN(test_config_mode_is_left_again);
    RUN(test_invalid_arguments);
    RUN(test_boundary_values_accepted);
    RUN(test_auto_calibrate);
    RUN(test_auto_calibrate_errors);

    printf("\n%d checks, %d failures\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
