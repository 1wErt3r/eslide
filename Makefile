# eslide Makefile

# Program name
PROGRAM = eslide

# Source files
SOURCES = main.c common.c media.c slideshow.c clock.c ui.c config.c weather.c

# Object files
OBJECTS = $(SOURCES:.c=.o)

# Compiler
CC = gcc

# Get EFL compilation flags using pkg-config (add Eet for persistence, ecore-con for HTTP, and libxml2 for XML parsing)
CFLAGS = `pkg-config --cflags elementary emotion eet ecore-con libxml-2.0`
LDFLAGS = `pkg-config --libs elementary emotion eet ecore-con libxml-2.0`

# Additional compiler flags for cross-platform compatibility
CFLAGS += -std=c99
CFLAGS += -D_GNU_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L

# Warning levels - progressively more strict
WARN_BASIC = -Wall
WARN_MEDIUM = $(WARN_BASIC) -Wextra -Wformat=2 -Wstrict-prototypes
WARN_STRICT = $(WARN_MEDIUM) -Wpedantic -Wcast-align -Wcast-qual -Wconversion -Wsign-conversion
WARN_PEDANTIC = $(WARN_STRICT) -Wshadow -Wredundant-decls -Wunreachable-code -Wwrite-strings -Wpointer-arith

# Default warning level (currently basic)
CFLAGS += $(WARN_BASIC)

# Default target
all: $(PROGRAM)

# Build the program
$(PROGRAM): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(PROGRAM) $(LDFLAGS)

# Compile source files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(PROGRAM)

# Install target (optional)
install: $(PROGRAM)
	cp $(PROGRAM) /usr/local/bin/

# Uninstall target (optional)
uninstall:
	rm -f /usr/local/bin/$(PROGRAM)

# Run the program
run: $(PROGRAM)
	./$(PROGRAM)

# Warning level targets - build with specific warning levels
warn-basic: clean
	@echo "Building with basic warnings (-Wall)..."
	$(MAKE) CFLAGS="$(shell echo '$(CFLAGS)' | sed 's/$(WARN_BASIC)/$(WARN_BASIC)/')"

warn-medium: clean
	@echo "Building with medium warnings (-Wall -Wextra -Wformat=2 -Wstrict-prototypes)..."
	$(MAKE) CFLAGS="$(shell echo '$(CFLAGS)' | sed 's/$(WARN_BASIC)/$(WARN_MEDIUM)/')"

warn-strict: clean
	@echo "Building with strict warnings (includes -Wpedantic -Wcast-align -Wcast-qual -Wconversion -Wsign-conversion)..."
	$(MAKE) CFLAGS="$(shell echo '$(CFLAGS)' | sed 's/$(WARN_BASIC)/$(WARN_STRICT)/')"

warn-pedantic: clean
	@echo "Building with pedantic warnings (includes -Wshadow -Wredundant-decls -Wunreachable-code -Wwrite-strings -Wpointer-arith)..."
	$(MAKE) CFLAGS="$(shell echo '$(CFLAGS)' | sed 's/$(WARN_BASIC)/$(WARN_PEDANTIC)/')"

# Check warnings at all levels
check-warnings:
	@echo "=== Checking code with progressive warning levels ==="
	@echo
	@echo "1. Testing with BASIC warnings..."
	@$(MAKE) warn-basic > /dev/null 2>&1 && echo "✓ BASIC warnings: PASS" || echo "✗ BASIC warnings: FAIL"
	@echo
	@echo "2. Testing with MEDIUM warnings..."
	@$(MAKE) warn-medium > /dev/null 2>&1 && echo "✓ MEDIUM warnings: PASS" || echo "✗ MEDIUM warnings: FAIL"
	@echo
	@echo "3. Testing with STRICT warnings..."
	@$(MAKE) warn-strict > /dev/null 2>&1 && echo "✓ STRICT warnings: PASS" || echo "✗ STRICT warnings: FAIL"
	@echo
	@echo "4. Testing with PEDANTIC warnings..."
	@$(MAKE) warn-pedantic > /dev/null 2>&1 && echo "✓ PEDANTIC warnings: PASS" || echo "✗ PEDANTIC warnings: FAIL"
	@echo
	@echo "=== Warning check complete ==="

# Check if EFL is properly installed
check-deps:
	@echo "Checking EFL dependencies..."
	@pkg-config --exists elementary && echo "✓ Elementary (EFL) found" || echo "✗ Elementary (EFL) not found"
	@pkg-config --modversion elementary 2>/dev/null && echo "EFL Version: `pkg-config --modversion elementary`" || echo "Could not determine EFL version"

# Help target
help:
	@echo "Available targets:"
	@echo "  all            - Build the program (default)"
	@echo "  clean          - Remove build artifacts"
	@echo "  run            - Build and run the program"
	@echo "  check-deps     - Check if EFL dependencies are installed"
	@echo "  install        - Install the program to /usr/local/bin"
	@echo "  uninstall      - Remove the program from /usr/local/bin"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Warning level targets (progressive difficulty):"
	@echo "  warn-basic     - Build with basic warnings (-Wall)"
	@echo "  warn-medium    - Build with medium warnings (adds -Wextra, -Wformat=2, -Wstrict-prototypes)"
	@echo "  warn-strict    - Build with strict warnings (adds -Wpedantic, -Wcast-*, -Wconversion)"
	@echo "  warn-pedantic  - Build with pedantic warnings (adds -Wshadow, -Wredundant-decls, etc.)"
	@echo "  check-warnings - Test code against all warning levels"

# Test target for cache functionality
test-cache: test_cache
	@echo "Running cache test..."
	./test_cache

test_cache: test_cache.o
	gcc test_cache.o -o test_cache

# Declare phony targets
.PHONY: all clean install uninstall run check-deps help warn-basic warn-medium warn-strict warn-pedantic check-warnings test-cache
