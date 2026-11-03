CC      ?= gcc
CFLAGS  := -std=c99 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -I. -I../src/sim -I../src/pd
LDLIBS  := -lm
SRC     := $(wildcard ../src/sim/*.c) ../src/pd/scenario.c
OBJS    := $(addprefix build/obj/,$(notdir $(SRC:.c=.o)))
TESTS   := $(wildcard test_*.c)
BINS    := $(TESTS:%.c=build/%)
RUNS    := $(TESTS:%.c=build/%.ok)

# Tests are independent processes: build and run them side by side. Output is grouped per target.
MAKEFLAGS += -j$(shell nproc 2>/dev/null || echo 4) --output-sync=target

vpath %.c ../src/sim ../src/pd

# The run targets are phony so every `make test` really runs every test.
.PHONY: test bench clean $(RUNS)
test: $(RUNS)

# $(RUNS) have no files: build/%.log keeps the output, shown in full only for a failure.
$(RUNS): build/%.ok: build/%
	@./$< > build/$*.log 2>&1 || { cat build/$*.log; exit 1; }
	@echo "ok  $* ($$(tail -n 1 build/$*.log))"

build/obj/%.o: %.c
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

build/%: %.c t.h $(OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -MMD -MP -MF build/$*.d -o $@ $< $(OBJS) $(LDLIBS)

-include $(wildcard build/obj/*.d build/*.d)

# timing, not a test: optimised and without sanitizers
bench: build/bench_seasound
	./build/bench_seasound

build/bench_seasound: bench_seasound.c $(SRC) $(wildcard ../src/sim/*.h)
	@mkdir -p build
	$(CC) -std=c99 -O2 -I../src/sim -o $@ bench_seasound.c $(SRC) $(LDLIBS)

clean:
	rm -rf build
