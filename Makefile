CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2 -g
LDFLAGS ?=
LDLIBS ?=

REQUIRED_CFLAGS = -std=c17 -Wall -Wextra -Wpedantic
BUILD_DIR = build
DOMAIN_OBJECTS = $(BUILD_DIR)/src/domain/client.o $(BUILD_DIR)/tests/domain_test.o
DEPENDENCIES = $(DOMAIN_OBJECTS:.o=.d)

.PHONY: all test debug clean

all: $(BUILD_DIR)/domain_test

$(BUILD_DIR)/domain_test: $(DOMAIN_OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) $(REQUIRED_CFLAGS) -MMD -MP -c $< -o $@

test: $(BUILD_DIR)/domain_test
	./$(BUILD_DIR)/domain_test

debug:
	$(MAKE) BUILD_DIR=build/debug CFLAGS='-O0 -g3' test

clean:
	$(RM) -r -- build

-include $(DEPENDENCIES)
