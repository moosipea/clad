CC := gcc
CFLAGS := -Wall -Wextra -pedantic -ggdb -MMD -MP

BUILD_DIR := build
BINARY_NAME := $(BUILD_DIR)/xml
SOURCES := $(wildcard *.c)
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
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<
