#include "mock_uart.h"
#include "driver/uart.h"
#include "freertos/task.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>

enum
{
    RX_SIZE       = 8192,
    TX_SIZE       = 8192,
    MAX_RESPONSES = 64,
    RESPONSE_SIZE = 512,
};

static uint8_t    s_rx[RX_SIZE];
static size_t     s_rx_len, s_rx_pos;
static uint8_t    s_tx[TX_SIZE];
static size_t     s_tx_len, s_writes;
static uint8_t    s_resp[MAX_RESPONSES][RESPONSE_SIZE];
static size_t     s_resp_len[MAX_RESPONSES];
static size_t     s_resp_count, s_resp_next;
static bool       s_repeat_last;
static int        s_write_limit = -1;
static TickType_t s_now;

void mock_reset(void)
{
    s_rx_len = s_rx_pos = 0;
    s_tx_len = s_writes = 0;
    s_resp_count = s_resp_next = 0;
    s_repeat_last = false;
    s_write_limit = -1;
}

void mock_rx_feed(const uint8_t *data, size_t len)
{
    assert(s_rx_len + len <= RX_SIZE);
    memcpy(&s_rx[s_rx_len], data, len);
    s_rx_len += len;
}

void mock_queue_response(const uint8_t *data, size_t len)
{
    assert(s_resp_count < MAX_RESPONSES && len <= RESPONSE_SIZE);
    memcpy(s_resp[s_resp_count], data, len);
    s_resp_len[s_resp_count++] = len;
}

void mock_repeat_last_response(void)
{
    s_repeat_last = true;
}

void mock_set_write_limit(int limit)
{
    s_write_limit = limit;
}

const uint8_t *mock_tx(size_t *len)
{
    *len = s_tx_len;
    return s_tx;
}

size_t mock_write_count(void)
{
    return s_writes;
}

uint32_t mock_now(void)
{
    return s_now;
}

TickType_t xTaskGetTickCount(void)
{
    return s_now;
}

void vTaskDelay(TickType_t ticks)
{
    s_now += ticks;
}

int uart_write_bytes(uart_port_t port, const void *src, size_t size)
{
    (void)port;
    s_writes++;
    if (s_write_limit >= 0 && size > (size_t)s_write_limit) size = s_write_limit;
    assert(s_tx_len + size <= TX_SIZE);
    memcpy(&s_tx[s_tx_len], src, size);
    s_tx_len += size;

    if (s_resp_next < s_resp_count) {
        mock_rx_feed(s_resp[s_resp_next], s_resp_len[s_resp_next]);
        s_resp_next++;
    } else if (s_repeat_last && s_resp_count > 0) {
        mock_rx_feed(s_resp[s_resp_count - 1], s_resp_len[s_resp_count - 1]);
    }
    return (int)size;
}

/* Like the real driver: returns early with fewer bytes only after the full wait */
int uart_read_bytes(uart_port_t port, void *buf, uint32_t length, TickType_t ticks_to_wait)
{
    (void)port;
    size_t avail = s_rx_len - s_rx_pos;
    size_t n     = avail < length ? avail : length;
    memcpy(buf, &s_rx[s_rx_pos], n);
    s_rx_pos += n;
    if (n < length) s_now += ticks_to_wait;
    return (int)n;
}

esp_err_t uart_flush_input(uart_port_t port)
{
    (void)port;
    s_rx_len = s_rx_pos = 0;
    return ESP_OK;
}
