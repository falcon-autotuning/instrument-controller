.PHONY: help configure build test clean install vcpkg-bootstrap 

# Default to using standard system shell  
SHELL ?= /bin/sh
export LOCALAPPDATA
export APPDATA
export TEMP
export TMP
export USERPROFILE

# Build preset (user can override: make build PRESET=linux-gcc-release)
PRESET ?= linux-clang-release
CMAKE_BUILD_DIR := build/$(PRESET)

ifeq ($(OS),Windows_NT)
  SUDO :=
else
  SUDO := sudo
endif

# Robust Windows OS detection (works on cmd, powershell, git bash, msys)
IS_WINDOWS :=
ifeq ($(OS),Windows_NT)
  IS_WINDOWS := yes
endif
ifneq ($(MSYSTEM),)
  IS_WINDOWS := yes
endif

ifdef ($(IS_WINDOWS),yes)
  CMAKE_ARGS := -D WIN32=TRUE
  RUN_CMAKE := MAKELEVEL=0 cmake
  BUILD_CMAKE := cmake
else
  CMAKE_ARGS :=
  RUN_CMAKE := MAKELEVEL=0 cmake
  BUILD_CMAKE := cmake
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

vcpkg-bootstrap:
	@echo "Bootstrapping vcpkg..."
	$(RUN_CMAKE) -D PRESET=$(PRESET) $(CMAKE_ARGS) -P cmake/bootstrap/bootstrap-vcpkg.cmake

configure: vcpkg-bootstrap
	@echo "Configuring $(PRESET)..."
	$(RUN_CMAKE) --preset $(PRESET)

build: configure
	@echo "Building $(PRESET)..."
	$(BUILD_CMAKE) --build --preset $(PRESET)

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
	cp -r tests/ packaging/tests
	cp Makefile packaging/Makefile
	cp README.md packaging/README.md
	cp LICENSE packaging/LICENSE
	cp -r CMakeFiles/ packaging/CMakeFiles
	# Copy NuGet credentials if present (speeds up authenticated package restores)
	if [ -f ".nuget-credentials" ]; then \
			cp .nuget-credentials packaging/.nuget-credentials; \
			echo "✓ Copied .nuget-credentials"; \
	fi
	cd packaging && $(MAKE) clean build PRESET=$(PRESET)
	if [ "$$(uname -s | grep -i 'mingw\|msys\|cygwin')" ]; then \
			cd packaging/build/$(PRESET) && \
			if [ -n "$(TAG)" ]; then \
				cpack -G ZIP -C Release -D CPACK_PACKAGE_FILE_NAME="instrument-controller-$(TAG)-Windows-AMD64"; \
			else \
				cpack -G ZIP -C Release; \
			fi && \
			mv *.zip ../../../build/$(PRESET)/ && \
			echo "✓ Windows package moved to build/$(PRESET)/"; \
	else \
			cd packaging/build/$(PRESET) && \
			if [ -n "$(TAG)" ]; then \
				cpack -G TGZ -C Release -D CPACK_PACKAGE_FILE_NAME="instrument-controller-$(TAG)-Linux-x86_64"; \
			else \
				cpack -G TGZ -C Release; \
			fi && \
			mv *.tar.gz ../../../build/$(PRESET)/ && \
			echo "✓ Linux package moved to build/$(PRESET)/"; \
	fi
	rm -rf packaging

package-release:
	@if [ -z "$(TAG)" ]; then \
		echo "ERROR: TAG is required. Usage: make package-release TAG=v0.1.3-alpha [PRESET=...]"; \
		exit 1; \
	fi
	@if ! command -v gh >/dev/null 2>&1; then \
		echo "ERROR: GitHub CLI ('gh') is not installed."; \
		exit 1; \
	fi
	@if ! gh auth status >/dev/null 2>&1; then \
		echo "ERROR: gh is not authenticated. Run 'gh auth login'."; \
		exit 1; \
	fi
	@PRESET_TO_USE="$(PRESET)"; \
	if [ "$$PRESET_TO_USE" = "linux-clang-release" ]; then \
		if [ -n "$(IS_WINDOWS)" ]; then \
			PRESET_TO_USE="windows-clang-cl-package"; \
		else \
			PRESET_TO_USE="linux-clang-package"; \
		fi \
	fi; \
	echo "Building package with preset $$PRESET_TO_USE..."; \
	$(MAKE) package PRESET=$$PRESET_TO_USE TAG=$(TAG); \
	\
	ACTUAL_BUILD_DIR="build/$$PRESET_TO_USE"; \
	if [ -n "$(IS_WINDOWS)" ]; then \
		PACKAGE_FILE="instrument-controller-$(TAG)-Windows-AMD64.zip"; \
	else \
		PACKAGE_FILE="instrument-controller-$(TAG)-Linux-x86_64.tar.gz"; \
	fi; \
	\
	echo "Creating customized install.sh..."; \
	cp scripts/install.sh $$ACTUAL_BUILD_DIR/install.sh; \
	sed -i "s/RELEASE_VERSION=\"\$${1:-v0.1.1-alpha}\"/RELEASE_VERSION=\"\$${1:-$(TAG)}\"/g" $$ACTUAL_BUILD_DIR/install.sh; \
	\
	echo "Uploading to GitHub releases..."; \
	if ! gh release view "$(TAG)" >/dev/null 2>&1; then \
		echo "Creating new draft release for tag $(TAG)..."; \
		gh release create "$(TAG)" --draft --title "Release $(TAG)" --notes "Pre-release version $(TAG)"; \
	fi; \
	\
	echo "Uploading release assets..."; \
	cd $$ACTUAL_BUILD_DIR && \
	gh release upload "$(TAG)" "$$PACKAGE_FILE" "install.sh" --clobber && \
	echo "--------------------------------------------------" && \
	echo "Release $(TAG) completed." && \
	echo "Release URL: $$(gh release view "$(TAG)" --web)"
