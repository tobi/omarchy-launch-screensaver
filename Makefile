# omarchy-launch-screensaver
#
#   make          release build
#   make check    tests and compiler checks
#   make benchmark
#   make run      build and launch (force)
#   make install  install to $(PREFIX)/bin

PREFIX           ?= /usr/local
CARGO            ?= cargo
CARGO_TARGET_DIR ?= build/rust
RUNFLAGS          ?=
BIN               := $(CARGO_TARGET_DIR)/release/omarchy-launch-screensaver

export CARGO_TARGET_DIR

.PHONY: all benchmark check clean run install

all:
	$(CARGO) build --release
	@echo $(BIN)

benchmark:
	$(CARGO) bench --bench render

check:
	$(CARGO) test
	$(CARGO) check

clean:
	$(CARGO) clean

run: all
	$(BIN) force $(RUNFLAGS)

install: all
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/omarchy-launch-screensaver
