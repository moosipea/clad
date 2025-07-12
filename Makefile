CFLAGS := -Wall -Wextra -pedantic -MMD -MP
BUILD_TYPE := DEBUG

ifeq ($(BUILD_TYPE),DEBUG)
	CFLAGS += -ggdb
else ifeq ($(BUILD_TYPE),RELEASE)
	CFLAGS += -O3
endif

BUILD_DIR := build
BINARY_NAME := $(BUILD_DIR)/clad
SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPENDS := $(patsubst %.c,$(BUILD_DIR)/%.d,$(SOURCES))

ifeq ($(OS),Windows_NT)
	BINARY_NAME := $(BINARY_NAME).exe
endif

all: $(BINARY_NAME)

$(BINARY_NAME): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

-include $(DEPENDS)

$(BUILD_DIR)/%.o: %.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<
