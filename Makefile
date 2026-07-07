.PHONY: help configure build test clean install vcpkg-bootstrap 

# Default to using standard system shell  
SHELL ?= /bin/sh
export MSYS_NO_PATHCONV=1

# Build preset (user can override: make build PRESET=linux-gcc-release)
PRESET ?= linux-clang-release
CMAKE_BUILD_DIR := build/$(PRESET)
# Robust Windows OS detection (works on cmd, powershell, git bash, msys)
IS_WINDOWS :=
ifeq ($(OS),Windows_NT)
  IS_WINDOWS := yes
else
  ifneq ($(findstring MINGW,$(shell uname -s 2>/dev/null)),)
    IS_WINDOWS := yes
  endif
  ifneq ($(findstring MSYS,$(shell uname -s 2>/dev/null)),)
    IS_WINDOWS := yes
  endif
endif

ifdef IS_WINDOWS
  SUDO :=
  BOOTSTRAP_PATH := $(shell cygpath -w cmake/bootstrap/bootstrap-vcpkg.cmake 2>/dev/null || echo cmake/bootstrap/bootstrap-vcpkg.cmake)
else
  SUDO := sudo
  BOOTSTRAP_PATH := cmake/bootstrap/bootstrap-vcpkg.cmake
endif

# Default target
all: build

help:
	@echo "Available targets:"
	@echo "  make build PRESET=<preset>      - Build the project (default: $(PRESET))"
	@echo "  make test PRESET=<preset>       - Run tests (default: $(PRESET))"
	@echo "  make install PRESET=<preset>    - Install to system"
	@echo "  make clean                      - Clean all build artifacts"
	@echo ""
	@echo "Examples:"
	@echo "  make build                                      # Build with clang (default)"
	@echo "  make build PRESET=linux-gcc-release             # Build with gcc"
	@echo "  make test PRESET=linux-clang-release            # Run tests"
	@echo "  make install PRESET=linux-clang-release         # Install"

test-os:
	@echo "OS is: '$(OS)'"
	@echo "uname is: '$(shell uname -s 2>/dev/null)'"
	@echo "IS_WINDOWS is: '$(IS_WINDOWS)'"
	@echo "BOOTSTRAP_PATH is: '$(BOOTSTRAP_PATH)'"

vcpkg-bootstrap:
	@echo "Bootstrapping vcpkg..."
	MAKELEVEL=0 cmake -P "$(BOOTSTRAP_PATH)"

configure: vcpkg-bootstrap
	@echo "Configuring $(PRESET)..."
	MAKELEVEL=0 cmake --preset $(PRESET)

build: configure
	@echo "Building $(PRESET)..."
	cmake --build --preset $(PRESET)

# use POSIX . instead of source to ensure compatibility with /bin/sh on all platforms
test: build
	@echo "Running tests for $(PRESET)..."
	@if [ ! -f "$(CMAKE_BUILD_DIR)/env.sh" ]; then \
		echo "ERROR: $(CMAKE_BUILD_DIR)/env.sh not found."; \
		exit 1; \
	fi
	. $(CMAKE_BUILD_DIR)/env.sh && LD_LIBRARY_PATH=$$VCPKG_INSTALLED_DIR/$$VCPKG_TRIPLET/lib:$$LD_LIBRARY_PATH ctest --preset $(PRESET) --output-on-failure

install: build
	@echo "Installing $(PRESET) to system..."
	$(SUDO) cmake --install $(CMAKE_BUILD_DIR)

clean:
	@echo "Cleaning all build artifacts..."
	rm -rf build vcpkg_installed
	@echo "✓ Clean complete"

package:  
	mkdir -p packaging
	mkdir -p build
	mkdir -p build/$(PRESET)
	cp CMakeLists.txt packaging/
	cp vcpkg.json.release packaging/vcpkg.json
	cp CMakePresets.json packaging/CMakePresets.json
	cp -r ports/ packaging/ports
	cp -r cmake/ packaging/cmake
	cp Makefile packaging/Makefile
	cp README.md packaging/README.md
	cp LICENSE packaging/LICENSE
	cp -r CMakeFiles/ packaging/CMakeFiles
	# Copy NuGet credentials if present (speeds up authenticated package restores)
	if [ -f ".nuget-credential" ]; then \
			cp .nuget-credential packaging/.nuget-credential; \
			echo "✓ Copied .nuget-credential"; \
	fi
	cd packaging && $(MAKE) clean build PRESET=$(PRESET)
	if [ "$$(uname -s | grep -i 'mingw\|msys\|cygwin')" ]; then \
			cd packaging/build/$(PRESET) && \
			cpack -G ZIP -C Release && \
			mv *.zip ../../../build/$(PRESET)/ && \
			echo "✓ Windows package moved to build/$(PRESET)/"; \
	else \
			cd packaging/build/$(PRESET) && \
			cpack -G TGZ -C Release && \
			mv *.tar.gz ../../../build/$(PRESET)/ && \
			echo "✓ Linux package moved to build/$(PRESET)/"; \
	fi
	rm -rf packaging
