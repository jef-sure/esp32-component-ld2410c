/* Host-test stub of the ESP-IDF UART driver, implemented by mock_uart.c */
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stddef.h>
#include <stdint.h>

typedef int uart_port_t;

#define UART_NUM_0   0
#define UART_NUM_1   1
#define UART_NUM_MAX 3

int       uart_write_bytes(uart_port_t port, const void *src, size_t size);
int       uart_read_bytes(uart_port_t port, void *buf, uint32_t length, TickType_t ticks_to_wait);
esp_err_t uart_flush_input(uart_port_t port);
