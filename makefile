
## microhttpd. UBUNTU package. https://www.gnu.org/software/libmicrohttpd/
## sudo apt-get update
## sudo apt-get install libmicrohttpd-dev
PATH_TO_LIBMHD_INCLUDES = /usr/local/include
PATH_TO_LIBMHD_LIBS = /usr/local/lib

DEBUG_FLAG = -g
CFLAGS	= $(DEBUG_FLAG) 
LIBS = 
CC = gcc

SOURCES = utils.c queue.c nlkup.c logger.c hashtable.c config.c sessions.c json.c
HEADERS = nlkup.h queue.h logger.h hashtable.h config.h utils.h sessions.h json.h

OBJECTS = $(SOURCES:.c=.o)

all: server

nlkup: $(HEADERS) $(OBJECTS) nlkup.c 
	$(CC) nlkup.c $(CFLAGS) -pthread -o nlkup $(OBJECTS) $(LIBS)

%.o: %.c $(HEADERS) 
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	-rm -f $(OBJECTS)

hellobrowser: hellobrowser.c
	$(CC) $(CFLAGS) -pthread hellobrowser.c -o hellobrowser -I$PATH_TO_LIBMHD_INCLUDES  -L$PATH_TO_LIBMHD_LIBS -lmicrohttpd

server: server.c $(HEADERS) $(OBJECTS) nlkup.c 
	$(CC) $(CFLAGS) -pthread server.c -o server -I$PATH_TO_LIBMHD_INCLUDES  -L$PATH_TO_LIBMHD_LIBS -lmicrohttpd $(OBJECTS)
