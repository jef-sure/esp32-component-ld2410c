#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ld2410c.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "main";

enum
{
    UART_PORT_NUM  = UART_NUM_1,
    UART_BAUD_RATE = 256000,
    UART_TX_PIN    = 17,
    UART_RX_PIN    = 16,
    UART_BUF_SIZE  = 256,
    LD2410C_OUT_PIN = 4,
};

static ld2410c_handle_t *s_ld;
static QueueHandle_t s_out_pin_queue;

static void uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate  = UART_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void IRAM_ATTR out_pin_isr_handler(void *arg)
{
    uint32_t level = gpio_get_level(LD2410C_OUT_PIN);
    xQueueSendFromISR(s_out_pin_queue, &level, NULL);
}

static void out_pin_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << LD2410C_OUT_PIN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLDOWN_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    s_out_pin_queue = xQueueCreate(4, sizeof(uint32_t));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(LD2410C_OUT_PIN, out_pin_isr_handler, NULL));
}

static const char *target_state_str(ld2410c_target_state_t state)
{
    switch (state) {
    case LD2410C_TARGET_NONE:       return "NONE";
    case LD2410C_TARGET_MOVING:     return "MOVING";
    case LD2410C_TARGET_STATIONARY: return "STATIONARY";
    case LD2410C_TARGET_BOTH:       return "MOVING+STATIONARY";
    default:                        return "UNKNOWN";
    }
}

static void presence_monitor_task(void *arg)
{
    uint8_t               buf[UART_BUF_SIZE];
    ld2410c_target_data_t target;
    ld2410c_target_state_t last_state = LD2410C_TARGET_NONE;
    uint32_t out_level;

    ESP_LOGI(TAG, "Presence monitor started");

    while (1) {
        /* Drain OUT pin edge events */
        while (xQueueReceive(s_out_pin_queue, &out_level, 0) == pdTRUE) {
            ESP_LOGW(TAG, "OUT pin: %s", out_level ? "PRESENCE" : "NO PRESENCE");
        }

        int len = uart_read_bytes(UART_PORT_NUM, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        if (ld2410c_parse_target_data(buf, len, &target) != ESP_OK) continue;

        if (target.state != last_state) {
            ESP_LOGW(TAG, "State changed: %s -> %s",
                     target_state_str(last_state), target_state_str(target.state));
            last_state = target.state;
        }

        if (target.state != LD2410C_TARGET_NONE) {
            ESP_LOGI(TAG, "[%s] mov=%dcm energy=%d%% | stat=%dcm energy=%d%% | det=%dcm",
                     target_state_str(target.state),
                     target.moving_distance_cm, target.moving_energy,
                     target.stationary_distance_cm, target.stationary_energy,
                     target.detection_distance_cm);
        }
    }
}

static void print_config(void)
{
    ld2410c_params_t     params;
    ld2410c_resolution_t res;

    if (ld2410c_get_full_config(s_ld, &params, &res) == ESP_OK) {
        ESP_LOGI(TAG, "--- Current Configuration ---");
        ESP_LOGI(TAG, "Resolution: %s", res == LD2410C_RESOLUTION_020M ? "0.2m" : "0.75m");
        ESP_LOGI(TAG, "Max moving gate: %d, max stationary gate: %d",
                 params.max_moving_gate, params.max_stationary_gate);
        ESP_LOGI(TAG, "No-one duration: %ds", params.no_one_duration);

        ESP_LOGI(TAG, "Gate sensitivities (moving / stationary):");
        for (int i = 0; i < LD2410C_MAX_DISTANCE_GATES; i++) {
            ESP_LOGI(TAG, "  Gate %d: %3d / %3d", i,
                     params.moving_sensitivity[i],
                     params.stationary_sensitivity[i]);
        }
    } else {
        ESP_LOGE(TAG, "Failed to read config");
    }
}

void app_main(void)
{
    uart_init();
    out_pin_init();
    s_ld = ld2410c_init(UART_PORT_NUM, 1000);

    /* Give the module time to start up */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Print firmware version */
    char fw_str[32];
    if (ld2410c_get_firmware_string(s_ld, fw_str, sizeof(fw_str)) == ESP_OK) {
        ESP_LOGI(TAG, "Firmware: %s", fw_str);
    }

    /* Print current configuration */
    print_config();

    /* Configure: max 6 gates (~4.5m at 0.75m resolution), 10s no-one timeout,
       sensitivity 40 for moving, 30 for stationary */
    esp_err_t err = ld2410c_configure_detection(s_ld, 6, 6, 10, 40, 30);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Detection configured: 6 gates, 10s timeout, sens 40/30");
    } else {
        ESP_LOGE(TAG, "Failed to configure detection: %s", esp_err_to_name(err));
    }

    /* Start continuous presence monitoring */
    xTaskCreate(presence_monitor_task, "presence", 4096, NULL, 10, NULL);
}
