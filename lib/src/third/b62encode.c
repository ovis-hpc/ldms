/* 	b62encode.c -- base62 encoding routine
	Copyright (C) 2001-2003 Mark Weaver
	Written by Mark Weaver <mark@npsl.co.uk>

	Part of the mailparse library.
	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Library General Public
	License as published by the Free Software Foundation; either
	version 2 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Library General Public License for more details.

	You should have received a copy of the GNU Library General Public
	License along with this library; if not, write to the
	Free Software Foundation, Inc., 59 Temple Place - Suite 330,
	Boston, MA  02111-1307, USA.
*/
/* bug fixes (len==4 case) and c99 port by Ben Allan, Sandia Corporation */

#include <stdint.h>

/* pseudo-base62 encode 
	base62 encoding is not a very nice mapping to character data.  The only
	way that we can properly do this is to divide by 62 each time and get
	the remainder, this will ensure full use of the base62 space.  This is
	/not/ very performant.  So instead we operate the base62 encoding on
	56 bit (7 byte) chunks.  This gives a pretty good usage, with far less
	lengthy division operations on moderately sized input.  I only did this
	for completeness as I got interested in it, but we can prove that you have
	to do the full division each time (although you may find a better way of
	implementing it!) as follows.  We want to encode data as a bitstream, so
	we want to find N,M s.t. 62^M = 2^N, and M,N are integers.  There are
	no such M,N (proof on request).  For base64 encoding we get 64^M = 2^N,
	obviously we can fit M=1,N=6 which equates to sucking up 6 bits each time
	for the encoding algorithm.  So instead we try to find a comprise between
	the the block size, and the number of bits wasted by the conversion to 
	base62 space.  The constraints here are

	(1) we want to deal with whole numbers of bytes to simplify the code
	(2) we want to waste as little of the encoding space as possible
	(3) we want to keep the division operations restricted to a reasonable
	number of bits as the running time of the division operation depends
	on the length of the input bit string.

	The ratio of the length of the bit strings in the two bases will be 
	log2(256)/log2(62)
	For base64 encoding we get 4/3 exactly.  So to minimize waste here we
	want to take chunks of 3 bytes, then there is no wastage between blocks.
	For base62 encoding we get ~1.34.  Picking 5 as the block size wastes
	some 30% of the encoding space for the last byte.  Let me know if you
	think another pick is better.  This one means that we are operating
	on 40-bit strings, so the division isn't too strenuous to compute, and
	on a 64-bit platform can be done all in a register.
 */

static char base62_tab[62] = { 
	'A','B','C','D','E','F','G','H',
	'I','J','K','L','M','N','O','P',
	'Q','R','S','T','U','V','W','X',
	'Y','Z','a','b','c','d','e','f',
	'g','h','i','j','k','l','m','n',
	'o','p','q','r','s','t','u','v',
	'w','x','y','z','0','1','2','3',
	'4','5','6','7','8','9'
};

int b62_encode(char* out, const unsigned char* data, int length)
{
	int i,j;
	char *start = out;
	uint64_t bitstring;

	for (i=0;i<length-4;i+=5) {
		bitstring = 
			(uint64_t)data[i]<<32|(uint64_t)data[i+1]<<24|(uint64_t)data[i+2]<<16|
			(uint64_t)data[i+3]<<8|(uint64_t)data[i+4];

		for (j=0;j<7;++j) {
			*out++ = base62_tab[bitstring%62];
			bitstring /= 62;
		}
	}
	switch (length-i) {
	case 1:
		*out++ = base62_tab[data[i]%62];
		*out++ = base62_tab[data[i]/62];
		break;
	case 2:
		bitstring = data[i]<<8 | data[i+1];
		*out++ = base62_tab[bitstring%62];
		bitstring /= 62;
		*out++ = base62_tab[bitstring%62];
		*out++ = base62_tab[bitstring/62];
		break;
	case 3:
		bitstring = data[i]<<16 | data[i+1]<<8 | data[i+2];
		*out++ = base62_tab[bitstring%62];
		bitstring /= 62;
		*out++ = base62_tab[bitstring%62];
		bitstring /= 62;
		*out++ = base62_tab[bitstring%62];
		bitstring /= 62;
		*out++ = base62_tab[bitstring%62];
		*out++ = base62_tab[bitstring/62];
		break;
	case 4:
		bitstring = data[i]<<24|data[i+1]<<16|data[i+2]<<8|data[i+3];
		*out++ = base62_tab[bitstring%62];
		bitstring /= 62;
		*out++ = base62_tab[bitstring%62];
		bitstring /= 62;
		*out++ = base62_tab[bitstring%62];
		bitstring /= 62;
		*out++ = base62_tab[bitstring%62];
		bitstring /= 62;
		*out++ = base62_tab[bitstring%62];
		break;
	}
	return (int)(out-start);
}

#if TEST_B62
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main() {
	uint64_t big = UINT64_MAX;
	char *out = malloc(64);
	int k;
	for (k = 0; k<8 ; k++) {
		memset(out,0,64);
		int sz = b62_encode( out, (unsigned char *)&big, sizeof(big)-k);
		printf("%d %s\n",sz, out);
	}
	free(out);
	return 0;
}
#endif
