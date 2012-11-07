CC=gcc
CFLAGS=-c -Wall -I/home/brandt/Ann/sos.usr/include -I/home/brandt/Ann/sos/src
LDFLAGS =-L/home/brandt/Ann/sos.usr/lib -lsos -lidx -lods -loidx -lm

all: stats stats_multi

stats: stats.o
	$(CC) $(LDFLAGS) stats.o -o stats

stats.o: stats.c
	$(CC) $(CFLAGS) stats.c

stats_multi: stats_multi.o
	$(CC) $(LDFLAGS) stats_multi.o -o stats_multi

stats_multi.o: stats_multi.c
	$(CC) $(CFLAGS) stats_multi.c

test: test.o
	$(CC) -lpthread -lm test.o -o test

test.o: test.c
	$(CC) -c -Wall test.c 

clean:
	 rm *.o  stats_multi stats test
