
# =========================================
# Cloud Task Scheduler - Makefile
# ======================================
# =========================================
# Cloud Task Scheduler - Makefile
# =========================================

CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDFLAGS = -lncurses -ltinfo

SERVER = server
CLIENT = client

all: $(SERVER) $(CLIENT)

# ---- Build server ----
$(SERVER): server.c
	$(CC) $(CFLAGS) server.c -o $(SERVER) $(LDFLAGS)

# ---- Build client ----
$(CLIENT): client.c
	$(CC) $(CFLAGS) client.c -o $(CLIENT) -lncurses

# ---- Run server ----
run-server: $(SERVER)
	./$(SERVER)

# ---- Run client ----
run-client: $(CLIENT)
	./$(CLIENT)

# ---- Clean ----
clean:
	rm -f $(SERVER) $(CLIENT)
	rm -rf jobs *.db *.txt

# ---- Rebuild ----
rebuild: clean all
