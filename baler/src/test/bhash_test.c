#include <unistd.h>
#include <stdlib.h>

#include "baler/bhash.h"

int main(int argc, char **argv)
{
	int i;
	int rc;
	struct bhash_entry *ent;
	const char *keys[] = {
		"key number one",
		"key number two",
		"key number three",
		"key number four",
		"key number five",
	};

	struct bhash *h = bhash_new(4099, 7, NULL);
	if (!h) {
		perror("bhash_new()");
		exit(-1);
	}

	for (i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
		printf("setting key: %s, value: %d\n", keys[i], i);
		ent = bhash_entry_set(h, keys[i], strlen(keys[i])+1, i);
		if (!ent) {
			printf("bhash_entry_set() error(%d): %s\n", rc, strerror(rc));
			exit(-1);
		}
	}

	printf("-----------\n");

	struct bhash_iter *iter = bhash_iter_new(h);
	if (!iter) {
		perror("bhash_iter_new()");
		exit(-1);
	}

	rc = bhash_iter_begin(iter);
	if (rc) {
		printf("bhash_iter_begin() error(%d): %s\n", rc, strerror(rc));
		exit(-1);
	}

	while ((ent = bhash_iter_entry(iter))) {
		printf("%s: %lu\n", ent->key, ent->value);
		bhash_iter_next(iter);
	}

	printf("-----------\n");

	for (i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
		ent = bhash_entry_get(h, keys[i], strlen(keys[i])+1);
		if (!ent) {
			printf("Error: %s not found!\n", keys[i]);
			exit(-1);
		}
		printf("%s:%d --> %s:%lu\n", keys[i], i, ent->key, ent->value);
	}

	printf("-----------\n");

	printf("Deleting %s\n", keys[0]);
	rc = bhash_entry_del(h, keys[0], strlen(keys[0])+1);
	if (rc) {
		printf("bhash_entry_del() error(%d): %s\n", rc, strerror(rc));
		exit(-1);
	}

	ent = bhash_entry_get(h, keys[4], strlen(keys[4])+1);
	printf("Deleting %s\n", keys[4]);
	rc = bhash_entry_remove(h, ent);
	if (rc) {
		printf("bhash_entry_remove() error(%d): %s\n", rc, strerror(rc));
		exit(-1);
	}

	rc = bhash_iter_begin(iter);
	if (rc) {
		printf("bhash_iter_begin() error(%d): %s\n", rc, strerror(rc));
		exit(-1);
	}

	while ((ent = bhash_iter_entry(iter))) {
		printf("%s: %lu\n", ent->key, ent->value);
		bhash_iter_next(iter);
	}

	printf("-----------\n");

	bhash_free(h);

	return 0;
}
