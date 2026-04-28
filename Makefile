.PHONY: all configure build-debug build-release test clean install help check-vcpkg

# Platform detection (works on Linux, MINGW/MSYS, and native Windows)
UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
IS_MINGW := $(findstring MINGW,$(UNAME_S))
IS_CYGWIN := $(findstring CYGWIN,$(UNAME_S))
IS_WINDOWS_NT := $(filter Windows_NT,$(OS))
ifeq ($(or $(IS_MINGW),$(IS_CYGWIN),$(IS_WINDOWS_NT)),)
  PLATFORM := linux
else
  PLATFORM := windows
endif

# Default compilers (user can override from environment)
ifeq ($(PLATFORM),windows)
  CMAKE_GENERATOR := Ninja
  export VCPKG_TRIPLET := x64-win-llvm
  EXE_SUFFIX := .exe
	NPROC := $(shell powershell -Command "[Environment]::ProcessorCount" 2>NUL || echo 4)
	SUDO ?= 
  export CC=clang-cl
	export CXX=clang-cl
else
  CMAKE_GENERATOR := Ninja
  export VCPKG_TRIPLET := x64-linux-dynamic
  EXE_SUFFIX :=
  NPROC := $(shell nproc 2>/dev/null || echo 4)
	SUDO := sudo
	export CC=clang
	export CXX=clang++
endif

# Paths
ENV_FILE := .nuget-credentials
ifeq ($(wildcard $(ENV_FILE)),)
  $(info [Makefile] $(ENV_FILE) not found, skipping environment sourcing)
else
  include $(ENV_FILE)
  export $(shell sed 's/=.*//' $(ENV_FILE) | xargs)
  $(info [Makefile] Loaded environment from $(ENV_FILE))
endif
# ── Paths ─────────────────────────────────────────────────────────────────────
VCPKG_ROOT ?= $(CURDIR)/vcpkg
VCPKG_TOOLCHAIN ?= $(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
export VCPKG_INSTALLED_DIR ?= $(CURDIR)/vcpkg_installed
FEED_URL ?= 
NUGET_API_KEY ?=
FEED_NAME ?= 
USERNAME ?=
VCPKG_BINARY_SOURCES ?= 
ifeq ($(strip $(FEED_URL)),)
  CMAKE_VCPKG_BINARY_SOURCES :=
else
	VCPKG_BINARY_SOURCES := nuget,$(FEED_URL),readwrite
  CMAKE_VCPKG_BINARY_SOURCES := -DVCPKG_BINARY_SOURCES="$(VCPKG_BINARY_SOURCES)"
endif
LINKER_FLAGS ?=
ifeq ($(PLATFORM),linux)
	LINKER_FLAGS := -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld" -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld"
endif
VCPKG_OVERLAY_TRIPS ?=
ifeq ($(PLATFORM),windows)
	VCPKG_OVERLAY_TRIPS := -DVCPKG_OVERLAY_TRIPLETS=../my-vcpkg-triplets
endif

BUILD_DIR := build

INSTALL_PREFIX    ?= /opt/falcon
INSTALL_LIBDIR    := $(INSTALL_PREFIX)/lib
INSTALL_INCLUDEDIR := $(INSTALL_PREFIX)/include
INSTALL_CMAKEDIR  := $(INSTALL_LIBDIR)/cmake/falcon-routine

# Default target
all: build-release

help:
	@echo "Instrument Controller Build System"
	@echo "===================================="
	@echo ""
	@echo "Build targets:"
	@echo "  make build              - Build integration tests"
	@echo "  make configure          - Configure builds"
	@echo ""
	@echo "Clean targets:"
	@echo "  make clean              - Clean build artifacts and test containers"
	@echo ""
	@echo "Test targets:"
	@echo "  make test               - Run tests with Docker (recommended, starts containers automatically)"
	@echo "  make test-interactive   - Start Docker services for manual/interactive testing"
	@echo ""
	@echo "Test Service targets:"
	@echo "  make docker-up          - Start test services (PostgreSQL + NATS)"
	@echo "  make docker-down        - Stop test services"
	@echo "  make docker-clean       - Clean test containers and data"
	@echo "  make docker-logs        - Show service logs"
	@echo ""
	@echo "Environment variables:"
	@echo "  VCPKG_ROOT              - Path to vcpkg root (default: ../.vcpkg)"
	@echo "  VCPKG_TRIPLET           - vcpkg triplet (default: x64-linux-dynamic)"
	@echo "  TEST_NATS_URL           - NATS connection for tests"
	@echo ""
	@echo "Current configuration:"
	@echo "  Platform: $(PLATFORM)"
	@echo "  Generator: $(CMAKE_GENERATOR)"
	@echo "  Triplet: $(VCPKG_TRIPLET)"

.PHONY: vcpkg-bootstrap
vcpkg-bootstrap:
	@if [ ! -d "$(VCPKG_ROOT)" ]; then \
		echo "Cloning vcpkg..."; \
		git clone https://github.com/microsoft/vcpkg.git $(VCPKG_ROOT); \
	fi
	@if [ ! -f "$(VCPKG_ROOT)/vcpkg" ] && [ ! -f "$(VCPKG_ROOT)/vcpkg.exe" ]; then \
		echo "Bootstrapping vcpkg..."; \
		UNAME="$$(uname -s 2>/dev/null || echo Unknown)"; \
		if echo "$$UNAME" | grep -i -q 'mingw\|msys\|cygwin'; then \
			echo "Detected Windows bash environment ($$UNAME). Using cmd.exe to launch bootstrap-vcpkg.bat"; \
			BAT_PATH="$$(cygpath -w "$(VCPKG_ROOT)/bootstrap-vcpkg.bat")"; \
			cmd.exe //C "$$BAT_PATH"; \
			git clone https://github.com/Neumann-A/my-vcpkg-triplets.git || true; \
		else \
			echo "Detected Unix environment ($$UNAME). Using bootstrap-vcpkg.sh"; \
			cd $(VCPKG_ROOT) && ./bootstrap-vcpkg.sh; \
		fi \
	fi

setup-nuget-auth:
	@if [ -z "$$NUGET_API_KEY" ]; then \
		echo "No .nuget_api_key or NUGET_API_KEY found, skipping NuGet setup (local-only build, no binary cache)."; \
		exit 0; \
	fi
	@echo "Setting up NuGet authentication for vcpkg binary caching..."
	@if [ "$$(uname -s 2>/dev/null)" != "Windows_NT" ] && [ "$$(uname -o 2>/dev/null)" != "Msys" ] && [ "$$(uname -o 2>/dev/null)" != "Cygwin" ]; then \
		if ! command -v mono >/dev/null 2>&1; then \
			echo "Error: mono is not installed. Please install mono (e.g., 'sudo pacman -S mono' on Arch, 'sudo apt install mono-complete' on Ubuntu)."; \
			exit 1; \
		fi; \
	fi
	@NUGET_EXE=$$($(VCPKG_ROOT)/vcpkg fetch nuget | tail -n1); \
	if [ "$$(uname -s 2>/dev/null)" = "Linux" ]; then \
		MONO_PREFIX="mono "; \
	else \
		MONO_PREFIX=""; \
	fi; \
	$$MONO_PREFIX"$$NUGET_EXE" sources remove -Name "$(FEED_NAME)" || true; \
	$$MONO_PREFIX"$$NUGET_EXE" sources add -Name "$(FEED_NAME)" -Source "$(FEED_URL)" -Username "$(USERNAME)" -Password "$(NUGET_API_KEY)";

.PHONY: vcpkg-install-deps
vcpkg-install-deps: setup-nuget-auth 
	@echo "Installing vcpkg dependencies" 
	VCPKG_FEATURE_FLAGS=binarycaching VCPKG_OVERLAY_TRIPLETS=my-vcpkg-triplets \
		$(VCPKG_ROOT)/vcpkg install \
		--overlay-ports=ports \
		--binarysource="$(VCPKG_BINARY_SOURCES)" \
		--triplet="$(VCPKG_TRIPLET)" \
		--vcpkg-root="$(VCPKG_ROOT)"

check-vcpkg: vcpkg-bootstrap  vcpkg-install-deps
	@echo "Checking vcpkg configuration..."
	@if [ ! -d "$(VCPKG_ROOT)" ]; then \
		echo "Error: vcpkg not found at $(VCPKG_ROOT)"; \
		echo "Run 'make deps' in the parent directory first"; \
		exit 1; \
	fi
	@if [ ! -f "$(VCPKG_TOOLCHAIN)" ]; then \
		echo "Error: vcpkg toolchain not found at $(VCPKG_TOOLCHAIN)"; \
		exit 1; \
	fi
	@echo "✓ vcpkg configuration OK"

configure-debug : check-vcpkg
	@echo "Configuring debug build..."
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_TOOLCHAIN) \
		-DVCPKG_INSTALLED_DIR=$(VCPKG_INSTALLED_DIR) \
		$(VCPKG_OVERLAY_TRIPS) \
		-DVCPKG_TARGET_TRIPLET=$(VCPKG_TRIPLET) \
		-DBUILD_TESTS=ON \
		-DUSE_CCACHE=ON \
		-DENABLE_PCH=ON \
		-DCMAKE_C_COMPILER=$(CC) \
		-DCMAKE_CXX_COMPILER=$(CXX) \
		$(CMAKE_VCPKG_BINARY_SOURCES) \
		$(LINKER_FLAGS) \
		-DVCPKG_OVERLAY_PORTS=../ports \
		-G $(CMAKE_GENERATOR)
	@echo "✓ Build configured"

configure-release: check-vcpkg
	@echo "Configuring debug build..."
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. \
		-DCMAKE_BUILD_TYPE=Release\
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_TOOLCHAIN) \
		-DVCPKG_INSTALLED_DIR=$(VCPKG_INSTALLED_DIR) \
		$(VCPKG_OVERLAY_TRIPS) \
		-DVCPKG_TARGET_TRIPLET=$(VCPKG_TRIPLET) \
		-DUSE_CCACHE=ON \
		-DENABLE_PCH=ON \
		-DCMAKE_C_COMPILER=$(CC) \
		-DCMAKE_CXX_COMPILER=$(CXX) \
		$(CMAKE_VCPKG_BINARY_SOURCES) \
		$(LINKER_FLAGS) \
		-DVCPKG_OVERLAY_PORTS=../ports \
		-G $(CMAKE_GENERATOR)
	@echo "✓ Build configured"

build-debug: configure-debug
	@echo "Building debug..."
	cmake --build $(BUILD_DIR) -- -j$(NPROC)
	@echo "✓ Debug build complete"
	@$(MAKE) clangd-helpers

build-release: configure-release
	@echo "Building debug..."
	cmake --build $(BUILD_DIR) -- -j$(NPROC)
	@echo "✓ Debug build complete"
	@$(MAKE) clangd-helpers

clean:
	@echo "Cleaning build artifacts and test containers..."
	rm -rf $(BUILD_DIR) build/ compile_commands.json ./vcpkg_installed/
	@echo "✓ Clean complete"

.PHONY: clangd-helpers
clangd-helpers:
	@if [ -f $(BUILD_DIR)/compile_commands.json ]; then \
		ln -sf $(BUILD_DIR)/compile_commands.json compile_commands.json; \
		echo "✓ clangd compile_commands.json symlinked"; \
	fi

# Docker test targets
.PHONY: docker-up docker-down docker-clean docker-logs test-with-docker

check-docker:
	@if ! command -v docker-compose >/dev/null 2>&1; then \
		echo "Error: docker-compose not found"; \
		echo "Install Docker Desktop or docker-compose to run tests"; \
		exit 1; \
	fi

docker-up: check-docker
	@echo "Starting test services (PostgreSQL + NATS)..."
	@docker-compose -f docker-compose.test.yml up -d
	@echo "Waiting for services to be ready..."
	@for i in 1 2 3 4 5; do \
		if docker-compose -f docker-compose.test.yml exec -T postgres pg_isready -U falcon_test >/dev/null 2>&1 && \
		   docker-compose -f docker-compose.test.yml exec -T nats wget -q -O- http://localhost:8222/healthz >/dev/null 2>&1; then \
			echo "✓ Services are ready"; \
			exit 0; \
		fi; \
		echo "  Waiting... ($$i/5)"; \
		sleep 3; \
	done; \
	echo "✗ Services failed to start"; \
	exit 1

docker-down:
	@echo "Stopping test services..."
	@docker-compose -f docker-compose.test.yml down
	@echo "✓ Services stopped"

docker-clean:
	@echo "Cleaning test data..."
	@docker-compose -f docker-compose.test.yml down -v
	@echo "✓ Test data cleaned"

docker-logs:
	@docker-compose -f docker-compose.test.yml logs -f

test: docker-up build-release
	@echo "Running tests with Docker services..."
	@$(MAKE) test-local \
		TEST_NATS_URL="nats://localhost:4222"
	@$(MAKE) docker-down
	@echo "✓ Tests complete, services stopped"

package: 
	mkdir -p packaging
	cp CMakeLists.txt packaging/
	cp vcpkg.json.release packaging/vcpkg.json
	cp -r ports/ packaging/ports
	cp Makefile packaging/Makefile
	cp README.md packaging/README.md
	cp LICENSE packaging/LICENSE
	cp -r CMakeFiles/ packaging/CMakeFiles/
	cd packaging && $(MAKE) clean build-release
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
