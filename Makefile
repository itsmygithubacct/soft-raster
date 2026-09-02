PROJECT := soft-raster
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=

CC ?= cc
AR ?= ar
INSTALL ?= install
PYTHON ?= python3

CPPFLAGS += -Iinclude
WARNINGS := \
	-Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -fPIC $(WARNINGS)
LDLIBS := -lm

LIB_OBJS := $(BUILD_DIR)/soft_raster.o
STATIC_LIB := $(BUILD_DIR)/lib$(PROJECT).a
SHARED_LIB := $(BUILD_DIR)/lib$(PROJECT).so
TEST_BIN := $(BUILD_DIR)/test-raster
GRAPH_TEST_BIN := $(BUILD_DIR)/test-graph-primitives
EXAMPLE_BIN := $(BUILD_DIR)/demo
BENCH_BIN := $(BUILD_DIR)/bench-raster

.PHONY: all benchmark clean install python-benchmark python-check \
	python-wheel sanitize test

all: $(STATIC_LIB) $(SHARED_LIB) $(EXAMPLE_BIN)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/soft_raster.o: src/soft_raster.c include/soft_raster.h \
		src/font8x16.h src/font7x14.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(STATIC_LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(SHARED_LIB): $(LIB_OBJS)
	$(CC) -shared $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST_BIN): tests/test_raster.c $(STATIC_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(GRAPH_TEST_BIN): tests/test_graph_primitives.c $(STATIC_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(EXAMPLE_BIN): examples/demo.c $(STATIC_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(BENCH_BIN): benchmarks/bench_raster.c $(STATIC_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB) $(LDFLAGS) $(LDLIBS) -o $@

test: $(TEST_BIN) $(GRAPH_TEST_BIN)
	$(TEST_BIN)
	$(GRAPH_TEST_BIN)

benchmark: $(BENCH_BIN)
	$(BENCH_BIN)

python-benchmark: all
	PYTHONPATH=python/src SOFT_RASTER_LIBRARY=$(abspath $(SHARED_LIB)) \
		$(PYTHON) benchmarks/bench_python.py

sanitize: | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g3 $(WARNINGS) \
		-fno-omit-frame-pointer -fsanitize=address,undefined \
		src/soft_raster.c tests/test_raster.c \
		-fsanitize=address,undefined $(LDLIBS) -o $(BUILD_DIR)/test-raster-sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD_DIR)/test-raster-sanitize
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g3 $(WARNINGS) \
		-fno-omit-frame-pointer -fsanitize=address,undefined \
		src/soft_raster.c tests/test_graph_primitives.c \
		-fsanitize=address,undefined $(LDLIBS) \
		-o $(BUILD_DIR)/test-graph-primitives-sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD_DIR)/test-graph-primitives-sanitize

python-check:
	$(MAKE) -C python check SOFT_RASTER_DIR=..

python-wheel:
	$(MAKE) -C python wheel SOFT_RASTER_DIR=..

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	$(INSTALL) -m 0644 include/soft_raster.h $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(STATIC_LIB) $(SHARED_LIB) $(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf $(BUILD_DIR)
