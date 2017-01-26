#ifndef __HTBL_H__
#define __HTBL_H__
#include <inttypes.h>
#include <sys/queue.h>
typedef struct hent *hent_t;

typedef	int (*htbl_cmp_fn_t)(const void *a, const void *b, size_t key_len);
typedef uint64_t (*htbl_hash_fn_t)(const void *key, size_t key_len);

struct hent {
	const void *key;
	size_t key_len;
	LIST_ENTRY(hent) hash_link;
};

typedef struct htbl {
	htbl_cmp_fn_t cmp_fn;
	htbl_hash_fn_t hash_fn;
	size_t table_depth;
	size_t entry_count;
	LIST_HEAD(hent_list_head, hent) table[0];
} *htbl_t;

htbl_t htbl_alloc(htbl_cmp_fn_t cmp_fn, size_t depth);
void hent_init(hent_t, const void *, size_t);
void htbl_ins(htbl_t t, hent_t);
hent_t htbl_find(htbl_t, const void *, size_t);
hent_t htbl_first(htbl_t t);
hent_t htbl_last(htbl_t t);
hent_t htbl_succ(hent_t e);
hent_t htbl_pred(hent_t e);

#ifndef container_of
#define container_of(ptr, type, member) ({ \
	const __typeof__(((type *)0)->member ) *__mptr = (ptr); \
	(type *)((char *)__mptr - offsetof(type,member));})
#endif
#endif
