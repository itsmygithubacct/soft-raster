PYTHON ?= python3
SOFT_RASTER_DIR ?= ../soft-raster
SOFT_RASTER_LIBRARY := $(abspath $(SOFT_RASTER_DIR)/build/libsoft-raster.so)
PYTHON_SOURCES := $(wildcard src/soft_raster/*.py)
TEST_SOURCES := $(wildcard tests/*.py)

.DEFAULT_GOAL := all

all: native compile

native:
	$(MAKE) -C $(SOFT_RASTER_DIR) all

upstream-test:
	$(MAKE) -C $(SOFT_RASTER_DIR) test

compile:
	$(PYTHON) -m py_compile $(PYTHON_SOURCES) $(TEST_SOURCES) examples/demo.py

test: native
	PYTHONPATH=src SOFT_RASTER_LIBRARY=$(SOFT_RASTER_LIBRARY) \
		$(PYTHON) -m unittest discover -s tests -v

check: upstream-test compile test

demo: native
	PYTHONPATH=src SOFT_RASTER_LIBRARY=$(SOFT_RASTER_LIBRARY) \
		$(PYTHON) examples/demo.py demo.ppm

wheel: native
	$(PYTHON) -m pip wheel --no-deps --no-build-isolation --wheel-dir dist .

clean:
	rm -rf build dist src/soft_raster_py.egg-info src/soft_raster/__pycache__ \
		tests/__pycache__ examples/__pycache__ demo.ppm

.PHONY: all native upstream-test compile test check demo wheel clean
