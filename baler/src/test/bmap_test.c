#include <unistd.h>
#include <stdlib.h>

#include "baler/bcommon.h"
#include "baler/btypes.h"
#include "baler/butils.h"
#include "baler/bmapper.h"

uint32_t test_insert(struct bmap *bmap, char *cstr)
{
	char buff[4096];
	struct bstr *str = (void*)buff;
	int id;
	bmap_ins_ret_t ret;

	bstr_set_cstr(str, cstr, 0);
	id = bmap_insert(bmap, str, &ret);
	if (id == BMAP_ID_ERR) {
		berr("bmap_insert error: %s", bmap_ins_ret_str[ret]);
		exit(-1);
	}
	return id;
}

void test_get_id(struct bmap *bmap, char *cstr, int cmp_id)
{
	char buff[4096];
	struct bstr *str = (void*) buff;
	str->blen = strlen(cstr);
	memcpy(str->cstr, cstr, str->blen);
	uint32_t id = bmap_get_id(bmap, str);
	if (id != cmp_id) {
		berr("Expecting %u, but got %u from key: %s", cmp_id, id, cstr);
		exit(-1);
	}

	binfo("id: %u, key: %s: OK", id, cstr);
}

void test_get_bstr(struct bmap *bmap, int id, char *cmp_cstr)
{
	const struct bstr *str;
	int len = strlen(cmp_cstr);
	str = bmap_get_bstr(bmap, id);
	if (!str) {
		berr("Cannot get bstr of ID: %u", id);
		exit(-1);
	}
	if (str->blen != len || strncmp(cmp_cstr, str->cstr, len) != 0) {
		berr("cmp_cstr (%s) != (%.*s) bstr", cmp_cstr, len, str->cstr);
		exit(-1);
	}
	binfo("str: %s, id: %u, OK", cmp_cstr, id);
}

/**
 * bmapper test program.
 */
int main(int argc, char **argv)
{
	char *path = "/tmp/bmap_test";
	struct bmap *bmap = bmap_open(path);
	if (!bmap) {
		berr("Cannot open bmap: %s, error: %m", path);
		exit(-1);
	}
	uint32_t id1,id2,id3;
	id1 = test_insert(bmap, "Hello");
	id2 = test_insert(bmap, "World");
	id3 = test_insert(bmap, ":)");

	/* close && reopen */
	bmap_close_free(bmap);
	bmap = bmap_open(path);

	if (!bmap) {
		berr("Cannot open bmap: %s", path);
		exit(-1);
	}

	test_get_id(bmap, "Hello", id1);
	test_get_id(bmap, "World", id2);
	test_get_id(bmap, ":)", id3);

	test_get_bstr(bmap, id1, "Hello");
	test_get_bstr(bmap, id2, "World");
	test_get_bstr(bmap, id3, ":)");
	bmap_close_free(bmap);

	/* clean up */
	char tmp[1024];
	sprintf(tmp, "rm -rf %s", path);
	system(tmp);

	return 0;
}
