.PHONY: all configure build-debug build-release test clean install help check-vcpkg

# Platform detection (works on Linux, MINGW/MSYS, and native Windows)
UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
IS_MINGW := $(findstring MINGW,$(UNAME_S))
IS_CYGWIN := $(findstring CYGWIN,$(UNAME_S))
IS_WINDOWS_NT := $(filter Windows_NT,$(OS))
GENERATED_MANIFEST := generated_template_manifest.txt
ifeq ($(or $(IS_MINGW),$(IS_CYGWIN),$(IS_WINDOWS_NT)),)
  PLATFORM := linux
else
  PLATFORM := windows
endif

# Default compilers (user can override from environment)
ifeq ($(PLATFORM),windows)
  # prefer clang-cl when available; user can pass CC/ CXX to override
  CC ?= clang-cl
	CXX ?= clang-cl
  CMAKE_GENERATOR := Ninja
  VCPKG_TRIPLET := x64-windows
  VCPKG_DEBUG_BIN := $(PWD)/vcpkg_installed/x64-windows/bin
  VCPKG_RELEASE_LIB := $(PWD)/vcpkg_installed/x64-windows/lib
  EXE_SUFFIX := .exe
	NPROC := $(shell powershell -Command "[Environment]::ProcessorCount" 2>NUL || echo 4)
  STRIP_CMD := # no-op (strip not usually present); set to "llvm-strip" if you have it
	RUN_PREFIX := PATH=$(VCPKG_DEBUG_BIN):$(VCPKG_RELEASE_LIB):$$PATH
	SUDO ?= sudo
  PYTHON_EXECUTABLE ?= python
  # On Windows, Ninja + clang-cl: still pass CMAKE_C_COMPILER / CMAKE_CXX_COMPILER
else
  CMAKE_GENERATOR := Ninja
  VCPKG_TRIPLET := x64-linux-dynamic
  VCPKG_DEBUG_LIB := $(PWD)/vcpkg_installed/x64-linux-dynamic/debug/lib
  VCPKG_RELEASE_LIB := $(PWD)/vcpkg_installed/x64-linux-dynamic/lib
  EXE_SUFFIX :=
  NPROC := $(shell nproc 2>/dev/null || echo 4)
  STRIP_CMD := strip
	RUN_PREFIX := LD_LIBRARY_PATH=$(VCPKG_DEBUG_LIB):$(VCPKG_RELEASE_LIB):$$LD_LIBRARY_PATH
	SUDO :=
	PYTHON_EXECUTABLE ?= python3
	export CC=clang
	export CXX=clang++
endif

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
VCPKG_INSTALLED_DIR ?= $(CURDIR)/vcpkg_installed
FEED_URL ?= 
NUGET_API_KEY ?=
FEED_NAME ?= 
USERNAME ?=
VCPKG_BINARY_SOURCES ?= ""
ifeq ($(strip $(FEED_URL)),)
  CMAKE_VCPKG_BINARY_SOURCES :=
else
	VCPKG_BINARY_SOURCES := "nuget,$(FEED_URL),readwrite"
  CMAKE_VCPKG_BINARY_SOURCES := -DVCPKG_BINARY_SOURCES=$(VCPKG_BINARY_SOURCES)
endif
LINKER_FLAGS ?=
ifeq ($(PLATFORM),linux)
	LINKER_FLAGS := -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld" -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld"
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
	@if [ ! -f "$(VCPKG_ROOT)/vcpkg" ]; then \
		echo "Bootstrapping vcpkg..."; \
		if [ "$$(uname -s | grep -i 'mingw\|msys\|cygwin')" ]; then \
			echo "Skipping since on windows"; \
		else \
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
	@NUGET_EXE=$$(vcpkg fetch nuget | tail -n1); \
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
ifeq ($(PLATFORM),windows)
  VCPKG_CMD := vcpkg
else
  VCPKG_CMD := $(VCPKG_ROOT)/vcpkg
endif
	VCPKG_FEATURE_FLAGS=binarycaching MAKELEVEL=0\
		$(VCPKG_CMD) install \
		--overlay-ports=ports \
		--binarysource="$(VCPKG_BINARY_SOURCES)" \
		--triplet="$(VCPKG_TRIPLET)"

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

configure: check-vcpkg
	@echo "Configuring build..."
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_TOOLCHAIN) \
		-DVCPKG_INSTALLED_DIR=$(VCPKG_INSTALLED_DIR) \
		-DVCPKG_TARGET_TRIPLET=$(VCPKG_TRIPLET) \
		-DBUILD_TESTS=ON \
		-DUSE_CCACHE=ON \
		-DENABLE_PCH=ON \
		-DCMAKE_C_COMPILER=clang \
		-DCMAKE_CXX_COMPILER=clang++ \
		$(CMAKE_VCPKG_BINARY_SOURCES) \
		$(LINKER_FLAGS) \
		-DVCPKG_OVERLAY_PORTS=../ports \
		-G $(CMAKE_GENERATOR)
	@echo "✓ Build configured"

build: configure
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

# Test with Docker (recommended)
test: docker-up build
	@echo "Running tests with Docker services..."
	@$(MAKE) test-local \
		TEST_NATS_URL="nats://localhost:4222"
	@$(MAKE) docker-down
	@echo "✓ Tests complete, services stopped"

# Keep services running for interactive testing
test-interactive: docker-up build
	@echo "Services are running. Run tests manually with:"
	@echo "  export TEST_NATS_URL=nats://localhost:4222"
	@echo "  cd $(BUILD_DIR) && ctest"
	@echo ""
	@echo "Or run individual test executables:"
	@echo "  $(BUILD_DIR)/tests/falcon_routine_unit_tests"
	@echo "  $(BUILD_DIR)/tests/falcon_routine_integration_tests"
	@echo ""
	@echo "When done, stop services with: make docker-down"

package: build
	mkdir -p packaging
	cp CMakeLists.txt packaging/
	cp vcpkg.json.release packaging/vcpkg.json
	cp -r ports/ packaging/ports
	cp Makefile packaging/Makefile
	cp README.md packaging/README.md
	cp LICENSE packaging/LICENSE
	cp -r CMakeFiles/ packaging/CMakeFiles/
	cd packaging && $(MAKE) clean build
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
