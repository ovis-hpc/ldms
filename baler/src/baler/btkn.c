#include "btkn.h"

const char *btkn_type_str[] = {
	BTKN_TYPE__LIST(, BENUM_STR)
};

btkn_type_t btkn_type(const char *str)
{
	return bget_str_idx(btkn_type_str, BTKN_TYPE_LAST, str);
}

struct btkn_store* btkn_store_open(char *path)
{
	char tmp[PATH_MAX];
	int __errno = 0; /* global errno might get reset in error handling path
			    */
	if (!bfile_exists(path)) {
		if (bmkdir_p(path, 0755) == -1)
			goto err0;
	}
	if (!bis_dir(path)) {
		__errno = EINVAL;
		goto err0;
	}
	struct btkn_store *ts = (typeof(ts)) malloc(sizeof(*ts));
	if (!ts) {
		__errno = errno;
		goto err0;
	}
	ts->path = strdup(path);
	if (!ts->path) {
		__errno = errno;
		goto err1;
	}
	sprintf(tmp, "%s/tkn_attr.bmvec", path);
	ts->attr = bmvec_generic_open(tmp);
	if (!ts->attr) {
		__errno = errno;
		goto err2;
	}
	sprintf(tmp, "%s/tkn.map", path);
	ts->map = bmap_open(tmp);
	if (!ts->map) {
		__errno = errno;
		goto err3;
	}
	return ts;
err3:
	bmvec_generic_close_free(ts->attr);
err2:
	free(ts->path);
err1:
	free(ts);
err0:
	errno = __errno;
	return NULL;
}

void btkn_store_close_free(struct btkn_store *s)
{
	bmvec_generic_close_free(s->attr);
	bmap_close_free(s->map);
}

int btkn_store_id2str(struct btkn_store *store, uint32_t id,
		      char *dest, int len)
{
	dest[0] = '\0'; /* first set dest to empty string */
	const struct bstr *bstr = bmap_get_bstr(store->map, id);
	if (!bstr)
		return ENOENT;
	if (len <= bstr->blen)
		return ENOMEM;
	strncpy(dest, bstr->cstr, bstr->blen);
	dest[bstr->blen] = '\0'; /* null-terminate the string */
	return 0;
}

uint32_t btkn_store_insert_cstr(struct btkn_store *store, const char *str,
							btkn_type_t type)
{
	int blen = strlen(str);
	if (!blen) {
		errno = EINVAL;
		return BMAP_ID_ERR;
	}
	struct bstr *bstr = malloc(sizeof(*bstr) + blen);
	bstr->blen = blen;
	memcpy(bstr->cstr, str, blen);
	uint32_t id = btkn_store_insert(store, bstr);
	if (id == BMAP_ID_ERR)
		return id;
	struct btkn_attr attr;
	attr.type = type;
	btkn_store_set_attr(store, id, attr);
	return id;
}

int btkn_store_char_insert(struct btkn_store *store, const char *cstr,
							btkn_type_t type)
{
	uint32_t buf[4];
	struct bstr *bs = (void*)buf;
	uint64_t tkn_id;
	bs->blen = 1;
	const char *c = cstr;
	struct btkn_attr attr;
	attr.type = type;
	while (*c) {
		bs->cstr[0] = *c;
		tkn_id = btkn_store_insert(store, bs);
		if (tkn_id == BMAP_ID_ERR)
			return errno;
		btkn_store_set_attr(store, tkn_id, attr);
		c++;
	}
}

