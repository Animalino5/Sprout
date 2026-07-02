# Makefile for Sprout compiler — host build (PC, no devkitPro needed)
#
# For 3DS builds, use:
#   ./scripts/build_3ds.sh tests/samples/hello_3ds.bas
#
# See README.md for details.

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -g -O0
LDFLAGS ?= -lm

SRC_DIR     = src
TEST_DIR    = tests
RUNTIME_DIR = runtime
HOST_DIR    = runtime/host

# Compiler source files
SPROUT_SRCS = $(SRC_DIR)/lexer.c $(SRC_DIR)/token.c $(SRC_DIR)/ast.c \
	      $(SRC_DIR)/parser.c $(SRC_DIR)/types.c $(SRC_DIR)/typecheck.c \
	      $(SRC_DIR)/emit.c $(SRC_DIR)/sproutc.c

# Runtime source files (host)
RT_SRCS = $(HOST_DIR)/nb_runtime.c

.PHONY: all clean test test_lexer test_parser test_e2e

all: sproutc test_lexer test_parser libsprouthost.a

# ── Sprout compiler ─────────────────────────────────────────────────

sproutc: $(SPROUT_SRCS) $(SRC_DIR)/lexer.h $(SRC_DIR)/token.h $(SRC_DIR)/ast.h \
	 $(SRC_DIR)/parser.h $(SRC_DIR)/types.h $(SRC_DIR)/typecheck.h \
	 $(SRC_DIR)/emit.h
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $@ $(SPROUT_SRCS) $(LDFLAGS)

# ── Lexer / parser test drivers ─────────────────────────────────────

test_lexer: $(TEST_DIR)/test_lexer.c $(SRC_DIR)/lexer.c $(SRC_DIR)/token.c \
	    $(SRC_DIR)/lexer.h $(SRC_DIR)/token.h
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $@ \
	    $(TEST_DIR)/test_lexer.c $(SRC_DIR)/lexer.c $(SRC_DIR)/token.c \
	    $(LDFLAGS)

test_parser: $(TEST_DIR)/test_parser.c $(SRC_DIR)/lexer.c $(SRC_DIR)/token.c \
	     $(SRC_DIR)/ast.c $(SRC_DIR)/parser.c \
	     $(SRC_DIR)/lexer.h $(SRC_DIR)/token.h $(SRC_DIR)/ast.h \
	     $(SRC_DIR)/parser.h
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $@ \
	    $(TEST_DIR)/test_parser.c $(SRC_DIR)/lexer.c $(SRC_DIR)/token.c \
	    $(SRC_DIR)/ast.c $(SRC_DIR)/parser.c $(LDFLAGS)

# ── Runtime library (static, host) ──────────────────────────────────

libsprouthost.a: $(RT_SRCS) $(RUNTIME_DIR)/nb_runtime.h
	$(CC) $(CFLAGS) -I$(RUNTIME_DIR) -c -o nb_runtime.o $(HOST_DIR)/nb_runtime.c
	ar rcs libsprouthost.a nb_runtime.o

# ── End-to-end test ─────────────────────────────────────────────────

# Usage: make test_e2e SAMPLE=tests/samples/hello_simple.bas
test_e2e: sproutc libsprouthost.a
	@echo "── Compiling $(SAMPLE) ──"
	./sproutc $(SAMPLE) -o /tmp/test_out.c
	@echo "── Generated C (first 30 lines) ──"
	@head -30 /tmp/test_out.c
	@echo "── Building executable ──"
	$(CC) $(CFLAGS) -I$(RUNTIME_DIR) -o /tmp/test_out /tmp/test_out.c libsprouthost.a $(LDFLAGS)
	@echo "── Running ──"
	/tmp/test_out

# Run all sample programs
test: sproutc libsprouthost.a
	@for sample in tests/samples/hello_simple.bas tests/samples/menu.bas; do \
	    echo "════════════════════════════════════════"; \
	    echo "Testing: $$sample"; \
	    echo "════════════════════════════════════════"; \
	    ./sproutc $$sample -o /tmp/test_out.c || true; \
	    echo "── Generated C (first 30 lines) ──"; \
	    head -30 /tmp/test_out.c; \
	    echo "── Building ──"; \
	    $(CC) $(CFLAGS) -I$(RUNTIME_DIR) -o /tmp/test_out /tmp/test_out.c libsprouthost.a $(LDFLAGS) 2>&1 || echo "(build failed)"; \
	    echo "── Running (with 1s timeout) ──"; \
	    timeout 1 /tmp/test_out 2>&1 || echo "(exit code $$?)"; \
	    echo; \
	done

clean:
	rm -f sproutc test_lexer test_parser libsprouthost.a nb_runtime.o /tmp/test_out /tmp/test_out.c
