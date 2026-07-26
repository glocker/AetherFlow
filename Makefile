include aetherflow.env
export

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude
PYTHON ?= python3

AETHERFLOW_HTTP_PORT ?= 8080
AETHERFLOW_CAN_INTERFACE ?= vcan0
AETHERFLOW_LOG_DIR ?= logs

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

VECTOR_GENERATOR_BIN := tools/generate_spacecan_vectors
VECTOR_FILE := compat/vectors/aetherflow_spacecan_vectors.json

PYTHON_SRCS := \
	bridge_service/*.py \
	bridge_service/eps/*.py \
	bridge_service/transports/*.py \
	eps_emulator/*.py

.PHONY: all dashboard-install dashboard-dev dashboard-build dashboard-preview demo test c-test python-check clean vectors compat compat-python FORCE

all: test compat

dashboard-install:
	npm ci --prefix openmct

dashboard-dev:
	npm --prefix openmct run dev

dashboard-build:
	npm --prefix openmct run build

dashboard-preview:
	npm --prefix openmct run preview

demo: dashboard-build
	./tools/run_local_demo.sh

tests/test_spacecan_codec: FORCE tests/test_spacecan_codec.c $(SPACECAN_SRCS) include/can_frame.h include/spacecan.h include/spacecan_services.h
	$(CC) $(CFLAGS) tests/test_spacecan_codec.c $(SPACECAN_SRCS) -o $@

tests/test_eps_simulator: FORCE tests/test_eps_simulator.c $(SPACECAN_SRCS) $(EPS_SRCS) include/can_frame.h include/spacecan.h include/spacecan_services.h include/eps_simulator.h
	$(CC) $(CFLAGS) tests/test_eps_simulator.c $(SPACECAN_SRCS) $(EPS_SRCS) -o $@

$(VECTOR_GENERATOR_BIN): tools/generate_spacecan_vectors.c $(SPACECAN_SRCS) $(EPS_SRCS) include/can_frame.h include/spacecan.h include/spacecan_services.h include/eps_simulator.h
	$(CC) $(CFLAGS) tools/generate_spacecan_vectors.c $(SPACECAN_SRCS) $(EPS_SRCS) -o $@

$(VECTOR_FILE): $(VECTOR_GENERATOR_BIN)
	./$(VECTOR_GENERATOR_BIN) > $(VECTOR_FILE)

vectors: $(VECTOR_FILE)

compat-python: $(VECTOR_FILE)
	$(PYTHON) compat/python/check_vectors.py $(VECTOR_FILE)

compat: compat-python

c-test: $(TEST_BINS)
	./tests/test_spacecan_codec
	./tests/test_eps_simulator

python-check:
	$(PYTHON) -m py_compile $(PYTHON_SRCS)

test: c-test python-check

clean:
	rm -f $(TEST_BINS) $(VECTOR_GENERATOR_BIN)
