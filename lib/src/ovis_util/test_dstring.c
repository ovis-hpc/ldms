#include "dstring.h"
#include "stdio.h"
#include "stdlib.h"
/* macros for convenience in typical use of one dynamic string in a function */
/* paste as needed in application code. */
/** declare and init a dstring named ds */
#define dsinit \
	dstring_t ds; \
	dstr_init(&ds)

/** declare and init an oversized dstring named ds with initial capacity cap.*/
#define dsinit2(cap) \
	dstring_t ds; \
	dstr_init2(&ds,cap)

/** append a dstring with  null terminated string char_star_x.*/
#define dscat(char_star_x) \
	dstrcat(&ds, char_star_x, DSTRING_ALL)

/** create real string (char *) from ds and reset ds, freeing any internal memory allocated. returns a char* the caller must free later. */
#define dsdone \
	dstr_extract(&ds)


/* simple test until we get unit testing in place. */


char *createfoo (int lim) 
{
	int i;
	dsinit;
	for (i=0; i < lim; i++) {
		dscat("1000 years luck");
	}
	return dsdone;
}

int main(int argc, char **argv) {
char * x, *y;
x = createfoo(10);
printf("%s\n",x);
free(x);
y=createfoo(10000);
free(y);
dsinit2(100000);
int i,lim=10000;
for (i=0; i < lim; i++) {
	dscat("1000 years luck");
}
char *res = dsdone;
free(res);
return 0;
}

