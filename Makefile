###

PLATFORM=esp32s3
NAME=powermon
SOURCES=main/$(NAME).c
EXAMPLE=example/$(NAME).c
VERSION=1.00
DEVICE=/dev/ttyACM0

###

BUILDER=idf.py
BUILDER_ARGS=build
BUILDER_CFGS=build/build.ninja

TARGET=build/$(NAME).bin
TARGET_VERSIONED=bin/$(NAME)-$(VERSION).bin

###

.PHONY: all

all: $(TARGET)
	@if [ ! -f $(TARGET_VERSIONED) ]; then \
		mkdir -p bin; \
		echo "Creating versioned binary: $(TARGET_VERSIONED)"; \
		cp $(TARGET) $(TARGET_VERSIONED); \
	fi

$(TARGET): $(SOURCES) $(BUILDER_CFGS)
	$(BUILDER) $(BUILDER_ARGS)

$(BUILDER_CFGS):
	$(BUILDER) set-target $(PLATFORM)

upload:
	$(BUILDER) flash -p $(DEVICE)

debug:
	$(BUILDER) flash -p $(DEVICE) monitor

###

clean:
	$(BUILDER) clean

fullclean:
	$(BUILDER) fullclean

format:
	clang-format -i $(SOURCES) $(EXAMPLE)

example:
	( cd example; make -f Makefile )

force-version: $(TARGET)
	@mkdir -p bin
	@echo "Force creating versioned binary: $(TARGET_VERSIONED)"
	@cp $(TARGET) $(TARGET_VERSIONED)

###

.PHONY: all clean fullclean format example force-version
.DEFAULT: all

###

