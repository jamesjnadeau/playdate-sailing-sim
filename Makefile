HEAP_SIZE      = 8388208
STACK_SIZE     = 61800

PRODUCT = SailingSim.pdx

SDK = ${PLAYDATE_SDK_PATH}
ifeq ($(SDK),)
	SDK = $(shell egrep '^\s*SDKRoot' ~/.Playdate/config | head -n 1 | cut -c9-)
endif

ifeq ($(SDK),)
$(error SDK path not found; set ENV value PLAYDATE_SDK_PATH)
endif

VPATH += src/pd:src/app:src/sim

SRC = src/pd/main.c src/pd/render.c src/pd/hud.c \
      src/pd/menu_draw.c src/pd/audio.c \
      $(wildcard src/app/*.c) $(wildcard src/sim/*.c)

UINCDIR = src/sim src/app src/pd
UASRC =
UDEFS =

# make SCENARIO=1 builds SailingSimScenario.pdx with the scripted-input harness
# (-DSCENARIO); the default build is unchanged.
VARIANT = normal
ifeq ($(SCENARIO),1)
PRODUCT = SailingSimScenario.pdx
UDEFS += -DSCENARIO
SRC += src/pd/scenario.c src/pd/scenario_io.c
VARIANT = scenario
endif
# make DEMO=1 builds SailingSimDemo.pdx: Free Range only, five minutes a run, then back to the home screen.
ifeq ($(DEMO),1)
PRODUCT = SailingSimDemo.pdx
UDEFS += -DDEMO
VARIANT := $(VARIANT)-demo
endif
# make FPS=1 draws the SDK frame-rate readout in the top-left corner, for device profiling.
ifeq ($(FPS),1)
UDEFS += -DSHOW_FPS
VARIANT := $(VARIANT)-fps
endif
UADEFS =
ULIBDIR =
ULIBS = --specs=nosys.specs

# Runtime configs ship inside the .pdx as plain files the game reads at start
# and when the boat changes.
STAGE := $(shell mkdir -p Source && rm -rf Source/boats Source/config Source/tracks Source/ghosts && mkdir -p ghosts && cp -r boats config tracks ghosts Source/)

include $(SDK)/C_API/buildsupport/common.mk

# Both variants share build/ and Source/pdex.*, and the simulator dylib rule only
# tracks source timestamps, so rebuild whenever the variant changes.
VARIANT_STAMP := $(shell mkdir -p $(OBJDIR) && { echo $(VARIANT) | cmp -s - $(OBJDIR)/.variant || echo $(VARIANT) > $(OBJDIR)/.variant; } )
$(OBJDIR)/pdex.${DYLIB_EXT} $(OBJS): $(OBJDIR)/.variant

# common.mk applies UDEFS to the device objects only; the simulator dylib is one gcc call.
DYLIB_FLAGS += $(UDEFS)

# Regenerate the baseline ghosts the game ships (run again after changing a track, boat, weather or the ghost format).
.PHONY: ghosts
ghosts:
	mkdir -p build ghosts
	cc -std=gnu99 -O1 -ffp-contract=off -Isrc/sim -o build/ghost_gen tools/ghost_gen.c $(wildcard src/sim/*.c) src/pd/scenario.c -lm
	build/ghost_gen . ghosts
