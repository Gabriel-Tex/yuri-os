CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2 -g
LDFLAGS ?=
LDLIBS ?=

REQUIRED_CFLAGS = -std=c17 -Wall -Wextra -Wpedantic
BUILD_DIR = build
CLIENT_OBJECT = $(BUILD_DIR)/src/domain/client.o
QUEUE_OBJECT = $(BUILD_DIR)/src/domain/client_queue.o
METRICS_OBJECT = $(BUILD_DIR)/src/domain/metrics.o
CLOCK_OBJECT = $(BUILD_DIR)/src/platform/clock.o
SCHEDULER_OBJECT = $(BUILD_DIR)/src/runtime/scheduler.o
PROCESSES_OBJECT = $(BUILD_DIR)/src/platform/processes.o
DEMAND_OBJECT = $(BUILD_DIR)/src/platform/demand.o
IPC_OBJECT = $(BUILD_DIR)/src/platform/ipc.o
COMPLETION_OBJECT = $(BUILD_DIR)/src/platform/completion_file.o
CONTROLLED_CLIENT_OBJECT = $(BUILD_DIR)/tests/fixtures/controlled_client.o
TEST_OBJECTS = $(BUILD_DIR)/tests/domain_test.o $(BUILD_DIR)/tests/client_queue_test.o $(BUILD_DIR)/tests/metrics_test.o $(BUILD_DIR)/tests/scheduler_test.o $(BUILD_DIR)/tests/processes_test.o $(BUILD_DIR)/tests/demand_test.o $(BUILD_DIR)/tests/ipc_test.o $(BUILD_DIR)/tests/completion_file_test.o
DEPENDENCIES = $(CLIENT_OBJECT:.o=.d) $(QUEUE_OBJECT:.o=.d) $(METRICS_OBJECT:.o=.d) $(CLOCK_OBJECT:.o=.d) $(SCHEDULER_OBJECT:.o=.d) $(PROCESSES_OBJECT:.o=.d) $(DEMAND_OBJECT:.o=.d) $(IPC_OBJECT:.o=.d) $(COMPLETION_OBJECT:.o=.d) $(CONTROLLED_CLIENT_OBJECT:.o=.d) $(TEST_OBJECTS:.o=.d)

.PHONY: all test debug clean

all: $(BUILD_DIR)/domain_test $(BUILD_DIR)/client_queue_test $(BUILD_DIR)/metrics_test $(BUILD_DIR)/scheduler_test $(BUILD_DIR)/controlled_client $(BUILD_DIR)/processes_test $(BUILD_DIR)/demand_test $(BUILD_DIR)/ipc_test $(BUILD_DIR)/completion_file_test

$(BUILD_DIR)/domain_test: $(CLIENT_OBJECT) $(BUILD_DIR)/tests/domain_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/client_queue_test: $(CLIENT_OBJECT) $(QUEUE_OBJECT) $(BUILD_DIR)/tests/client_queue_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/metrics_test: $(CLIENT_OBJECT) $(METRICS_OBJECT) $(CLOCK_OBJECT) $(BUILD_DIR)/tests/metrics_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/scheduler_test: $(SCHEDULER_OBJECT) $(BUILD_DIR)/tests/scheduler_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/controlled_client: $(CONTROLLED_CLIENT_OBJECT)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/processes_test: $(PROCESSES_OBJECT) $(BUILD_DIR)/tests/processes_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/demand_test: $(DEMAND_OBJECT) $(PROCESSES_OBJECT) $(CLOCK_OBJECT) $(BUILD_DIR)/tests/demand_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/ipc_test: $(IPC_OBJECT) $(BUILD_DIR)/tests/ipc_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/completion_file_test: $(COMPLETION_OBJECT) $(BUILD_DIR)/tests/completion_file_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) $(REQUIRED_CFLAGS) -MMD -MP -c $< -o $@

test: all
	./$(BUILD_DIR)/domain_test
	./$(BUILD_DIR)/client_queue_test
	./$(BUILD_DIR)/metrics_test
	./$(BUILD_DIR)/scheduler_test
	./$(BUILD_DIR)/processes_test $(abspath $(BUILD_DIR)/controlled_client)
	./$(BUILD_DIR)/demand_test $(abspath $(BUILD_DIR)/controlled_client)
	./$(BUILD_DIR)/ipc_test
	./$(BUILD_DIR)/completion_file_test

debug:
	$(MAKE) BUILD_DIR=build/debug CFLAGS='-O0 -g3' test

clean:
	$(RM) -r -- build

-include $(DEPENDENCIES)
