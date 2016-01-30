CC = gcc
DEBUG_FLAGS = -g -O0 -DDEBUG
CFLAGS = $(DEBUG_FLAGS) -Wall
RM = rm -f

all: web_client web_server

web_client: web_client.o
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

web_server: web_server.o
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	$(RM) *.o web_client web_server
