# eslide Makefile

# Program name
PROGRAM = eslide

# Source files
SOURCES = main.c common.c media.c slideshow.c clock.c ui.c

# Object files
OBJECTS = $(SOURCES:.c=.o)

# Compiler
CC = gcc

# Get EFL compilation flags using pkg-config
CFLAGS = `pkg-config --cflags elementary emotion`
LDFLAGS = `pkg-config --libs elementary emotion`

# Additional compiler flags for cross-platform compatibility
CFLAGS += -Wall -Wextra -std=c99
CFLAGS += -D_GNU_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L

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

# Check if EFL is properly installed
check-deps:
	@echo "Checking EFL dependencies..."
	@pkg-config --exists elementary && echo "✓ Elementary (EFL) found" || echo "✗ Elementary (EFL) not found"
	@pkg-config --modversion elementary 2>/dev/null && echo "EFL Version: `pkg-config --modversion elementary`" || echo "Could not determine EFL version"

# Help target
help:
	@echo "Available targets:"
	@echo "  all        - Build the program (default)"
	@echo "  clean      - Remove build artifacts"
	@echo "  run        - Build and run the program"
	@echo "  check-deps - Check if EFL dependencies are installed"
	@echo "  install    - Install the program to /usr/local/bin"
	@echo "  uninstall  - Remove the program from /usr/local/bin"
	@echo "  help       - Show this help message"

# Declare phony targets
.PHONY: all clean install uninstall run check-deps help
