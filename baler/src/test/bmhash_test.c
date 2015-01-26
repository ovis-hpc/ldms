#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "baler/bmhash.h"

#define TEST_PATH "./test.bmh"

int main(int argc, char **argv)
{
	struct bmhash *bmh;
	struct bstr *bstr;
	struct bmhash_entry *ent;
	struct bmhash_iter *iter;
	int rc;
	const char *keys[] = {
		"key number one",
		"key number two",
		"key number three",
		"key number four",
		"key number five",
	};
	struct bstr *bkeys[sizeof(keys)/sizeof(*keys)];
	int i;

	bmh = bmhash_open(TEST_PATH, O_RDWR | O_CREAT,
			4099, BMHASH_FN_MURMUR, 7);
	if (!bmh) {
		printf("bmhash_open() error(%d): %m\n", errno);
		exit(-1);
	}

	for (i = 0; i < sizeof(keys)/sizeof(*keys); i++) {
		bkeys[i] = bstr_alloc_init_cstr(keys[i]);

		printf("setting key: %s, value: %d\n", keys[i], i);
		ent = bmhash_entry_set(bmh, bkeys[i], i);
		if (!ent) {
			printf("bhash_entry_set() error(%d): %m\n", errno);
			exit(-1);
		}
	}

	printf("-----------\n");

	iter = bmhash_iter_new(bmh);
	if (!iter) {
		perror("bmhash_iter_new()");
		exit(-1);
	}

	rc = bmhash_iter_begin(iter);
	if (rc) {
		printf("bmhash_iter_begin() error(%d): %s\n", rc, strerror(rc));
		exit(-1);
	}

	printf("Iterating over the collection:\n");
	while ((ent = bmhash_iter_entry(iter))) {
		printf("%s: %lu\n", iter->ent->key.cstr, iter->ent->value);
		bmhash_iter_next(iter);
	}

	printf("-----------\n");

	printf("Search test ...\n");
	for (i = 0; i < sizeof(bkeys)/sizeof(*bkeys); i++) {
		ent = bmhash_entry_get(bmh, bkeys[i]);
		if (!ent) {
			printf("bmhash_entry_get(%s) failed, errno: (%d) %m\n",
					bkeys[i]->cstr, errno);
			exit(-1);
		}
		printf("%s:%d --> %s:%lu\n", bkeys[i]->cstr, i, ent->key.cstr,
								ent->value);
	}

	printf("-----------\n");

	bmhash_close_free(bmh);

	rc = bmhash_unlink(TEST_PATH);
	if (rc) {
		printf("bmhash_unlink() error(%d): %s\n", rc, strerror(rc));
		exit(-1);
	}

	return 0;
}
