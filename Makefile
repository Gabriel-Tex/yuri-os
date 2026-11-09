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
INTERRUPT_OBJECT = $(BUILD_DIR)/src/platform/interrupt.o
COMPLETION_OBJECT = $(BUILD_DIR)/src/platform/completion_file.o
TERMINAL_OBJECT = $(BUILD_DIR)/src/platform/terminal.o
ANALYST_COORDINATOR_OBJECT = $(BUILD_DIR)/src/app/analyst_coordinator.o
SERVICE_OBJECT = $(BUILD_DIR)/src/app/service.o
WORKERS_OBJECT = $(BUILD_DIR)/src/app/workers.o
CONTROLLED_CLIENT_OBJECT = $(BUILD_DIR)/tests/fixtures/controlled_client.o
TEST_OBJECTS = $(BUILD_DIR)/tests/domain_test.o $(BUILD_DIR)/tests/client_queue_test.o $(BUILD_DIR)/tests/metrics_test.o $(BUILD_DIR)/tests/scheduler_test.o $(BUILD_DIR)/tests/processes_test.o $(BUILD_DIR)/tests/demand_test.o $(BUILD_DIR)/tests/ipc_test.o $(BUILD_DIR)/tests/completion_file_test.o $(BUILD_DIR)/tests/analyst_test.o $(BUILD_DIR)/tests/analyst_coordinator_test.o $(BUILD_DIR)/tests/service_test.o $(BUILD_DIR)/tests/reception_test.o $(BUILD_DIR)/tests/attendant_test.o $(BUILD_DIR)/tests/finite_service_test.o
DEPENDENCIES = $(CLIENT_OBJECT:.o=.d) $(QUEUE_OBJECT:.o=.d) $(METRICS_OBJECT:.o=.d) $(CLOCK_OBJECT:.o=.d) $(SCHEDULER_OBJECT:.o=.d) $(PROCESSES_OBJECT:.o=.d) $(DEMAND_OBJECT:.o=.d) $(IPC_OBJECT:.o=.d) $(INTERRUPT_OBJECT:.o=.d) $(COMPLETION_OBJECT:.o=.d) $(TERMINAL_OBJECT:.o=.d) $(ANALYST_COORDINATOR_OBJECT:.o=.d) $(SERVICE_OBJECT:.o=.d) $(WORKERS_OBJECT:.o=.d) $(CONTROLLED_CLIENT_OBJECT:.o=.d) $(TEST_OBJECTS:.o=.d) $(BUILD_DIR)/tests/terminal_test.d $(BUILD_DIR)/tests/infinite_service_test.d $(BUILD_DIR)/src/analyst_main.d $(BUILD_DIR)/src/service_main.d

.PHONY: all test debug clean

all: $(BUILD_DIR)/domain_test $(BUILD_DIR)/client_queue_test $(BUILD_DIR)/metrics_test $(BUILD_DIR)/scheduler_test $(BUILD_DIR)/controlled_client $(BUILD_DIR)/processes_test $(BUILD_DIR)/demand_test $(BUILD_DIR)/ipc_test $(BUILD_DIR)/completion_file_test $(BUILD_DIR)/terminal_test $(BUILD_DIR)/analyst $(BUILD_DIR)/analyst_test $(BUILD_DIR)/analyst_coordinator_test $(BUILD_DIR)/service $(BUILD_DIR)/service_test $(BUILD_DIR)/reception_test $(BUILD_DIR)/attendant_test $(BUILD_DIR)/finite_service_test $(BUILD_DIR)/infinite_service_test

$(BUILD_DIR)/terminal_test: $(TERMINAL_OBJECT) $(BUILD_DIR)/tests/terminal_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

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

$(BUILD_DIR)/analyst: $(COMPLETION_OBJECT) $(IPC_OBJECT) $(BUILD_DIR)/src/analyst_main.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/analyst_test: $(COMPLETION_OBJECT) $(IPC_OBJECT) $(PROCESSES_OBJECT) $(BUILD_DIR)/tests/analyst_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/analyst_coordinator_test: $(ANALYST_COORDINATOR_OBJECT) $(COMPLETION_OBJECT) $(IPC_OBJECT) $(PROCESSES_OBJECT) $(SCHEDULER_OBJECT) $(BUILD_DIR)/tests/analyst_coordinator_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

SERVICE_DEPENDENCIES = $(SERVICE_OBJECT) $(ANALYST_COORDINATOR_OBJECT) $(COMPLETION_OBJECT) $(IPC_OBJECT) $(INTERRUPT_OBJECT) $(PROCESSES_OBJECT) $(SCHEDULER_OBJECT) $(CLOCK_OBJECT) $(TERMINAL_OBJECT) $(WORKERS_OBJECT) $(CLIENT_OBJECT) $(QUEUE_OBJECT) $(METRICS_OBJECT) $(DEMAND_OBJECT)

$(BUILD_DIR)/service: $(SERVICE_DEPENDENCIES) $(BUILD_DIR)/src/service_main.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/service_test: $(SERVICE_DEPENDENCIES) $(BUILD_DIR)/tests/service_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/reception_test: $(SERVICE_DEPENDENCIES) $(BUILD_DIR)/tests/reception_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/attendant_test: $(SERVICE_DEPENDENCIES) $(BUILD_DIR)/tests/attendant_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/finite_service_test: $(SERVICE_DEPENDENCIES) $(BUILD_DIR)/tests/finite_service_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/infinite_service_test: $(SERVICE_DEPENDENCIES) $(BUILD_DIR)/tests/infinite_service_test.o
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
	./$(BUILD_DIR)/terminal_test
	./$(BUILD_DIR)/analyst_test $(abspath $(BUILD_DIR)/analyst)
	./$(BUILD_DIR)/analyst_coordinator_test $(abspath $(BUILD_DIR)/analyst)
	./$(BUILD_DIR)/service_test $(abspath $(BUILD_DIR)/analyst)
	./tests/service_cli_test.sh $(abspath $(BUILD_DIR)/service)
	./$(BUILD_DIR)/reception_test $(abspath $(BUILD_DIR)/analyst) $(abspath $(BUILD_DIR)/controlled_client)
	./$(BUILD_DIR)/attendant_test $(abspath $(BUILD_DIR)/analyst) $(abspath $(BUILD_DIR)/controlled_client)
	./$(BUILD_DIR)/finite_service_test $(abspath $(BUILD_DIR)/analyst) $(abspath $(BUILD_DIR)/controlled_client)
	./$(BUILD_DIR)/infinite_service_test $(abspath $(BUILD_DIR)/analyst) $(abspath $(BUILD_DIR)/controlled_client)
	bash ./tests/finite_service_test.sh $(abspath $(BUILD_DIR)/service) $(abspath $(BUILD_DIR)/controlled_client)
	bash ./tests/infinite_service_test.sh $(abspath $(BUILD_DIR)/service) $(abspath $(BUILD_DIR)/controlled_client)
	bash ./tests/error_service_test.sh $(abspath $(BUILD_DIR)/service) $(abspath $(BUILD_DIR)/controlled_client)

debug:
	$(MAKE) BUILD_DIR=build/debug CFLAGS='-O0 -g3' test

clean:
	$(RM) -r -- build

-include $(DEPENDENCIES)
