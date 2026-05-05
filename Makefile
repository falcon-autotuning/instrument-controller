.PHONY: help configure build test clean install vcpkg-bootstrap 
# Build preset (user can override: make build PRESET=linux-gcc-release)
PRESET ?= linux-clang-release
CMAKE_BUILD_DIR := build/$(PRESET)
ifeq ($(OS),Windows_NT)
  SUDO :=
else
  SUDO := sudo
endif

help:
	@echo "Instrument Controller Build System"
	@echo "============================="
	@echo ""
	@echo "Available presets:"
	@cmake --list-presets=all
	@echo ""
	@echo "Usage:"
	@echo "  make configure PRESET=<preset>  - Configure build (default: $(PRESET))"
	@echo "  make build PRESET=<preset>      - Build (default: $(PRESET))"
	@echo "  make test PRESET=<preset>       - Run tests (default: $(PRESET))"
	@echo "  make install PRESET=<preset>    - Install to system"
	@echo "  make clean                      - Clean all build artifacts"
	@echo ""
	@echo "Examples:"
	@echo "  make build                                      # Build with clang (default)"
	@echo "  make build PRESET=linux-gcc-release             # Build with gcc"
	@echo "  make test PRESET=linux-clang-release            # Run tests"
	@echo "  make install PRESET=linux-clang-release         # Install"

vcpkg-bootstrap:
	@echo "Bootstrapping vcpkg..."
	MAKELEVEL=0 cmake -P cmake/bootstrap/bootstrap-vcpkg.cmake

configure: vcpkg-bootstrap
	@echo "Configuring $(PRESET)..."
	MAKELEVEL=0 cmake --preset $(PRESET)

build: configure
	@echo "Building $(PRESET)..."
	cmake --build --preset $(PRESET)

test: build
	@echo "Running tests for $(PRESET)..."
	@if [ ! -f "$(CMAKE_BUILD_DIR)/env.sh" ]; then \
		echo "ERROR: $(CMAKE_BUILD_DIR)/env.sh not found."; \
		exit 1; \
	fi
	source $(CMAKE_BUILD_DIR)/env.sh && LD_LIBRARY_PATH=$$VCPKG_INSTALLED_DIR/$$VCPKG_TRIPLET/lib:$$LD_LIBRARY_PATH ctest --preset $(PRESET) --output-on-failure

install: build
	@echo "Installing $(PRESET) to system..."
	$(SUDO) cmake --install $(CMAKE_BUILD_DIR)

clean:
	@echo "Cleaning all build artifacts..."
	rm -rf build vcpkg_installed
	@echo "✓ Clean complete"

package:  
	mkdir -p packaging
	cp CMakeLists.txt packaging/
	cp vcpkg.json.release packaging/vcpkg.json
	cp CMakePresets.json packaging/CMakePresets.json
	cp -r ports/ packaging/ports
	cp -r cmake/ packaging/cmake
	cp Makefile packaging/Makefile
	cp README.md packaging/README.md
	cp LICENSE packaging/LICENSE
	cp -r CMakeFiles/ packaging/CMakeFiles
	cd packaging && $(MAKE) clean build $(PRESET)
	if [ "$$(uname -s | grep -i 'mingw\|msys\|cygwin')" ]; then \
		cd packaging/build && cpack -G ZIP -C Release; \
		mv packaging/build/*.zip build/ 2>/dev/null || true; \
		echo "✓ Windows package moved to build/"; \
	else \
		cd packaging/build && cpack -G TGZ -C Release; \
		mv packaging/build/*.tar.gz build/ 2>/dev/null || true; \
		echo "✓ Linux package moved to build/"; \
	fi
	rm -rf packaging
