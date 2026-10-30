CC      ?= gcc
CFLAGS  := -std=c99 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -I. -I../src/sim
LDLIBS  := -lm
SRC     := $(wildcard ../src/sim/*.c)
TESTS   := $(wildcard test_*.c)
BINS    := $(TESTS:%.c=build/%)

.PHONY: test clean
test: $(BINS)
	@for b in $(BINS); do echo "== $$b"; ./$$b || exit 1; done

build/%: %.c t.h $(SRC) $(wildcard ../src/sim/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< $(SRC) $(LDLIBS)

clean:
	rm -rf build
