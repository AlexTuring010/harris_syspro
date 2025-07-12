CC = gcc
CFLAGS = -Wall -pthread

OBJS = basics.o

all: nfs_manager nfs_console nfs_client

nfs_manager: nfs_manager.c $(OBJS)
	$(CC) $(CFLAGS) -o nfs_manager nfs_manager.c $(OBJS)

nfs_console: nfs_console.c $(OBJS)
	$(CC) $(CFLAGS) -o nfs_console nfs_console.c $(OBJS)

nfs_client: nfs_client.c $(OBJS)
	$(CC) $(CFLAGS) -o nfs_client nfs_client.c $(OBJS)

basics.o: basics.c basics.h
	$(CC) $(CFLAGS) -c basics.c

clean:
	rm -f *.o nfs_manager nfs_console nfs_client
