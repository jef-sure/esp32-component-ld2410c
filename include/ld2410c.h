/**
 * @file ld2410c.h
 * @brief ESP-IDF driver for the HLK-LD2410C human presence sensing radar module.
 *
 * Implements the LD2410C serial communication protocol V1.09.
 * Provides low-level command functions (require manual enable_config/end_config),
 * high-level convenience wrappers, and data frame parsers.
 *
 * Default UART settings: 256000 baud, 8N1, no flow control.
 *
 * All functions taking a handle return ESP_ERR_INVALID_ARG for a NULL handle,
 * NULL output pointer, or out-of-range parameter.
 *
 * Thread safety: the driver does no locking. Every command function flushes
 * the UART RX buffer and then reads the ACK from it, so commands must not run
 * concurrently with each other or with ld2410c_read_data_frame() (or any
 * other reader of the same UART). Either use the handle from a single task or
 * serialize access with your own mutex. The ld2410c_parse_*() functions touch
 * no shared state and are safe to call from any task.
 */
#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/** Maximum number of distance gates (0 through 8). */
#define LD2410C_MAX_DISTANCE_GATES 9

/** Target state reported by the radar. */
typedef enum
{
    LD2410C_TARGET_NONE       = 0x00, /**< No target detected. */
    LD2410C_TARGET_MOVING     = 0x01, /**< Moving target detected. */
    LD2410C_TARGET_STATIONARY = 0x02, /**< Stationary target detected. */
    LD2410C_TARGET_BOTH       = 0x03, /**< Both moving and stationary targets. */
    LD2410C_TARGET_NOISE_DET  = 0x04, /**< Background noise detection in progress. */
    LD2410C_TARGET_NOISE_OK   = 0x05, /**< Noise detection completed successfully. */
    LD2410C_TARGET_NOISE_FAIL = 0x06, /**< Noise detection failed. */
} ld2410c_target_state_t;

/** @brief True if the state reports a moving target (alone or together with a stationary one). */
static inline bool ld2410c_target_is_moving(ld2410c_target_state_t state)
{
    return state == LD2410C_TARGET_MOVING || state == LD2410C_TARGET_BOTH;
}

/** @brief True if the state reports a stationary target (alone or together with a moving one). */
static inline bool ld2410c_target_is_stationary(ld2410c_target_state_t state)
{
    return state == LD2410C_TARGET_STATIONARY || state == LD2410C_TARGET_BOTH;
}

/** @brief True if the state reports any target. The noise detection states (0x04 to 0x06) are not targets. */
static inline bool ld2410c_target_is_present(ld2410c_target_state_t state)
{
    return ld2410c_target_is_moving(state) || ld2410c_target_is_stationary(state);
}

/**
 * Time to wait after ld2410c_restart() or ld2410c_factory_reset_and_restart()
 * before sending the next command, in milliseconds. The vendor documents give
 * no figure; this is the delay the ESPHome ld2410 component uses.
 */
#define LD2410C_RESTART_DELAY_MS 1000

/** Serial port baud rate selection indices. Factory default: LD2410C_BAUD_256000. */
typedef enum
{
    LD2410C_BAUD_9600   = 0x0001,
    LD2410C_BAUD_19200  = 0x0002,
    LD2410C_BAUD_38400  = 0x0003,
    LD2410C_BAUD_57600  = 0x0004,
    LD2410C_BAUD_115200 = 0x0005,
    LD2410C_BAUD_230400 = 0x0006,
    LD2410C_BAUD_256000 = 0x0007,
    LD2410C_BAUD_460800 = 0x0008,
} ld2410c_baud_t;

/** Distance resolution per gate. Factory default: 0.75m. */
typedef enum
{
    LD2410C_RESOLUTION_075M = 0x0000, /**< 0.75 m per distance gate. */
    LD2410C_RESOLUTION_020M = 0x0001, /**< 0.20 m per distance gate. */
} ld2410c_resolution_t;

/** Light-sensing auxiliary control mode for the OUT pin. */
typedef enum
{
    LD2410C_LIGHT_CTRL_OFF          = 0x00, /**< OUT pin not affected by light sensing. */
    LD2410C_LIGHT_CTRL_BELOW_THRESH = 0x01, /**< Condition met when light < threshold. */
    LD2410C_LIGHT_CTRL_ABOVE_THRESH = 0x02, /**< Condition met when light > threshold. */
} ld2410c_light_ctrl_mode_t;

/** Default logic level of the OUT pin. */
typedef enum
{
    LD2410C_OUT_DEFAULT_LOW  = 0x00, /**< Low when idle, high on presence. */
    LD2410C_OUT_DEFAULT_HIGH = 0x01, /**< High when idle, low on presence. */
} ld2410c_out_level_t;

/** Background noise detection status. */
typedef enum
{
    LD2410C_NOISE_NOT_IN_PROGRESS = 0x0000,
    LD2410C_NOISE_IN_PROGRESS     = 0x0001,
    LD2410C_NOISE_COMPLETED       = 0x0002,
} ld2410c_noise_status_t;

/**
 * @brief Basic target data reported in normal operating mode.
 *
 * Parsed from data frames with header F4 F3 F2 F1 and data type 0x02.
 */
typedef struct
{
    ld2410c_target_state_t state;                 /**< Current target state. */
    uint16_t               moving_distance_cm;    /**< Distance to moving target (cm). */
    uint8_t                moving_energy;          /**< Moving target energy (0-100). */
    uint16_t               stationary_distance_cm; /**< Distance to stationary target (cm). */
    uint8_t                stationary_energy;      /**< Stationary target energy (0-100). */
    uint16_t               detection_distance_cm;  /**< Overall detection distance (cm). */
} ld2410c_target_data_t;

/**
 * @brief Extended target data reported in engineering mode.
 *
 * Includes per-gate energy values, photosensitive reading, and OUT pin state.
 * The per-gate arrays hold gates 0 to 8, filled from as many gates as the
 * frame carries (the module normally sends all nine, whatever maximum gate is
 * configured); the remaining entries are 0.
 */
typedef struct
{
    ld2410c_target_data_t basic;                                       /**< Basic target info. */
    uint8_t               max_moving_gate;                             /**< Max moving distance gate as reported in the frame. */
    uint8_t               max_stationary_gate;                         /**< Max stationary distance gate as reported in the frame. */
    uint8_t               moving_gate_energy[LD2410C_MAX_DISTANCE_GATES];     /**< Per-gate moving energy. */
    uint8_t               stationary_gate_energy[LD2410C_MAX_DISTANCE_GATES]; /**< Per-gate stationary energy. */
    uint8_t               photosensitive;                              /**< Light sensor value (0-255). */
    uint8_t               out_pin_state;                               /**< OUT pin state: 0 no one, 1 someone. */
} ld2410c_engineering_data_t;

/** Radar configuration parameters as read from the module. */
typedef struct
{
    uint8_t  max_moving_gate;                                    /**< Configured max moving gate (normally 2-8, never above 8). */
    uint8_t  max_stationary_gate;                                /**< Configured max stationary gate (normally 2-8, never above 8). */
    uint8_t  moving_sensitivity[LD2410C_MAX_DISTANCE_GATES];     /**< Per-gate moving sensitivity (0-100). */
    uint8_t  stationary_sensitivity[LD2410C_MAX_DISTANCE_GATES]; /**< Per-gate stationary sensitivity (0-100). */
    uint16_t no_one_duration;                                    /**< No-one timeout in seconds. */
} ld2410c_params_t;

/**
 * Firmware version information.
 *
 * The module encodes the fields BCD-style, so they read correctly when
 * printed as hex: major 0x01, minor 0x07, patch 0x22091516 is "V1.07.22091516".
 */
typedef struct
{
    uint16_t firmware_type; /**< Firmware type identifier. */
    uint8_t  major;         /**< Major version number (print as hex). */
    uint8_t  minor;         /**< Minor version number (print as hex). */
    uint32_t patch;         /**< Build date/time, YYMMDDHH (print as hex). */
} ld2410c_firmware_ver_t;

/** Auxiliary control (light sensing + OUT pin) configuration. */
typedef struct
{
    ld2410c_light_ctrl_mode_t mode;        /**< Light control mode. */
    uint8_t                   threshold;   /**< Light threshold (0-255), default 0x80. */
    ld2410c_out_level_t       out_default; /**< OUT pin default level. */
} ld2410c_aux_ctrl_t;

/** Size of the internal buffer for bytes that were read from the UART but still belong to the stream. */
#define LD2410C_RX_PENDING_SIZE 256

/**
 * Driver handle. Stores UART port and command timeout.
 *
 * Create it with ld2410c_init(); do not fill it in by hand. The rx_pending
 * fields are private: they hold bytes that were read while looking for a
 * frame and turned out not to belong to it (for example the start of the next
 * frame after a frame with a damaged tail), so they are not lost.
 */
typedef struct
{
    uart_port_t uart_port;  /**< UART port connected to the LD2410C. */
    int         timeout_ms; /**< ACK receive timeout in milliseconds. */

    uint8_t rx_pending[LD2410C_RX_PENDING_SIZE]; /**< Private: bytes read ahead of the parser. */
    size_t  rx_pending_len;                       /**< Private: number of valid bytes in rx_pending. */
} ld2410c_handle_t;

/* ========================================================================== */
/*  Lifecycle                                                                 */
/* ========================================================================== */

/**
 * @brief Allocate and initialize an LD2410C handle.
 *
 * UART must be initialized separately before calling any command functions.
 *
 * @param port       UART port number connected to the module.
 * @param timeout_ms Timeout for receiving an ACK or a data frame (ms), must be > 0.
 * @return Handle pointer, or NULL on invalid arguments or allocation failure.
 */
ld2410c_handle_t *ld2410c_init(uart_port_t port, int timeout_ms);

/**
 * @brief Free an LD2410C handle and set the pointer to NULL.
 *
 * Does not deinitialize UART — that must be done separately.
 *
 * @param handle Pointer to the handle variable. NULL is safe.
 */
void ld2410c_deinit(ld2410c_handle_t **handle);

/* ========================================================================== */
/*  Low-level commands (require enable_config / end_config wrapping)          */
/* ========================================================================== */

/**
 * @brief Enter configuration mode (cmd 0x00FF).
 * @note Must be called before any other configuration command.
 */
esp_err_t ld2410c_enable_config(ld2410c_handle_t *handle);

/** @brief Exit configuration mode (cmd 0x00FE). Radar resumes working mode. */
esp_err_t ld2410c_end_config(ld2410c_handle_t *handle);

/**
 * @brief Set maximum distance gates and no-one duration (cmd 0x0060).
 *
 * @param max_moving_gate     Max moving detection gate (2-8).
 * @param max_stationary_gate Max stationary detection gate (2-8).
 * @param no_one_duration_s   Seconds to wait before reporting "no one" (0-65535).
 */
esp_err_t ld2410c_set_max_gate_and_duration(ld2410c_handle_t *handle, uint8_t max_moving_gate, uint8_t max_stationary_gate, uint16_t no_one_duration_s);

/**
 * @brief Read current configuration parameters (cmd 0x0061).
 * @param[out] params Populated with current gate/sensitivity/duration settings.
 * @return ESP_ERR_INVALID_RESPONSE if the answer reports more than 9 gates, a
 *         configured max gate above the reported one or a sensitivity above 100.
 */
esp_err_t ld2410c_read_params(ld2410c_handle_t *handle, ld2410c_params_t *params);

/**
 * @brief Set sensitivity for a specific distance gate (cmd 0x0064).
 *
 * @param gate                   Gate index (0-8), or 0xFFFF for all gates.
 * @param moving_sensitivity     Moving sensitivity (0-100). 100 = ignore gate.
 * @param stationary_sensitivity Stationary sensitivity (0-100).
 */
esp_err_t ld2410c_set_gate_sensitivity(ld2410c_handle_t *handle, uint16_t gate, uint8_t moving_sensitivity, uint8_t stationary_sensitivity);

/** @brief Set uniform sensitivity for all distance gates (cmd 0x0064, gate=0xFFFF). */
esp_err_t ld2410c_set_all_gate_sensitivity(ld2410c_handle_t *handle, uint8_t moving_sensitivity, uint8_t stationary_sensitivity);

/** @brief Enable engineering mode (cmd 0x0062). Adds per-gate energy to reports. Lost on power cycle. */
esp_err_t ld2410c_enable_engineering_mode(ld2410c_handle_t *handle);

/** @brief Disable engineering mode (cmd 0x0063). */
esp_err_t ld2410c_disable_engineering_mode(ld2410c_handle_t *handle);

/**
 * @brief Read firmware version (cmd 0x00A0).
 * @param[out] ver Populated with version info.
 */
esp_err_t ld2410c_read_firmware_version(ld2410c_handle_t *handle, ld2410c_firmware_ver_t *ver);

/** @brief Set UART baud rate (cmd 0x00A1). Takes effect after restart. */
esp_err_t ld2410c_set_baud_rate(ld2410c_handle_t *handle, ld2410c_baud_t baud);

/** @brief Restore factory default settings (cmd 0x00A2). Takes effect after restart. */
esp_err_t ld2410c_factory_reset(ld2410c_handle_t *handle);

/**
 * @brief Restart the module (cmd 0x00A3).
 *
 * The module restarts after it has sent the ACK and does not answer while it
 * starts up: wait LD2410C_RESTART_DELAY_MS before the next command.
 */
esp_err_t ld2410c_restart(ld2410c_handle_t *handle);

/**
 * @brief Enable or disable Bluetooth (cmd 0x00A4). Takes effect after restart.
 * @param enable true to enable, false to disable. Default: enabled.
 */
esp_err_t ld2410c_set_bluetooth(ld2410c_handle_t *handle, bool enable);

/**
 * @brief Query the module's MAC address (cmd 0x00A5).
 *
 * With Bluetooth switched off the module answers with the placeholder
 * 08:05:04:03:02:01 instead of its address; see ld2410c_get_bluetooth().
 *
 * @param[out] mac 6-byte MAC address in big-endian order.
 */
esp_err_t ld2410c_get_mac_address(ld2410c_handle_t *handle, uint8_t mac[6]);

/**
 * @brief Find out whether Bluetooth is switched on.
 *
 * The protocol has no query for it. This reads the MAC address (cmd 0x00A5)
 * and reports Bluetooth as off when the module answers with the placeholder
 * address 08:05:04:03:02:01. The vendor documents do not describe the
 * placeholder; the ESPHome ld2410 component detects Bluetooth the same way.
 *
 * @param[out] enabled true if Bluetooth is on.
 */
esp_err_t ld2410c_get_bluetooth(ld2410c_handle_t *handle, bool *enabled);

/**
 * @brief Set the Bluetooth password (cmd 0x00A9). Takes effect after restart.
 * @param password 6-character password. Default: "HiLink".
 */
esp_err_t ld2410c_set_bluetooth_password(ld2410c_handle_t *handle, const char password[6]);

/** @brief Set distance resolution (cmd 0x00AA). Takes effect after restart. */
esp_err_t ld2410c_set_distance_resolution(ld2410c_handle_t *handle, ld2410c_resolution_t res);

/**
 * @brief Query current distance resolution (cmd 0x00AB).
 * @param[out] res Current resolution setting.
 * @return ESP_ERR_INVALID_RESPONSE if the module reports an unknown resolution index.
 */
esp_err_t ld2410c_get_distance_resolution(ld2410c_handle_t *handle, ld2410c_resolution_t *res);

/** @brief Configure light-sensing auxiliary control for the OUT pin (cmd 0x00AD). */
esp_err_t ld2410c_set_aux_control(ld2410c_handle_t *handle, const ld2410c_aux_ctrl_t *ctrl);

/**
 * @brief Query current auxiliary control configuration (cmd 0x00AE).
 * @param[out] ctrl Current settings.
 * @return ESP_ERR_INVALID_RESPONSE if the module reports an unknown mode or OUT level.
 */
esp_err_t ld2410c_get_aux_control(ld2410c_handle_t *handle, ld2410c_aux_ctrl_t *ctrl);

/**
 * @brief Start background noise detection and auto-sensitivity calibration (cmd 0x000B).
 *
 * Everyone must leave the detection area within 10 seconds of issuing this command.
 *
 * @param duration_s Detection duration in seconds.
 */
esp_err_t ld2410c_start_noise_detection(ld2410c_handle_t *handle, uint16_t duration_s);

/**
 * @brief Query noise detection status (cmd 0x001B).
 * @param[out] status Current detection status.
 * @return ESP_ERR_INVALID_RESPONSE if the module reports an unknown status.
 */
esp_err_t ld2410c_query_noise_detection_status(ld2410c_handle_t *handle, ld2410c_noise_status_t *status);

/* ========================================================================== */
/*  Data frame parsing                                                        */
/* ========================================================================== */

/**
 * @brief Parse a basic target data frame (normal or engineering mode).
 *
 * Searches the buffer for the first complete data frame (header F4 F3 F2 F1,
 * tail F8 F7 F6 F5) and extracts target state, distances, and energy values.
 *
 * @param frame Raw UART receive buffer.
 * @param len   Number of bytes in the buffer.
 * @param[out] data Parsed target data.
 * @return ESP_OK on success,
 *         ESP_ERR_NOT_FOUND if the buffer holds no complete frame,
 *         ESP_ERR_INVALID_SIZE if the frame is too short,
 *         ESP_ERR_INVALID_RESPONSE if the 0xAA head, 0x55 tail or 0x00 check byte is wrong,
 *         ESP_ERR_INVALID_ARG if the data type is unknown.
 */
esp_err_t ld2410c_parse_target_data(const uint8_t *frame, size_t len, ld2410c_target_data_t *data);

/**
 * @brief Parse an engineering mode data frame.
 *
 * Extracts basic target data plus per-gate energy values,
 * photosensitive reading, and OUT pin state.
 *
 * The number of gates in each array is taken from the length of the frame
 * (17 + 2 * gates bytes of data), not from the configured maximum gates, so a
 * module configured for fewer than 8 gates is parsed correctly. A frame with
 * 1 to 9 gates is accepted.
 *
 * @param frame Raw UART receive buffer.
 * @param len   Number of bytes in the buffer.
 * @param[out] data Parsed engineering data.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if frame is not engineering mode,
 *         ESP_ERR_INVALID_SIZE if the data length does not fit 1 to 9 gates,
 *         ESP_ERR_INVALID_RESPONSE if a reported max gate is above 8,
 *         otherwise the same errors as ld2410c_parse_target_data().
 */
esp_err_t ld2410c_parse_engineering_data(const uint8_t *frame, size_t len, ld2410c_engineering_data_t *data);

/**
 * @brief Read a raw data frame from UART.
 *
 * Blocks until a complete data frame (header F4 F3 F2 F1 ... tail F8 F7 F6 F5)
 * is received, or the handle's timeout expires (the timeout bounds the whole
 * call). On success buf holds exactly one frame, starting at buf[0], which can
 * be passed to ld2410c_parse_target_data() or ld2410c_parse_engineering_data().
 *
 * Only that frame is consumed from the stream: bytes after it stay available
 * (in the UART driver's RX buffer or in the handle), so repeated calls return
 * consecutive frames without losing data to UART fragmentation. A frame whose
 * tail is damaged is skipped without swallowing the frames behind it.
 *
 * A timeout in the middle of a frame keeps the part that was received; the
 * next call continues with it.
 *
 * @param handle   Driver handle.
 * @param buf      Buffer to receive the frame.
 * @param buf_size Size of the buffer: at least 23 for basic frames, 45 for
 *                 engineering frames (recommend >= 64). Larger frames are
 *                 skipped, as are frames longer than 256 bytes (the module
 *                 sends none).
 * @param[out] out_len Length of the frame in buf, 0 on failure. May be NULL.
 * @return ESP_OK if a complete data frame was read,
 *         ESP_ERR_NOT_FOUND if data was received but no valid frame,
 *         ESP_ERR_TIMEOUT if no data was received,
 *         ESP_ERR_INVALID_SIZE if buf_size is below 23.
 */
esp_err_t ld2410c_read_data_frame(ld2410c_handle_t *handle, uint8_t *buf, size_t buf_size, size_t *out_len);

/**
 * @brief Discard all received bytes that were not read yet.
 *
 * Clears the UART driver's RX buffer and the bytes the handle keeps from an
 * earlier read (see ld2410c_read_data_frame()). Use it instead of
 * uart_flush_input() to drop stale reports, for example after a long pause
 * between reads: uart_flush_input() alone leaves the part of a frame that is
 * kept in the handle. Command functions do this themselves.
 *
 * @param handle Driver handle.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if handle is NULL.
 */
esp_err_t ld2410c_flush_input(ld2410c_handle_t *handle);

/* ========================================================================== */
/*  High-level convenience functions (auto-wrap enable_config/end_config)     */
/* ========================================================================== */

/**
 * @brief Configure detection range, timeout, and uniform sensitivity in one call.
 *
 * Automatically enters/exits configuration mode. Out-of-range arguments are
 * rejected before anything is sent to the module.
 *
 * @param max_moving_gate        Max moving gate (2-8).
 * @param max_stationary_gate    Max stationary gate (2-8).
 * @param no_one_duration_s      No-one timeout in seconds.
 * @param moving_sensitivity     Uniform moving sensitivity for all gates (0-100).
 * @param stationary_sensitivity Uniform stationary sensitivity for all gates (0-100).
 */
esp_err_t ld2410c_configure_detection(       //
    ld2410c_handle_t *handle,                //
    uint8_t           max_moving_gate,       //
    uint8_t           max_stationary_gate,   //
    uint16_t          no_one_duration_s,     //
    uint8_t           moving_sensitivity,    //
    uint8_t           stationary_sensitivity //
);

/**
 * @brief Read firmware version as a formatted string (e.g. "V1.07.22091516").
 *
 * @param[out] buf      Destination buffer.
 * @param      buf_size Size of the buffer, at least 16 to hold any version.
 * @return ESP_ERR_INVALID_SIZE if the buffer is too small for the version; buf
 *         then holds the truncated string.
 */
esp_err_t ld2410c_get_firmware_string(ld2410c_handle_t *handle, char *buf, size_t buf_size);

/**
 * @brief Read all parameters and distance resolution in one config session.
 *
 * @param[out] params     Current gate/sensitivity/duration configuration.
 * @param[out] resolution Current distance resolution.
 */
esp_err_t ld2410c_get_full_config(ld2410c_handle_t *handle, ld2410c_params_t *params, ld2410c_resolution_t *resolution);

/**
 * @brief Factory reset and restart the module in one call.
 *
 * Wait LD2410C_RESTART_DELAY_MS before the next command, see ld2410c_restart().
 * The restart ends config mode, so no end_config is sent after it; if the
 * factory reset or the restart command fails, config mode is left as usual.
 */
esp_err_t ld2410c_factory_reset_and_restart(ld2410c_handle_t *handle);

/**
 * @brief Run auto noise calibration and block until complete.
 *
 * Starts noise detection, then polls the status at the given interval.
 * Everyone must leave the detection area before calling this function.
 *
 * @param duration_s       Detection duration in seconds.
 * @param poll_interval_ms Polling interval in milliseconds, must be > 0. Capped at
 *                         the overall timeout (duration_s + 15 s).
 * A status poll whose ACK is lost or garbled is repeated at the next interval;
 * the call gives up after three such polls in a row.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_TIMEOUT if calibration did not complete in time,
 *         ESP_FAIL if the module reports that no detection is in progress,
 *         otherwise the error of the failed command.
 */
esp_err_t ld2410c_auto_calibrate(ld2410c_handle_t *handle, uint16_t duration_s, uint32_t poll_interval_ms);
