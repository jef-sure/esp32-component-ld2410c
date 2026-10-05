# Host unit tests for the LD2410C driver: builds src/ld2410c.c against stubbed
# ESP-IDF headers and a scripted UART mock. Run with: make -C test/host
CC      ?= cc
CFLAGS  += -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer
CPPFLAGS += -Istubs -I. -I../../include

BUILD := build

.PHONY: test clean
test: $(BUILD)/test_ld2410c
	./$<

$(BUILD)/test_ld2410c: test_ld2410c.c mock_uart.c ../../src/ld2410c.c mock_uart.h ../../include/ld2410c.h $(wildcard stubs/*.h stubs/*/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_ld2410c.c mock_uart.c ../../src/ld2410c.c -o $@

clean:
	rm -rf $(BUILD)
