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
TEST_OBJECTS = $(BUILD_DIR)/tests/domain_test.o $(BUILD_DIR)/tests/client_queue_test.o $(BUILD_DIR)/tests/metrics_test.o
DEPENDENCIES = $(CLIENT_OBJECT:.o=.d) $(QUEUE_OBJECT:.o=.d) $(METRICS_OBJECT:.o=.d) $(CLOCK_OBJECT:.o=.d) $(TEST_OBJECTS:.o=.d)

.PHONY: all test debug clean

all: $(BUILD_DIR)/domain_test $(BUILD_DIR)/client_queue_test $(BUILD_DIR)/metrics_test

$(BUILD_DIR)/domain_test: $(CLIENT_OBJECT) $(BUILD_DIR)/tests/domain_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/client_queue_test: $(CLIENT_OBJECT) $(QUEUE_OBJECT) $(BUILD_DIR)/tests/client_queue_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/metrics_test: $(CLIENT_OBJECT) $(METRICS_OBJECT) $(CLOCK_OBJECT) $(BUILD_DIR)/tests/metrics_test.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) $(REQUIRED_CFLAGS) -MMD -MP -c $< -o $@

test: all
	./$(BUILD_DIR)/domain_test
	./$(BUILD_DIR)/client_queue_test
	./$(BUILD_DIR)/metrics_test

debug:
	$(MAKE) BUILD_DIR=build/debug CFLAGS='-O0 -g3' test

clean:
	$(RM) -r -- build

-include $(DEPENDENCIES)
