CC      ?= gcc
CFLAGS  := -std=c99 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -I. -I../src/sim -I../src/pd
LDLIBS  := -lm
SRC     := $(wildcard ../src/sim/*.c) ../src/pd/scenario.c
TESTS   := $(wildcard test_*.c)
BINS    := $(TESTS:%.c=build/%)

.PHONY: test bench clean
test: $(BINS)
	@for b in $(BINS); do echo "== $$b"; ./$$b || exit 1; done

build/%: %.c t.h $(SRC) $(wildcard ../src/sim/*.h) ../src/pd/scenario.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< $(SRC) $(LDLIBS)

# timing, not a test: optimised and without sanitizers
bench: build/bench_seasound
	./build/bench_seasound

build/bench_seasound: bench_seasound.c $(SRC) $(wildcard ../src/sim/*.h)
	@mkdir -p build
	$(CC) -std=c99 -O2 -I../src/sim -o $@ bench_seasound.c $(SRC) $(LDLIBS)

clean:
	rm -rf build
