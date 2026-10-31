# Host harness: the real src/pd main loop against a stub Playdate API.
# make run SCEN=name | run-all | check-determinism | update-golden
CC     ?= gcc
ROOT   := ..
SDK     = $(PLAYDATE_SDK_PATH)
ifeq ($(SDK),)
	SDK = $(shell egrep '^\s*SDKRoot' ~/.Playdate/config | head -n 1 | cut -c9-)
endif
ifeq ($(SDK),)
	SDK = $(HOME)/Developer/PlaydateSDK-3.1.2
endif
ifeq ($(wildcard $(SDK)/C_API),)
$(error SDK path not found; set ENV value PLAYDATE_SDK_PATH)
endif

BUILD  := build
SRC    := $(ROOT)/src/pd/main.c $(ROOT)/src/pd/assets.c $(ROOT)/src/pd/render.c $(ROOT)/src/pd/hud.c \
          $(ROOT)/src/pd/tracks.c $(ROOT)/src/pd/menu_draw.c $(ROOT)/src/pd/scenario.c \
          $(ROOT)/src/pd/scenario_io.c $(wildcard $(ROOT)/src/sim/*.c) pdstub.c host_main.c
# -ffp-contract=off keeps float results identical across compilers and CPUs
CFLAGS := -std=gnu99 -O1 -Wall -Wextra -DSCENARIO -DTARGET_SIMULATOR=1 -DTARGET_EXTENSION=1 -ffp-contract=off \
          -I. -I$(SDK)/C_API -I$(ROOT)/src/sim -I$(ROOT)/src/pd
SCEN   ?= smoke
# typo is a deliberate parse failure, so it stays out of run-all and the goldens
SCENS  := $(filter-out typo,$(basename $(notdir $(wildcard $(ROOT)/tests/scenarios/*.txt))))
GOLDEN := $(ROOT)/tests/golden/host

.PHONY: all assets run run-all check-determinism update-golden check check-regression check-fidelity clean
all: $(BUILD)/harness

$(BUILD)/harness: $(SRC) $(wildcard $(ROOT)/src/*/*.h) pdstub.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

# the same boats/config/tracks the root Makefile stages into Source/
$(BUILD)/assets: $(wildcard $(ROOT)/boats/* $(ROOT)/config/* $(ROOT)/tracks/*)
	rm -rf $@ && mkdir -p $@
	cp -r $(ROOT)/boats $(ROOT)/config $(ROOT)/tracks $@/
	@touch $@
assets: $(BUILD)/assets

run: $(BUILD)/harness $(BUILD)/assets
	rm -rf $(BUILD)/out/$(SCEN) && mkdir -p $(BUILD)/out/$(SCEN)
	$(BUILD)/harness $(ROOT)/tests/scenarios/$(SCEN).txt $(BUILD)/out/$(SCEN) $(BUILD)/assets

# run-all keeps going past a failing scenario but still fails overall
run-all: $(BUILD)/harness $(BUILD)/assets
	@rc=0; for s in $(SCENS); do $(MAKE) --no-print-directory run SCEN=$$s || rc=1; done; exit $$rc

check-determinism: $(BUILD)/harness $(BUILD)/assets
	@for s in $(SCENS); do \
	  rm -rf $(BUILD)/det1 $(BUILD)/det2 && mkdir -p $(BUILD)/det1 $(BUILD)/det2 && \
	  $(BUILD)/harness $(ROOT)/tests/scenarios/$$s.txt $(BUILD)/det1 $(BUILD)/assets 2>/dev/null && \
	  $(BUILD)/harness $(ROOT)/tests/scenarios/$$s.txt $(BUILD)/det2 $(BUILD)/assets 2>/dev/null && \
	  diff -r $(BUILD)/det1 $(BUILD)/det2 >/dev/null && echo "deterministic: $$s" || { echo "NONDETERMINISTIC: $$s"; exit 1; }; \
	done

update-golden: run-all
	rm -rf $(GOLDEN) && mkdir -p $(GOLDEN)
	cp -r $(BUILD)/out/* $(GOLDEN)/

check-regression: run-all
	@OUT_DIR=$(BUILD)/out bash $(CURDIR)/check.sh regression

check-fidelity: run-all
	@OUT_DIR=$(BUILD)/out bash $(CURDIR)/check.sh fidelity

check: run-all
	@OUT_DIR=$(BUILD)/out bash $(CURDIR)/check.sh all

clean:
	rm -rf $(BUILD)
