#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "baler/btypes.h"
#include "baler/butils.h"

char buff[4096];

void test(struct bstr *x, struct bstr *y)
{
	printf("lev_dist(%.*s, %.*s) = %d\n", x->blen, x->cstr, y->blen, y->cstr,
			bstr_lev_dist_u32(x, y, (void*)buff, sizeof(buff)));
	printf("lcs_dist(%.*s, %.*s) = %d\n", x->blen, x->cstr, y->blen, y->cstr,
			bstr_lcs_dist_u32(x, y, (void*)buff, sizeof(buff)));
	printf("lcs_len(%.*s, %.*s) = %d\n", x->blen, x->cstr, y->blen, y->cstr,
			bstr_lcs_u32(x, y, (void*)buff, sizeof(buff)));
}

int main(int argc, char **argv)
{
	struct bstr *a = bstr_alloc_init_cstr("NNNNaaaarrrraaaatttteeee");
	struct bstr *b = bstr_alloc_init_cstr("TTTTaaaaeeeerrrraaaatttt");
	struct bstr *c = bstr_alloc_init_cstr("hhhheeeelllloooo");
	struct bstr *d = bstr_alloc_init_cstr("aaaa");

	test(a, b);
	test(a, c);
	test(a, d);
	test(c, d);
	test(a, a);

	return 0;
}
