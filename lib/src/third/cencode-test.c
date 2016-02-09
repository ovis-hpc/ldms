/*
derived from c-example1.c - libb64 example code

This is a short example of how to use libb64's C interface to encode

*/

#include <cencode.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


/* arbitrary buffer size */
#define SIZE 100

#define U6 0xffffffffffff
#define U8 0xffffffffffffffff
#define V8 0x7777777777777777

char* encode(const char* input)
{
	/* set up a destination buffer large enough to hold the encoded data */
	char* output = (char*)malloc(SIZE);
	/* keep track of our encoded position */
	char* c = output;
	/* store the number of bytes encoded by a single call */
	int cnt = 0;
	/* we need an encoder state */
	base64_encodestate s;
	uint64_t iu6[2] = {V8,0};
	/*---------- START ENCODING ----------*/
	/* initialise the encoder state */
	base64_init_encodestate(&s);
	/* gather data from the input and send it to the output */
	cnt = base64_encode_block((char *)iu6, 6, c, &s);
	c += cnt;
	/* since we have encoded the entire input string, we know that 
	   there is no more input data; finalise the encoding */
	cnt = base64_encode_blockend(c, &s);
	c += cnt;
	/*---------- STOP ENCODING  ----------*/
	
	/* we want to print the encoded data, so null-terminate it: */
	*c = 0;
	
	return output;
}

char* encodeu64(uint64_t v)
{
	char* output = malloc(10);
	char* c = output;
	int cnt = 0;
	base64_encodestate s;
	uint64_t iu6[2] = {v,0};
	base64_init_encodestate(&s);
	cnt = base64_encode_block((char *)iu6, 6, c, &s);
	c += cnt;
	cnt = base64_encode_blockend(c, &s);
	c += cnt;
	*c = 0;
	
	return output;
}

int main()
{
	const char* input = "hello world";
	char* encoded;
	
	
	/* encode the data */
	encoded = encode(input);
	printf("encoded: %s", encoded); 
	char *enczero = encodeu64(0);
	printf("enczero: %s\n", enczero); 
	free(enczero);
	free(encoded);
	return 0;
}


