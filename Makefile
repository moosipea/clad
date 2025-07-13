CFLAGS := -Wall -Wextra -pedantic -MMD -MP
BUILD_TYPE := DEBUG

ifeq ($(BUILD_TYPE),DEBUG)
	CFLAGS += -ggdb
else ifeq ($(BUILD_TYPE),RELEASE)
	CFLAGS += -O3
endif

BUILD_DIR := build
CLAD_BINARY := $(BUILD_DIR)/clad
SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPENDS := $(patsubst %.c,$(BUILD_DIR)/%.d,$(SOURCES))

ifeq ($(OS),Windows_NT)
	CLAD_BINARY := $(CLAD_BINARY).exe
endif

GENERATED_DIR := generated
GENERATED_HEADER_FILE := $(GENERATED_DIR)/clad/gl.h
GENERATED_SOURCE_FILE := $(GENERATED_DIR)/clad/gl.c
KHR_HEADER := $(GENERATED_DIR)/KHR/khrplatform.h

GL_API := gl
GL_PROFILE := core
GL_VERSION = 3.3

all: $(GENERATED_HEADER_FILE)
binary: $(CLAD_BINARY)

$(GENERATED_HEADER_FILE) $(GENERATED_SOURCE_FILE): $(CLAD_BINARY) $(KHR_HEADER) files/gl.xml
	mkdir -p $(@D)
	$(CLAD_BINARY) \
	--in-xml files/gl.xml \
	--out-header $(GENERATED_HEADER_FILE) \
	--out-source $(GENERATED_SOURCE_FILE) \
	--api $(GL_API) \
	--profile $(GL_PROFILE) \
	--version $(GL_VERSION)

$(KHR_HEADER): files/khrplatform.h
	mkdir -p $(@D)
	cp $< $@

$(CLAD_BINARY): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

-include $(DEPENDS)

$(BUILD_DIR)/%.o: %.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<
