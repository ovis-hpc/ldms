CC=gcc
CFLAGS=-c -Wall -I/home/brandt/Ann/sos.usr/include -I/home/brandt/Ann/sos/src
LDFLAGS =-L/home/brandt/Ann/sos.usr/lib -lsos -lidx -lods -loidx -lm

all: stats

stats: stats.o
	$(CC) $(LDFLAGS) stats.o -o stats

stats.o: stats.c
	$(CC) $(CFLAGS) stats.c

clean:
	 rm *.o  stats
