CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude

SPACECAN_SRCS := \
	src/spacecan_id.c \
	src/spacecan_packet.c \
	src/spacecan_reassembly.c \
	src/spacecan_services.c

TEST_BIN := tests/test_spacecan_codec

.PHONY: all test clean

all: test

$(TEST_BIN): tests/test_spacecan_codec.c $(SPACECAN_SRCS) include/can_frame.h include/spacecan.h include/spacecan_services.h
	$(CC) $(CFLAGS) tests/test_spacecan_codec.c $(SPACECAN_SRCS) -o $(TEST_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(TEST_BIN)
