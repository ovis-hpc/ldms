CC=gcc
CFLAGS=-c -Wall -I/home/brandt/Ann/ldms.usr/include -I/usr/lib64/glib-2.0/include -I/usr/include/glib-2.0 
#-I/home/brandt/Ann/ldms/nodeobjsrc
LDFLAGS =-L/home/brandt/Ann/ldms.usr/lib -l ldms -L/usr/lib64/glib-2.0/lib -lglib-2.0 -L/home/brandt/Ann/ldms/nodeobjsrc -lmetricmap

all: libmetricmap.a hwlocldms_ls 

hwlocldms_ls: hwlocldms_ls.o
	$(CC) $(LDFLAGS) hwlocldms_ls.o -o hwlocldms_ls

hwlocldms_ls.o: hwlocldms_ls.c
	$(CC) $(CFLAGS) hwlocldms_ls.c

libmetricmap.a: metricmap.o
	ar -cvq libmetricmap.a metricmap.o

metricmap.o: metricmap.c metricmap.h
	$(CC) $(CFLAGS) metricmap.c -o metricmap.o


clean:
	 rm *.o  hwlocldms_ls.o hwlocldms_ls libmetricmap.a
