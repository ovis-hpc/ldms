#ifndef __HTBL_H__
#define __HTBL_H__
#include <inttypes.h>
#include <sys/queue.h>
typedef struct hent *hent_t;

typedef	int (*htbl_cmp_fn_t)(const void *a, const void *b, size_t key_len);
typedef uint64_t (*htbl_hash_fn_t)(const void *key, size_t key_len);

typedef struct htbl *htbl_t;
struct hent {
	const void *key;
	size_t key_len;
	htbl_t tbl;
	LIST_ENTRY(hent) hash_link;
};

struct htbl {
	htbl_cmp_fn_t cmp_fn;
	htbl_hash_fn_t hash_fn;
	size_t table_depth;
	size_t entry_count;
	LIST_HEAD(hent_list_head, hent) table[0];
};

htbl_t htbl_alloc(htbl_cmp_fn_t cmp_fn, size_t depth);
void htbl_free(htbl_t t);
void hent_init(hent_t, const void *, size_t);
void htbl_ins(htbl_t t, hent_t);
void htbl_del(htbl_t t, hent_t e);
int htbl_empty(htbl_t t);
hent_t htbl_find(htbl_t, const void *, size_t);
hent_t htbl_first(htbl_t t);
hent_t htbl_next(hent_t e);

#ifndef offsetof
/* C standard since c89 */
#define offsetof(type, member) ((size_t) &((type *)0)->member)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({ \
	const __typeof__(((type *)0)->member ) *__mptr = (ptr); \
	(type *)((char *)__mptr - offsetof(type,member));})
#endif
#endif
