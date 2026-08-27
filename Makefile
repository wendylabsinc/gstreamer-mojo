.PHONY: all build test test-native test-mojo package clean

BUILD_DIR ?= build
MOJO ?= mojo

all: test

build:
	cmake -S . -B $(BUILD_DIR) -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) --parallel

test-native: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

test-mojo: build
	LD_LIBRARY_PATH=$(abspath $(BUILD_DIR)):$(LD_LIBRARY_PATH) \
	DYLD_LIBRARY_PATH=$(abspath $(BUILD_DIR)):$(DYLD_LIBRARY_PATH) \
	$(MOJO) run -I . tests/mojo_smoke.mojo

test: test-native test-mojo

package: build
	$(MOJO) precompile gstreamer_mojo -o $(BUILD_DIR)/gstreamer_mojo.mojoc

clean:
	cmake -E remove_directory $(BUILD_DIR)
