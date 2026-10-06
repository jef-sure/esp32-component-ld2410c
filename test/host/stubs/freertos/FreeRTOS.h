/* Host-test stub of FreeRTOS: one tick is one millisecond of mocked time */
#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;

/* Milliseconds per tick of the mocked clock (1 by default, set by the tests to model e.g. a 100 Hz tick) */
extern uint32_t mock_ms_per_tick;

#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / mock_ms_per_tick))
