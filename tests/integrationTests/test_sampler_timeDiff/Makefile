all:  test_sampler_timeDiff_main.exe

test_sampler_timeDiff_main.exe: test_sampler_timeDiff_main.o test_sampler_timeDiff.o
	gcc -o test_sampler_timeDiff_main.exe test_sampler_timeDiff_main.o test_sampler_timeDiff.o

 test_sampler_timeDiff_main.o:  test_sampler_timeDiff_main.c
	gcc -Wall -g -c  test_sampler_timeDiff_main.c	
	
test_sampler_timeDiff.o: test_sampler_timeDiff.c test_sampler_timeDiff.h
	gcc -Wall -g -c test_sampler_timeDiff.c	
	
clean:
	-rm -f *.exe
	-rm -f *.o

