#include "ovis-map.h"
//#include "third/city.h"
#include <inttypes.h>
#include <string.h>
#include <errno.h>
//#include <pthread.h>
//#include <assert.h>
// #include "rbt.h"


/** test at the top of file to ensure the tests don't cheat
with private implementation details.
returns 0 if ok or line number of badness if not.
*/
#include <stdio.h>

static struct data {
	int num;
} d[4] = { {1} , {2}, {3}, {4} };
#define DPTR(x) ((struct data *)(x))

static void ve(struct ovis_map_element *e, void *user)
{
	printf("visit: e = { %s , %"PRIu64 ", %p, %d\n",
	       e->key, e->keyhash, e->value,
	       DPTR(e->value)->num);
}

static void ekill(struct ovis_map_element *e, void *user)
{
	(void)user;
	if (e) {
		free((char *)(e->key));
		free(e->value);
		e->key = e->value = NULL;
	}
}

int main(int argc, char ** argv)
{
	struct ovis_map *m = ovis_map_create();
	if (!m) {
		printf("map create fail.\n");
		return __LINE__;
	}
	const char *fred = "fred";
	const char *b[] = {"bob1", "bob2", "bob3", "bob4"};

	struct ovis_map_element me = ovis_map_find(m, fred);
	if (me.value != NULL) {
		printf("bad search succeeded.\n");
		return __LINE__;
	}
#define FREDHASH 5733294712249453063L
	if (me.keyhash != FREDHASH) {
		printf("bad hash fred: %"PRIu64 "\n",me.keyhash);
		return 1;
	}
	if (me.key != fred) {
		printf("bad search key result.\n");
		return __LINE__;
	}
	me.value = & (d[0]);
	int e0 = ovis_map_insert_fast(m, me);
	if (e0) {
		printf("bad insert of d[0]: %d %s\n",e0,strerror(e0));
		return __LINE__;
	}
	int edup = ovis_map_insert(m, fred, &d[1]);
	if (edup != ENOTUNIQ) {
		printf("bad duplicate insert of d[0]: %d %s\n",edup,
		       strerror(edup));
		return __LINE__;
	}
	struct ovis_map_element me2 = ovis_map_find(m, fred);
	if (me2.value != (void *)&d[0]) {
		printf("failed to retrieve d[0]: got %p and %d\n", me2.value,
		       (me2.value ? DPTR(me2.value)->num : -1) );
		return __LINE__;
	}
	int i = 0;
	for (; i<=3; i++) {
		if (ovis_map_insert(m, b[i], &d[i]) ) {
			printf("failed to insert d[%d] for key %s\n", i, b[i]);
			return __LINE__;
		}
	}
	size_t ms = ovis_map_size(m);
	struct ovis_map_element * x[2];
	int64_t newsize = ovis_map_snapshot(m, x, 2);
	if (!newsize) {
		printf("failed to get bigger snapshot size\n");
		return __LINE__;
	}
	if (newsize != ms+1) {
		printf("map size mismatch: %"PRId64", %zd\n", newsize, ms);
		return __LINE__;
	}

	struct ovis_map_element * y[newsize];
	newsize = ovis_map_snapshot(m, y, newsize);
	if (newsize) {
		printf("failed to copy map content.\n");
		return __LINE__;
	}
	for (i = 0; i < ms; i++) {
		printf("ome[%d] = { %s , %"PRIu64 ", %p, %d\n",
		       i, y[i]->key, y[i]->keyhash, y[i]->value,
		       DPTR(y[i]->value)->num);
	}

	ovis_map_visit(m, ve, NULL);
	ovis_map_destroy(m,NULL,NULL);
	ms = ovis_map_size(m); /* expect read-free and bad magic here */
	if (ms) {
		printf("size worked on dead map: %zd.\n",ms);
		return __LINE__;
	}

	m = ovis_map_create();
	if (!m) {
		printf("map create fail.\n");
		return __LINE__;
	}
	i = 0;
	for (; i <= 512; i++) {
		struct data * dp = malloc(sizeof(struct data));
		char *key = malloc(20);
		if (!key || !dp) {
			printf("malloc failed\n");
			return __LINE__;
		}
		dp->num = i+11;
		snprintf(key,20,"keynumber%d",i);
		int rc;
		if ((rc = ovis_map_insert(m, key, dp) )) {
			printf("failed to insert dp %d for key %s. %d %s\n",
			       i, key, rc, strerror(rc));
			return __LINE__;
		}
	}
	ovis_map_visit(m, ve, NULL);
	ovis_map_destroy(m, ekill, NULL);
	return 0;
}
