CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude

SPACECAN_SRCS := \
	src/spacecan_id.c \
	src/spacecan_packet.c \
	src/spacecan_reassembly.c \
	src/spacecan_services.c

EPS_SRCS := \
	src/eps_simulator.c

TRANSPORT_SRCS := \
	src/can_frame_wire.c \
	transport/udp_transport.c

TEST_BINS := \
	tests/test_spacecan_codec \
	tests/test_eps_simulator

VECTOR_GENERATOR_BIN := tools/generate_spacecan_vectors
VECTOR_FILE := compat/vectors/aetherflow_spacecan_vectors.json

CONTROLLER_SIMULATOR_BIN := controller_simulator
EPS_SIMULATOR_BIN := eps_simulator
BRIDGE_SERVICE_BIN := bridge_service
BACKEND_BINS := $(CONTROLLER_SIMULATOR_BIN) $(EPS_SIMULATOR_BIN) $(BRIDGE_SERVICE_BIN)

.PHONY: all build backend dashboard-install dashboard-dev dashboard-build dashboard-preview demo test clean vectors compat compat-python

all: test backend compat

build: backend

backend: $(BACKEND_BINS)

dashboard-install:
	npm ci --prefix openmct

dashboard-dev:
	npm --prefix openmct run dev

dashboard-build:
	npm --prefix openmct run build

dashboard-preview:
	npm --prefix openmct run preview

demo: backend dashboard-install
	./tools/run_local_demo.sh

tests/test_spacecan_codec: tests/test_spacecan_codec.c $(SPACECAN_SRCS) include/can_frame.h include/spacecan.h include/spacecan_services.h
	$(CC) $(CFLAGS) tests/test_spacecan_codec.c $(SPACECAN_SRCS) -o $@

tests/test_eps_simulator: tests/test_eps_simulator.c $(SPACECAN_SRCS) $(EPS_SRCS) include/can_frame.h include/spacecan.h include/spacecan_services.h include/eps_simulator.h
	$(CC) $(CFLAGS) tests/test_eps_simulator.c $(SPACECAN_SRCS) $(EPS_SRCS) -o $@

$(VECTOR_GENERATOR_BIN): tools/generate_spacecan_vectors.c $(SPACECAN_SRCS) $(EPS_SRCS) include/can_frame.h include/spacecan.h include/spacecan_services.h include/eps_simulator.h
	$(CC) $(CFLAGS) tools/generate_spacecan_vectors.c $(SPACECAN_SRCS) $(EPS_SRCS) -o $@

$(VECTOR_FILE): $(VECTOR_GENERATOR_BIN)
	./$(VECTOR_GENERATOR_BIN) > $(VECTOR_FILE)

vectors: $(VECTOR_FILE)

compat-python: $(VECTOR_FILE)
	python3 compat/python/check_vectors.py $(VECTOR_FILE)

compat: compat-python

$(CONTROLLER_SIMULATOR_BIN): src/controller_simulator_main.c $(SPACECAN_SRCS) $(TRANSPORT_SRCS) include/can_frame.h include/can_frame_wire.h include/spacecan.h include/transport.h
	$(CC) $(CFLAGS) src/controller_simulator_main.c $(SPACECAN_SRCS) $(TRANSPORT_SRCS) -o $@

$(EPS_SIMULATOR_BIN): src/eps_simulator_main.c $(SPACECAN_SRCS) $(EPS_SRCS) $(TRANSPORT_SRCS) include/can_frame.h include/can_frame_wire.h include/spacecan.h include/spacecan_services.h include/eps_simulator.h include/transport.h
	$(CC) $(CFLAGS) src/eps_simulator_main.c $(SPACECAN_SRCS) $(EPS_SRCS) $(TRANSPORT_SRCS) -o $@

$(BRIDGE_SERVICE_BIN): src/bridge_service_main.c $(SPACECAN_SRCS) $(EPS_SRCS) $(TRANSPORT_SRCS) include/can_frame.h include/can_frame_wire.h include/spacecan.h include/spacecan_services.h include/eps_simulator.h include/transport.h
	$(CC) $(CFLAGS) src/bridge_service_main.c $(SPACECAN_SRCS) $(EPS_SRCS) $(TRANSPORT_SRCS) -o $@

test: $(TEST_BINS)
	./tests/test_spacecan_codec
	./tests/test_eps_simulator

clean:
	rm -f $(TEST_BINS) $(BACKEND_BINS) $(VECTOR_GENERATOR_BIN)
