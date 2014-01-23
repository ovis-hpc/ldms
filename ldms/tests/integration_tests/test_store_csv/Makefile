all:  test_time_difference_main.exe

test_time_difference_main.exe:test_time_difference_main.o test_time_difference.o
	gcc -o test_time_difference_main.exe test_time_difference_main.o test_time_difference.o

test_time_difference_main.o: test_time_difference_main.c
	gcc -Wall -g -c  test_time_difference_main.c	
	
test_time_difference.o: test_time_difference.c test_time_difference.h
	gcc -Wall -g -c test_time_difference.c	
	
clean:
	-rm -f *.exe
	-rm -f *.o

