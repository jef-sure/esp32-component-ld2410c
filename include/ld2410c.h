/**
 * @file ld2410c.h
 * @brief ESP-IDF driver for the HLK-LD2410C human presence sensing radar module.
 *
 * Implements the LD2410C serial communication protocol V1.07.
 * Provides low-level command functions (require manual enable_config/end_config),
 * high-level convenience wrappers, and data frame parsers.
 *
 * Default UART settings: 256000 baud, 8N1, no flow control.
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
 */
typedef struct
{
    ld2410c_target_data_t basic;                                       /**< Basic target info. */
    uint8_t               max_moving_gate;                             /**< Max moving distance gate. */
    uint8_t               max_stationary_gate;                         /**< Max stationary distance gate. */
    uint8_t               moving_gate_energy[LD2410C_MAX_DISTANCE_GATES];     /**< Per-gate moving energy. */
    uint8_t               stationary_gate_energy[LD2410C_MAX_DISTANCE_GATES]; /**< Per-gate stationary energy. */
    uint8_t               photosensitive;                              /**< Light sensor value (0-255). */
    uint8_t               out_pin_state;                               /**< Current OUT pin level. */
} ld2410c_engineering_data_t;

/** Radar configuration parameters as read from the module. */
typedef struct
{
    uint8_t  max_moving_gate;                                    /**< Configured max moving gate (1-8). */
    uint8_t  max_stationary_gate;                                /**< Configured max stationary gate (1-8). */
    uint8_t  moving_sensitivity[LD2410C_MAX_DISTANCE_GATES];     /**< Per-gate moving sensitivity (0-100). */
    uint8_t  stationary_sensitivity[LD2410C_MAX_DISTANCE_GATES]; /**< Per-gate stationary sensitivity (0-100). */
    uint16_t no_one_duration;                                    /**< No-one timeout in seconds. */
} ld2410c_params_t;

/** Firmware version information. */
typedef struct
{
    uint16_t firmware_type; /**< Firmware type identifier. */
    uint8_t  major;         /**< Major version number. */
    uint8_t  minor;         /**< Minor version number. */
    uint32_t patch;         /**< Patch/build number. */
} ld2410c_firmware_ver_t;

/** Auxiliary control (light sensing + OUT pin) configuration. */
typedef struct
{
    ld2410c_light_ctrl_mode_t mode;        /**< Light control mode. */
    uint8_t                   threshold;   /**< Light threshold (0-255), default 0x80. */
    ld2410c_out_level_t       out_default; /**< OUT pin default level. */
} ld2410c_aux_ctrl_t;

/** Driver handle. Stores UART port and command timeout. */
typedef struct
{
    uart_port_t uart_port;  /**< UART port connected to the LD2410C. */
    int         timeout_ms; /**< ACK receive timeout in milliseconds. */
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
 * @param timeout_ms Timeout for receiving ACK responses (ms).
 * @return Handle pointer, or NULL on allocation failure.
 */
ld2410c_handle_t *ld2410c_init(uart_port_t port, int timeout_ms);

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

/** @brief Restart the module (cmd 0x00A3). */
esp_err_t ld2410c_restart(ld2410c_handle_t *handle);

/**
 * @brief Enable or disable Bluetooth (cmd 0x00A4). Takes effect after restart.
 * @param enable true to enable, false to disable. Default: enabled.
 */
esp_err_t ld2410c_set_bluetooth(ld2410c_handle_t *handle, bool enable);

/**
 * @brief Query the module's MAC address (cmd 0x00A5).
 * @param[out] mac 6-byte MAC address in big-endian order.
 */
esp_err_t ld2410c_get_mac_address(ld2410c_handle_t *handle, uint8_t mac[6]);

/**
 * @brief Set the Bluetooth password (cmd 0x00A9).
 * @param password 6-character password. Default: "HiLink".
 */
esp_err_t ld2410c_set_bluetooth_password(ld2410c_handle_t *handle, const char password[6]);

/** @brief Set distance resolution (cmd 0x00AA). Takes effect after restart. */
esp_err_t ld2410c_set_distance_resolution(ld2410c_handle_t *handle, ld2410c_resolution_t res);

/**
 * @brief Query current distance resolution (cmd 0x00AB).
 * @param[out] res Current resolution setting.
 */
esp_err_t ld2410c_get_distance_resolution(ld2410c_handle_t *handle, ld2410c_resolution_t *res);

/** @brief Configure light-sensing auxiliary control for the OUT pin (cmd 0x00AD). */
esp_err_t ld2410c_set_aux_control(ld2410c_handle_t *handle, const ld2410c_aux_ctrl_t *ctrl);

/**
 * @brief Query current auxiliary control configuration (cmd 0x00AE).
 * @param[out] ctrl Current settings.
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
 */
esp_err_t ld2410c_query_noise_detection_status(ld2410c_handle_t *handle, ld2410c_noise_status_t *status);

/* ========================================================================== */
/*  Data frame parsing                                                        */
/* ========================================================================== */

/**
 * @brief Parse a basic target data frame (normal or engineering mode).
 *
 * Searches the buffer for a valid data frame (header F4 F3 F2 F1)
 * and extracts target state, distances, and energy values.
 *
 * @param frame Raw UART receive buffer.
 * @param len   Number of bytes in the buffer.
 * @param[out] data Parsed target data.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no valid frame found.
 */
esp_err_t ld2410c_parse_target_data(const uint8_t *frame, size_t len, ld2410c_target_data_t *data);

/**
 * @brief Parse an engineering mode data frame.
 *
 * Extracts basic target data plus per-gate energy values,
 * photosensitive reading, and OUT pin state.
 *
 * @param frame Raw UART receive buffer.
 * @param len   Number of bytes in the buffer.
 * @param[out] data Parsed engineering data.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if frame is not engineering mode.
 */
esp_err_t ld2410c_parse_engineering_data(const uint8_t *frame, size_t len, ld2410c_engineering_data_t *data);

/* ========================================================================== */
/*  High-level convenience functions (auto-wrap enable_config/end_config)     */
/* ========================================================================== */

/**
 * @brief Configure detection range, timeout, and uniform sensitivity in one call.
 *
 * Automatically enters/exits configuration mode.
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
 * @param      buf_size Size of the buffer (recommend >= 24).
 */
esp_err_t ld2410c_get_firmware_string(ld2410c_handle_t *handle, char *buf, size_t buf_size);

/**
 * @brief Read all parameters and distance resolution in one config session.
 *
 * @param[out] params     Current gate/sensitivity/duration configuration.
 * @param[out] resolution Current distance resolution.
 */
esp_err_t ld2410c_get_full_config(ld2410c_handle_t *handle, ld2410c_params_t *params, ld2410c_resolution_t *resolution);

/** @brief Factory reset and restart the module in one call. */
esp_err_t ld2410c_factory_reset_and_restart(ld2410c_handle_t *handle);

/**
 * @brief Run auto noise calibration and block until complete.
 *
 * Starts noise detection, then polls the status at the given interval.
 * Everyone must leave the detection area before calling this function.
 *
 * @param duration_s       Detection duration in seconds.
 * @param poll_interval_ms Polling interval in milliseconds.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if calibration did not complete.
 */
esp_err_t ld2410c_auto_calibrate(ld2410c_handle_t *handle, uint16_t duration_s, uint32_t poll_interval_ms);
