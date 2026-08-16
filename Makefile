# omarchy-launch-screensaver
#
#   make          build
#   make clean    remove the build tree
#   make run      build and launch (force)
#   make install  install to $(PREFIX)/bin
#
#   make run RUNFLAGS='--effect print --seed 1'
#   make run RUNFLAGS='--headless --frames 20 --effect print --cols 40 --rows 12'
#   make install PREFIX=$HOME/.local

BUILD  ?= build
PREFIX ?= /usr/local
BIN    := $(BUILD)/omarchy-launch-screensaver

CMAKE     ?= cmake
GENERATOR ?= Ninja
RUNFLAGS  ?=

.PHONY: all clean run install

all:
	$(CMAKE) -S . -B $(BUILD) -G $(GENERATOR)
	$(CMAKE) --build $(BUILD)
	@echo $(BIN)

clean:
	rm -rf $(BUILD)

run: all
	$(BIN) force $(RUNFLAGS)

install: all
	$(CMAKE) --install $(BUILD) --prefix $(PREFIX)
