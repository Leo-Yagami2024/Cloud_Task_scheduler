# =========================================
# Cloud Task Scheduler - Makefile
# =========================================

CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -g
LDFLAGS  = -lncurses -ltinfo

SRC_DIR  = src
BIN_DIR  = bin

SERVER_SRC = $(SRC_DIR)/server.c
CLIENT_SRC = $(SRC_DIR)/client.c

SERVER_BIN = $(BIN_DIR)/server
CLIENT_BIN = $(BIN_DIR)/client

# Default target
all: directories $(SERVER_BIN) $(CLIENT_BIN)

# Create bin directory if not exists
directories:
	mkdir -p $(BIN_DIR)

# Build server
$(SERVER_BIN): $(SERVER_SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Build client
$(CLIENT_BIN): $(CLIENT_SRC)
	$(CC) $(CFLAGS) $< -o $@ -lncurses

# Run server
run-server: $(SERVER_BIN)
	./$(SERVER_BIN)

# Run client
run-client: $(CLIENT_BIN)
	./$(CLIENT_BIN)

# Clean build + runtime files
clean:
	rm -rf $(BIN_DIR)
	rm -rf jobs
	rm -f *.db *.txt

# Rebuild everything
rebuild: clean all

# Debug build (extra flags)
debug:
	$(CC) $(CFLAGS) -DDEBUG $(SERVER_SRC) -o $(SERVER_BIN) $(LDFLAGS)
	$(CC) $(CFLAGS) -DDEBUG $(CLIENT_SRC) -o $(CLIENT_BIN) -lncurses

# Phony targets
.PHONY: all clean rebuild run-server run-client directories debug
