/* Scriptable UART + tick mock for the LD2410C host tests */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Clear RX data, queued responses, captured TX and the write limit. Time keeps running. */
void mock_reset(void);

/* Make bytes available to uart_read_bytes() right away. */
void mock_rx_feed(const uint8_t *data, size_t len);

/* Queue a response that becomes readable after the next uart_write_bytes(). */
void mock_queue_response(const uint8_t *data, size_t len);

/* Repeat the last queued response for every further write. */
void mock_repeat_last_response(void);

/* Make uart_write_bytes() accept at most this many bytes (-1 = no limit). */
void mock_set_write_limit(int limit);

/* Bytes passed to uart_write_bytes() since the last reset. */
const uint8_t *mock_tx(size_t *len);
size_t         mock_write_count(void);

uint32_t mock_now(void);
