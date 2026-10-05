/* Host-test stub of ESP-IDF esp_log.h: logging is discarded */
#pragma once

#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
