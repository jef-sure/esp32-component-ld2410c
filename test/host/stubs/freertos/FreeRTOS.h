/* Host-test stub of FreeRTOS: one tick is one millisecond of mocked time */
#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
