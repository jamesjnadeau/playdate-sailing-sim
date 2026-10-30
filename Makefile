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

VPATH += src/pd:src/sim

SRC = src/pd/main.c src/pd/assets.c src/pd/render.c src/pd/hud.c \
      $(wildcard src/sim/*.c)

UINCDIR = src/sim src/pd
UASRC =
UDEFS =
UADEFS =
ULIBDIR =
ULIBS =

# Runtime configs ship inside the .pdx as plain files the game reads at start
# and from the Reload config menu item.
STAGE := $(shell mkdir -p Source && rm -rf Source/boats Source/config && cp -r boats config Source/)

include $(SDK)/C_API/buildsupport/common.mk
