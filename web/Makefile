# Browser build: the shared sim (src/sim) and app flow (src/app) compiled to WebAssembly with Emscripten,
# drawn by the Three.js page in web/www. Needs emcc on PATH (source emsdk_env.sh).
#   make            build into dist/
#   make serve      build, then serve dist/ on http://localhost:8000
#   make test       parity (native vs wasm), JS unit tests and the browser smoke test
ROOT    := ..
DIST    := dist
BUILD   := build
EMCC    ?= emcc
CC      ?= gcc
SRC     := $(wildcard $(ROOT)/src/sim/*.c) $(wildcard $(ROOT)/src/app/*.c) $(wildcard src/*.c)
HDR     := $(wildcard $(ROOT)/src/sim/*.h) $(wildcard $(ROOT)/src/app/*.h) $(wildcard src/*.h)
INC     := -I$(ROOT)/src/sim -I$(ROOT)/src/app -Isrc
# -ffp-contract=off as on the host, so the wasm sim matches the native one bit for bit
CFLAGS  := -std=gnu99 -O2 -Wall -Wextra -ffp-contract=off $(INC)
EXPORTS := _web_init,_web_step,_web_frame_json,_web_static_json,_web_static_generation,_web_sys_boat,_web_sys_weather,_web_sys_back,_web_playing,_web_take_dirty,_web_audio,_web_audio_on,_web_alloc,_malloc,_free
LDFLAGS := -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createSim -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web \
           -sEXPORTED_FUNCTIONS=$(EXPORTS) -sEXPORTED_RUNTIME_METHODS=UTF8ToString,FS,HEAP16 \
           -lidbfs.js -sFORCE_FILESYSTEM=1
DATA    := --preload-file $(ROOT)/boats@/pdx/boats --preload-file $(ROOT)/config@/pdx/config \
           --preload-file $(ROOT)/tracks@/pdx/tracks --preload-file $(ROOT)/ghosts@/pdx/ghosts \
           --exclude-file '*/disabled/*'
ASSETS  := $(shell find $(ROOT)/boats $(ROOT)/config $(ROOT)/tracks $(ROOT)/ghosts -type f)
WWW     := $(shell find www -type f)

.PHONY: all serve test parity jstest smoke clean
all: $(DIST)/sim.js $(patsubst www/%,$(DIST)/%,$(WWW))

$(DIST)/sim.js: $(SRC) $(HDR) $(ASSETS) Makefile
	@mkdir -p $(DIST)
	$(EMCC) $(CFLAGS) $(SRC) $(LDFLAGS) $(DATA) -o $@

$(DIST)/%: www/%
	@mkdir -p $(dir $@)
	cp $< $@

serve: all
	cd $(DIST) && python3 -m http.server 8000

# ---- tests ----
$(BUILD)/parity: test/parity.c $(SRC) $(HDR)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) test/parity.c $(SRC) -lm -o $@

$(BUILD)/parity.js: test/parity.c $(SRC) $(HDR)
	@mkdir -p $(BUILD)
	$(EMCC) $(CFLAGS) test/parity.c $(SRC) -sENVIRONMENT=node -sNODERAWFS=1 -o $@

parity: $(BUILD)/parity $(BUILD)/parity.js
	@mkdir -p $(BUILD)/save-native $(BUILD)/save-wasm
	$(BUILD)/parity $(ROOT) $(BUILD)/save-native > $(BUILD)/parity-native.txt
	node $(BUILD)/parity.js $(ROOT) $(BUILD)/save-wasm > $(BUILD)/parity-wasm.txt
	python3 -I test/compare.py $(BUILD)/parity-native.txt $(BUILD)/parity-wasm.txt

jstest:
	node --test test/*.test.mjs

smoke: all
	node test/smoke.mjs $(DIST)

test: parity jstest smoke

clean:
	rm -r -f dist build
