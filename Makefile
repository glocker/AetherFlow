CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude

SPACECAN_SRCS := \
	src/spacecan_id.c \
	src/spacecan_packet.c \
	src/spacecan_reassembly.c \
	src/spacecan_services.c

EPS_SRCS := \
	src/eps_simulator.c

TEST_BINS := \
	tests/test_spacecan_codec \
	tests/test_eps_simulator

EPS_SIMULATOR_BIN := eps_simulator

.PHONY: all test clean

all: test $(EPS_SIMULATOR_BIN)

tests/test_spacecan_codec: tests/test_spacecan_codec.c $(SPACECAN_SRCS) include/can_frame.h include/spacecan.h include/spacecan_services.h
	$(CC) $(CFLAGS) tests/test_spacecan_codec.c $(SPACECAN_SRCS) -o $@

tests/test_eps_simulator: tests/test_eps_simulator.c $(SPACECAN_SRCS) $(EPS_SRCS) include/can_frame.h include/spacecan.h include/spacecan_services.h include/eps_simulator.h
	$(CC) $(CFLAGS) tests/test_eps_simulator.c $(SPACECAN_SRCS) $(EPS_SRCS) -o $@

$(EPS_SIMULATOR_BIN): src/eps_simulator_main.c $(SPACECAN_SRCS) $(EPS_SRCS) include/can_frame.h include/spacecan.h include/spacecan_services.h include/eps_simulator.h
	$(CC) $(CFLAGS) src/eps_simulator_main.c $(SPACECAN_SRCS) $(EPS_SRCS) -o $@

test: $(TEST_BINS)
	./tests/test_spacecan_codec
	./tests/test_eps_simulator

clean:
	rm -f $(TEST_BINS) $(EPS_SIMULATOR_BIN)
