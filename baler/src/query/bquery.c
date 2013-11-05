/**
 * \file bquery.c
 * \brief Baler storage query.
 *
 * bquery queries Baler database for:
 * 	- token
 * 	- patterns (in given node-time window)
 * 	- messages (in given node-time window)
 * 	- image pixels (in given node-time window in various granularity)
 *
 * tokens and patterns information are obtained from baler store (specified by
 * store path).
 *
 * bquery requires balerd to use bout_sos_img for image pixel query and
 * bout_sos_msg for message query. The sos files for img's and msg's are assumed
 * to be in the store path as well.
 *
 * Other storage format for various output plugins are not supported.
 *
 * bqeury can also run in daemon mode to eliminate the open-file overhead before
 * every query. A client can stay connected to bquery daemon and query
 * information repeatedly.
 *
 * XXX Issue: store should contain several images of various granularity. Users
 * can configure multiple bout_sos_img to store multiple images as they pleased.
 * However, bquery does not know about those configuration. I need to solve
 * this, either with baler_store configuration or other things. But, for now, I
 * will only support message query to test the balerd. Image query support shall
 * be revisited soon.
 *
 */

#include "bquery.h"
#include <getopt.h>
#include <errno.h>

#include <time.h>
#include <dirent.h>

/********** Global Variables **********/
/**
 * \param[in] num_list List of numbers (e.g. "1,2,5-7")
 * \param[out] _set Pointer to pointer to ::bset_u32. \c (*_set) will point to
 * 	the newly created ::bset_u32.
 * \returns 0 on success.
 * \returns Error code on error.
 */
int strnumlist2set(const char *num_lst, struct bset_u32 **_set)
{
	int rc = 0;
	char *buff = strdup(num_lst);
	if (!buff) {
		rc = errno;
		goto err0;
	}
	struct bset_u32 *set = bset_u32_alloc(MASK_HSIZE);
	if (!set) {
		rc = errno;
		goto err1;
	}
	*_set = set;
	char *tok = strtok(buff, ",");
	while (tok) {
		int a, b, i;
		a = atoi(tok);
		b = a;
		char *tok2 = strchr(tok, '-');
		if (tok2)
			b = atoi(tok2 + 1);
		for (i=a; i<=b; i++) {
			int _rc;
			_rc = bset_u32_insert(set, i);
			if (_rc && _rc != EEXIST) {
				rc = _rc;
				goto err2;
			}
		}
		tok = strtok(NULL, ",");
	}
	return 0;
err2:
	bset_u32_free(set);
err1:
	free(buff);
err0:
	return rc;
}

struct bsos_wrap* bsos_wrap_open(const char *path)
{
	struct bsos_wrap *bsw = calloc(1, sizeof(*bsw));
	if (!bsw)
		goto err0;
	bsw->sos = sos_open(path, O_RDWR);
	if (!bsw->sos)
		goto err1;
	char *bname = basename(path);
	if (!bname)
		goto err2;
	bsw->store_name = strdup(bname);
	if (!bsw->store_name)
		goto err2;
	return bsw;
err2:
	sos_close(bsw->sos);
err1:
	free(bsw);
err0:
	return NULL;
}

void bsos_wrap_close_free(struct bsos_wrap *bsw)
{
	sos_close(bsw->sos);
	free(bsw->store_name);
	free(bsw);
}

/**
 * Open baler store.
 *
 * Open baler store, which includes:
 * 	- comp_store (mapping hostname<->host_id)
 * 	- tkn_store (mapping token<->token_id)
 * 	- ptn_store (mapping pattern<->pattern_id)
 * 	- msg_store (sos for message data)
 *
 * \param path The path to the store.
 * \return 0 on success.
 * \return Error code on error.
 */
struct bq_store* bq_open_store(const char *path)
{
	struct bq_store *s = calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	char spath[PATH_MAX];
	/* comp_store */
	sprintf(spath, "%s/comp_store", path);
	s->cmp_store = btkn_store_open(spath);
	if (!s->cmp_store)
		goto err0;

	/* tkn_store */
	sprintf(spath, "%s/tkn_store", path);
	s->tkn_store = btkn_store_open(spath);
	if (!s->tkn_store)
		goto err1;

	/* ptn_store */
	sprintf(spath, "%s/ptn_store", path);
	s->ptn_store = bptn_store_open(spath);
	if (!s->ptn_store)
		goto err2;

	/* msg_store */
	sprintf(spath, "%s/msg_store/msg", path);
	s->msg_sos = sos_open(spath, O_RDWR);
	if (!s->msg_sos)
		goto err3;

	/* img_store */
	sprintf(spath, "%s/img_store", path);
	DIR *imgd = opendir(spath);
	if (!imgd)
		goto err4;

	struct dirent dent;
	struct dirent *dentp;
	struct bsos_wrap *bsw;
	char *suffix;

dent_loop:
	if (readdir_r(imgd, &dent, &dentp) != 0)
		goto err5;
	if (!dentp)
		goto out; /* no more entry */

	/* go to next entry if suffix is not "_sos.OBJ" */
	if ((suffix = strrchr(dent.d_name, '_')) == NULL)
		goto dent_loop;
	if (strcmp(suffix, "_sos.OBJ") != 0)
		goto dent_loop;
	/* main sos filename is {store_name}_sos.OBJ */
	suffix[0] = '\0'; /* cut the suffix off of dent.d_name */
	sprintf(spath + strlen(spath), "/%s", dent.d_name);
	bsw = bsos_wrap_open(spath);
	if (!bsw)
		goto err5;
	LIST_INSERT_HEAD(&s->img_sos_list, bsw, link);
	goto dent_loop;

out:
	return s;
err5:
	/* clear list image store */
	while (bsw = LIST_FIRST(&s->img_sos_list)) {
		LIST_REMOVE(bsw, link);
		bsos_wrap_close_free(bsw);
	}
err4:
	sos_close(s->msg_sos);
err3:
	bptn_store_close_free(s->ptn_store);
err2:
	btkn_store_close_free(s->tkn_store);
err1:
	btkn_store_close_free(s->cmp_store);
err0:
	berr("Cannot open %s\n", spath);
	free(s);
	return NULL;
}

int bq_local_ptn_routine(struct bq_store *s)
{
	char buff[4096];
	uint32_t id = bptn_store_first_id(s->ptn_store);
	uint32_t last_id = bptn_store_last_id(s->ptn_store);
	int rc = 0;
	while (id <= last_id) {
		rc = bptn_store_id2str(s->ptn_store, s->tkn_store, id, buff,
									4096);
		if (rc)
			return rc;
		printf("%d %s\n", id, buff);
		id++;
	}
	return rc;
}

struct bqprint {
	void *p;
	int (*print)(void* p, char *fmt, ...);
};

/**
 * Print message to \c buff.
 * \param s The store handle.
 * \param buff The output buffer.
 * \param buff_len The length of the output buffer.
 * \param msg The message (ptn_id, args).
 * \return 0 on success.
 * \return Error code on error.
 */
int bq_print_msg(struct bq_store *s,  char *buff, int buff_len,
		 struct bmsg *msg)
{
	int rc = 0;
	const struct bstr *ptn;
	struct bptn_store *ptn_store = s->ptn_store;
	struct btkn_store *tkn_store = s->tkn_store;
	ptn = bmap_get_bstr(ptn_store->map, msg->ptn_id);
	if (!ptn)
		return ENOENT;
	uint32_t *msg_arg = msg->argv;
	const uint32_t *ptn_tkn = ptn->u32str;
	int len = ptn->blen;
	int blen;
	while (len) {
		uint32_t tkn_id = *ptn_tkn++;
		if (tkn_id == BMAP_ID_STAR)
			tkn_id = *msg_arg++;
		rc = btkn_store_id2str(tkn_store, tkn_id, buff, buff_len);
		if (rc)
			goto out;
		len -= sizeof(uint32_t);
		blen = strlen(buff);
		buff += blen;
		buff_len -= blen;
	}
out:
	return rc;
}

void bquery_destroy(struct bquery *q)
{
	if (q->itr)
		sos_iter_free(q->itr);
	if (q->hst_ids)
		bset_u32_free(q->hst_ids);
	if (q->ptn_ids)
		bset_u32_free(q->ptn_ids);
	free(q);
}

void bimgquery_destroy(struct bimgquery *q)
{
	free(q->store_name);
	struct brange_u32 *r;
	while (q->hst_rngs && (r = LIST_FIRST(q->hst_rngs))) {
		LIST_REMOVE(r, link);
		free(r);
	}
	free(q->hst_rngs);
	bquery_destroy((void*)q);
}

struct bquery* bquery_create(struct bq_store *store, const char *hst_ids,
			     const char *ptn_ids, const char *ts0,
			     const char *ts1, int is_text, char sep, int *rc)
{
	int _rc = 0;
	if (!store) {
		_rc = EINVAL;
		goto out;
	}

	struct bquery *q = calloc(1, sizeof(*q));
	if (!q) {
		_rc = errno;
		goto out;
	}

	q->store = store;

	if (hst_ids) {
		_rc = strnumlist2set(hst_ids, &q->hst_ids);
		if (_rc)
			goto err;
	}

	if (ptn_ids) {
		_rc = strnumlist2set(ptn_ids, &q->ptn_ids);
		if (_rc)
			goto err;
	}

	struct tm tm;
	char *ts_ret;
	if (ts0) {
		bzero(&tm, sizeof(tm));
		ts_ret = strptime(ts0, "%F %T", &tm);
		if (ts_ret == NULL) {
			_rc = EINVAL;
			goto err;
		}
		q->ts_0 = mktime(&tm);
	}

	if (ts1) {
		bzero(&tm, sizeof(tm));
		ts_ret = strptime(ts1, "%F %T", &tm);
		if (ts_ret == NULL) {
			_rc = EINVAL;
			goto err;
		}
		q->ts_1 = mktime(&tm);
	}

	q->text_flag = is_text;
        q->sep = (sep)?(sep):(' ');
	goto out;

err:
	bquery_destroy(q);
out:
	if (rc)
		*rc = _rc;
	return q;
}

struct bimgquery* bimgquery_create(struct bq_store *store, const char *hst_ids,
				const char *ptn_ids, const char *ts0,
				const char *ts1, const char *img_store_name,
				int *rc)
{
	struct bsos_wrap *bsw = bsos_wrap_find(&store->img_sos_list,
						img_store_name);
	if (!bsw)
		return NULL; /* cannot find the given store name */

        struct bquery *bq = bquery_create(store, hst_ids, ptn_ids, ts0, ts1, 0,
                                                0, rc);
	if (!bq)
		return NULL;
	struct bimgquery *bi = calloc(1, sizeof(*bi));

	/* Transfer the contents of bq to bi, and free (not destroy) the unused
	 * bq */
	bi->base = *bq;
	free(bq);
	bi->img_sos = bsw->sos;
	bi->store_name = strdup(img_store_name);
	if (!bi->store_name)
		goto err;
	if (bi->base.hst_ids) {
		bi->hst_rngs = bset_u32_to_brange_u32(bi->base.hst_ids);
		if (!bi->hst_rngs)
			goto err;
		bi->crng = LIST_FIRST(bi->hst_rngs);
	}
	return bi;
err:
	bimgquery_destroy(bi);
	return NULL;
}

bq_stat_t bq_get_stat(struct bquery *q)
{
	return q->stat;
}

char* bq_query_generic(void* q, int *rc, int buffsiz, int(fn)(void*,char*,int))
{
	int _rc = 0;
	char *buff = malloc(buffsiz); /* 1024 characters should be enough for a
				    * message */
	if (!buff) {
		_rc = ENOMEM;
		goto out;
	}

	_rc = fn(q, buff, buffsiz);

out:
	if (_rc) {
		free(buff);
		buff = NULL;
	}
	if (rc)
		*rc = _rc;
	return buff;
}

char* bq_query(struct bquery *q, int *rc)
{
	return bq_query_generic(q, rc, 1024, (void*)bq_query_r);
}

char* bq_imgquery(struct bimgquery *q, int *rc)
{
	return bq_query_generic(q, rc, 64, (void*)bq_imgquery_r);
}

int bq_query_r(struct bquery *q, char *buff, size_t bufsz)
{
	int rc = 0;
	sos_t msg_sos = q->store->msg_sos;
	sos_attr_t attr = sos_obj_attr_by_id(msg_sos, SOS_MSG_SEC);

	buff[0] = 0;

	if (!q->itr) {
		/* First call */
		q->itr = sos_iter_new(msg_sos, SOS_MSG_SEC);
		if (!q->itr) {
			rc = errno;
			goto out;
		}
		if (q->ts_0) {
			struct sos_key_s key;
			sos_attr_key_set(attr, &q->ts_0, &key);
			uint64_t obj = sos_iter_seek_inf(q->itr, &key);
			if (!obj) {
				/* Don't worry, there's just no infimum */
				sos_iter_seek_start(q->itr);
			}
		}
		q->obj = sos_iter_next(q->itr);
	}

	if (!q->obj) {
		rc = ENOENT;
		goto out;
	}

	uint32_t comp_id;
	uint32_t sec;
	uint32_t usec;
	struct bmsg *msg;
	int len;
loop:
	SOS_OBJ_ATTR_GET(sec, msg_sos, SOS_MSG_SEC, q->obj);
	/* need to check ts_0 again because of seek_inf is not an exact seek */
	if (q->ts_0 && sec < q->ts_0) {
		goto next;
	}
	if (q->ts_1 && sec > q->ts_1) {
		/* Out of time window, no need to continue */
		rc = ENOENT;
		goto out;
	}
	SOS_OBJ_ATTR_GET(usec, msg_sos, SOS_MSG_USEC, q->obj);
	SOS_OBJ_ATTR_GET(comp_id, msg_sos, SOS_MSG_COMP_ID, q->obj);
	if (q->hst_ids && !bset_u32_exist(q->hst_ids, comp_id))
		goto next;
	msg = sos_obj_attr_get(msg_sos, SOS_MSG_MSG, q->obj);
	if (q->ptn_ids && !bset_u32_exist(q->ptn_ids, msg->ptn_id))
		goto next;
	if (q->text_flag) {
		struct tm tm;
		time_t t = sec;
		localtime_r(&t, &tm);
		strftime(buff, 64, "%Y-%m-%d %T", &tm);
		len = strlen(buff);
		const struct bstr *bstr = bmap_get_bstr(
						q->store->cmp_store->map,
						comp_id + BMAP_ID_BEGIN - 1);
		if (bstr)
			len += sprintf(buff+len, ".%06d%c%.*s%c", usec, q->sep,
                                        bstr->blen, bstr->cstr, q->sep);
		else
                        len += sprintf(buff+len, ".%06d%cNULL%c", usec, q->sep,
                                        q->sep);
	} else {
                len = sprintf(buff, "%u.%06u%c%u%c", sec, usec, q->sep, comp_id,
                                q->sep);
	}
	rc = bq_print_msg(q->store, buff+len, bufsz-len, msg);
	goto done;
next:
	q->obj = sos_iter_next(q->itr);
	if (!q->obj) {
		rc = ENOENT;
		goto out;
	}
	goto loop;

done:
	/* When done, point obj to the next message */
	q->obj = sos_iter_next(q->itr);
out:
	return rc;
}

int bq_imgquery_r(struct bimgquery *q, char *buff, size_t bufsz)
{
	int rc = 0;
	sos_t img_sos = q->img_sos;
	struct bquery *_q = (void*)&q->base;
	/* For an image, the attr 0  is {time, node} */
	sos_attr_t attr = sos_obj_attr_by_id(img_sos, 0);

	buff[0] = 0;

	if (!_q->itr) {
		/* First call */
		_q->itr = sos_iter_new(img_sos, 0);
		if (!_q->itr) {
			rc = errno;
			goto out;
		}
		struct bout_sos_img_key k;
		k.ts = _q->ts_0;
		if (q->hst_rngs)
			k.comp_id = LIST_FIRST(q->hst_rngs)->a;
		else
			k.comp_id = 0;
		struct sos_key_s key;
		sos_attr_key_set(attr, &k, &key);
		uint64_t obj = sos_iter_seek_inf(_q->itr, &key);
		if (!obj) {
			/* Don't worry, there's just no infimum
			 * to the key. */
			sos_iter_seek_start(_q->itr);
		}
		_q->obj = sos_iter_next(_q->itr);
	}

	if (!_q->obj) {
		rc = ENOENT;
		goto out;
	}

	struct bout_sos_img_key k;
	uint32_t comp_id;
	uint32_t sec;
	uint32_t usec;
	uint32_t ptn_id;
	uint32_t count;
	struct bmsg *msg;
	int len;
loop:
	SOS_OBJ_ATTR_GET(k, img_sos, 0, _q->obj);
	sec = k.ts;
	comp_id = k.comp_id;
	/* need to check ts_0 again because of seek_inf is not an exact seek */
	if (_q->ts_0 && sec < _q->ts_0) {
		goto next;
	}
	if (_q->ts_1 && sec > _q->ts_1) {
		/* Out of time window, no need to continue */
		rc = ENOENT;
		goto out;
	}
	if (_q->hst_ids && !bset_u32_exist(_q->hst_ids, comp_id))
		goto next;
	SOS_OBJ_ATTR_GET(ptn_id, img_sos, 1, _q->obj);
	if (_q->ptn_ids && !bset_u32_exist(_q->ptn_ids, ptn_id))
		goto next;
	SOS_OBJ_ATTR_GET(count, img_sos, 2, _q->obj);
	len = sprintf(buff, "%u %u %u %u", sec, comp_id, ptn_id, count);
	goto done;
next:
	_q->obj = sos_iter_next(_q->itr);
	if (!_q->obj)
		goto out;
	goto loop;

done:
	/* When done, point obj to the next message */
	_q->obj = sos_iter_next(_q->itr);
out:
	return rc;
}

#define STR_SZ 65536
char* bq_get_ptns(struct bq_store *s)
{
	uint32_t id = bptn_store_first_id(s->ptn_store);
	uint32_t last_id = bptn_store_last_id(s->ptn_store);
	int rc = 0;
	char *buf = malloc(STR_SZ);
	char *str = buf;
	char *newbuf;
	size_t bufsz = STR_SZ; /* Tracking the total allocated size */
	size_t str_len = STR_SZ; /* The left over bytes to write */
	int len;
loop:
	if (id > last_id)
		goto out;
	rc = bptn_store_id2str(s->ptn_store, s->tkn_store, id, str, str_len);
	switch (rc) {
		case 0:
			len = strlen(str);
			if (len + 2 <= str_len)
				break;
			/* else, treat as ENOMEM */
		case ENOMEM:
			/* Expand buf */
			bufsz += STR_SZ;
			newbuf = realloc(buf, bufsz);
			if (!newbuf)
				goto err;
			str = newbuf + (str - buf);
			str_len = bufsz - (str - newbuf);
			buf = newbuf;
			goto loop;
			break;
		default:
			goto err;
	}
	/* This is valid because of case 0 */
	str[len] = '\n';
	str[len+1] = '\0';
	str_len -= len+1;
	str += len+1;
	id++;
	goto loop;
out:
	str[str_len-1] = '\0';
	return buf;

err:
	free(buf);
	return NULL;
}

int bq_get_ptns_r(struct bq_store *s, char *buf, size_t buflen)
{
	uint32_t id = bptn_store_first_id(s->ptn_store);
	uint32_t last_id = bptn_store_last_id(s->ptn_store);
	int rc = 0;
	while (id <= last_id && buflen > 1) {
		rc = bptn_store_id2str(s->ptn_store, s->tkn_store, id, buf,
									buflen);
		if (rc)
			return rc;
		int len = strlen(buf);
		buflen -= len;
		buf += len;
		if (buflen > 1) {
			buflen--;
			*buf++ = '\n';
		}
		id++;
	}
	buf[buflen-1] = '\0';
	return rc;
}

char* bq_get_ptn_tkns(struct bq_store *store, int ptn_id, int arg_idx)
{
	struct btkn_store *tkn_store = store->tkn_store;
	struct bptn_store *ptn_store = store->ptn_store;
	uint64_t attr_off = ptn_store->attr_idx->bvec->data[ptn_id];
	struct bptn_attrM *attrM = BMPTR(ptn_store->mattr, attr_off);
	if (!attrM || arg_idx > attrM->argc) {
		errno = EINVAL;
		return NULL;
	}
	uint64_t arg_off = attrM->arg_off[arg_idx];
	struct bmlnode_u32 *node;
	struct bdstr *bdstr = bdstr_new(65536);
	char buf[4096+2]; /* 4096 should be more than enough for a token, and
			   * the +2 is for \n and \0 */
	int rc = 0;
	int len;
	BMLIST_FOREACH(node, arg_off, link, ptn_store->marg) {
		/* node->data is token ID */
		rc = btkn_store_id2str(tkn_store, node->data, buf, 4096);
		if (rc)
			goto err;
		len = strlen(buf);
		buf[len] = '\n';
		buf[len+1] = '\0';
		rc = bdstr_append(bdstr, buf);
		if (rc)
			goto err;
	}
	/* Keep only the string in bdstr, and throw away the wrapper */
	char *str = bdstr->str;
	free(bdstr);
	return str;
err:
	free(bdstr->str);
	free(bdstr);
	errno = rc;
	return NULL;
}

#ifdef BIN

/* query type definitions */
#define BQ_TYPE__LIST(WRAP) \
	WRAP(UNKNOWN) \
	WRAP(MSG) \
	WRAP(PTN)

#define BQ_TYPE_WRAP_ENUM(X) BQ_TYPE_##X,
#define BQ_TYPE_WRAP_STR(X) #X,

enum BQ_TYPE {
	BQ_TYPE__LIST(BQ_TYPE_WRAP_ENUM)
	BQ_TYPE_LAST
};

const char *BQ_TYPE_STR[] = {
	BQ_TYPE__LIST(BQ_TYPE_WRAP_STR)
};

const char* bq_type_str(enum BQ_TYPE type)
{
	if (type <= 0 || type >= BQ_TYPE_LAST)
		return BQ_TYPE_STR[BQ_TYPE_UNKNOWN];
	return BQ_TYPE_STR[type];
}

enum BQ_TYPE bq_type(const char *str)
{
	int i;
	for (i=0; i<BQ_TYPE_LAST; i++)
		if (strcasecmp(BQ_TYPE_STR[i], str)==0)
			return i;
	return BQ_TYPE_UNKNOWN;
}

enum BQ_TYPE query_type = BQ_TYPE_MSG;
char *store_path = NULL;
int daemon_flag = 0;
char *remote_host = NULL;
enum {
	XPRT_NONE = 0,
	XPRT_SOCKET,
	XPRT_RDMA
} xprt_type = XPRT_NONE;
uint32_t port = 0;

char *hst_ids = NULL;
char *ptn_ids = NULL;
char *ts_begin = NULL;
char *ts_end = NULL;

struct btkn_store *tkn_store = NULL;
struct btkn_store *comp_store = NULL;
struct bptn_store *ptn_store = NULL;
sos_t msg_sos = NULL;

enum {
	BQ_MODE_INVAL = -1, /* invalid mode */
	BQ_MODE_LOCAL,   /* locally query once */
	BQ_MODE_DAEMON,  /* daemon mode */
	BQ_MODE_REMOTE,  /* remotely query once */
} running_mode = BQ_MODE_LOCAL;

void show_help()
{
	printf(
"Usages\n"
"    (single query): Read data from the baler store. Data are filtered by\n"
"	specified query options.\n"
"\n"
"	bquery --store-path <path> [QUERY_OPTIONS]\n"
"\n"
"\n"
"    (daemon mode): Run bquery in daemon mode. Providing data from the store\n"
"	to other bquery over the network.\n"
"\n"
"	bquery --daemon --store-path <path> [XPRT_OPTIONS]\n"
"\n"
"\n"
"    (single query through daemon): Like single query, but get the data from\n"
"	the bquery daemon instead.\n"
"\n"
"	bquery --remote-host <host> [XPRT_OPTIONS] [QUERY_OPTIONS]\n"
"\n"
"\n"
"XPRT_OPTIONS:\n"
"    --xprt,-x (sock|rdma)	Network transport type.\n"
"    --port,-p NUMBER		Port number to listen to (daemon mode) or \n"
"				connect to (query through daemon).\n"
"\n"
"QUERY_OPTIONS:\n"
"    --type,-t TYPE             The TYPE of the query, can be MSG or PTN.\n"
"    --host-mask,-H NUMBER,...	The comma-separated list of numbers of\n"
"				required hosts. The NUMBER can be in X-Y\n"
"				format. (example: -H 1-10,20,30-50)\n"
"				If --host-mask is not specified, all hosts\n"
"				are included in the query.\n"
"    --begin,-B T1		T1 is the beginning of the time window.\n"
"    --end,-E T2		T2 is the ending of the time window.\n"
"				If T1 is empty, bquery will obtain all data\n"
"				up until T2. Likewise, if T2 is empty, bquery\n"
"				obtains all data from T1 onward. Example:\n"
"				-B \"2012-01-01 00:00:00\" \n"
"				-E \"2012-12-31 23:59:59\" \n"
"				If --begin and --end are not specified,\n"
"				there is no time window condition.\n"
"    --ptn_id-mask,-P NUMBER,...\n"
"				The number format is similar to --host-mask\n"
"				option. The list of numbers specify\n"
"				Pattern IDs to be queried. If ptn_id-mask is\n"
"				is not specified, all patterns are included.\n"
"\n"
"Other OPTIONS:\n"
"    --store-path,s PATH	The path to the baler store. Using this\n"
"				option without --daemon implies single query\n"
"				mode.\n"
"    --daemon,d			The daemon mode flag. Require --store-path\n"
"				option.\n"
"    --remote-host,-r HOST	The hostname or IP address that another\n"
"				bquery resides. Using this option implies\n"
"				single query through other daemon mode.\n"
"				--xprt and --port can be used with this\n"
"				option to specify transport type and port\n"
"				number of the remote host.\n"
"\n"
	      );
}

/********** Options **********/
char *short_opt = "hs:dr:x:p:t:H:B:E:P:";
struct option long_opt[] = {
	{"help",         no_argument,        0,  'h'},
	{"store-path",   required_argument,  0,  's'},
	{"daemon",       no_argument,        0,  'd'},
	{"remote-host",  required_argument,  0,  'r'},
	{"xprt",         required_argument,  0,  'x'},
	{"port",         required_argument,  0,  'p'},
	{"type",         required_argument,  0,  't'},
	{"host-mask",    required_argument,  0,  'H'},
	{"begin",        required_argument,  0,  'B'},
	{"end",          required_argument,  0,  'E'},
	{"ptn_id-mask",  required_argument,  0,  'P'},
#if 0
	/* This part of code shall be enabled when image query support is
	 * available */
	{"image",        required_argument,  0,  'I'},
	{"message",      required_argument,  0,  'M'},
#endif
	{0,              0,                  0,  0}
};

/**
 * Determine the running mode from the program options.
 * \param mode[out] The output variable for running mode.
 * \return One of the BQ_MODE_LOCAL, BQ_MODE_DAEMON and BQ_MODE_REMOTE on
 * 	success.
 * \return BQ_MODE_INVAL on error.
 */
int bq_get_mode()
{
	if (store_path) {
		if (daemon_flag)
			return BQ_MODE_DAEMON;
		return BQ_MODE_LOCAL;
	}
	if (remote_host) {
		return BQ_MODE_REMOTE;
	}

	return BQ_MODE_INVAL;
}

void process_args(int argc, char **argv)
{
	char c;
	int __idx=0;
	int rc;

next_arg:
	c = getopt_long(argc, argv, short_opt, long_opt, &__idx);
	switch (c) {
	case -1:
		goto out;
		break;
	case 'h': /* help */
		show_help();
		exit(0);
		break;
	case 's': /* store-path */
		store_path = strdup(optarg);
		break;
	case 'd': /* daemon */
		daemon_flag = 1;
		break;
	case 'r': /* remote-host */
		remote_host = strdup(optarg);
		break;
	case 't': /* type */
		query_type = bq_type(optarg);
		if (query_type == BQ_TYPE_UNKNOWN) {
			berr("Unknown --type %s\n", optarg);
			exit(-1);
		}
		break;
	case 'x': /* xprt */
		if (strcmp(optarg, "sock") == 0) {
			xprt_type = XPRT_SOCKET;
		} else if (strcmp(optarg, "rdma") == 0) {
			xprt_type = XPRT_RDMA;
		} else {
			printf("Unknown xprt: %s\n", argv[optind - 1]);
			exit(-1);
		}
		break;
	case 'p': /* port */
		port = atoi(optarg);
		break;
	case 'H': /* host-mask */
		hst_ids = strdup(optarg);
		if (!hst_ids) {
			perror("hst_ids: strdup");
			exit(-1);
		}
		break;
	case 'B': /* begin (time) */
		ts_begin = strdup(optarg);
		if (!ts_begin) {
			perror("ts_begin: strdup");
			exit(-1);
		}
		break;
	case 'E': /* end (time) */
		ts_end = strdup(optarg);
		if (!ts_end) {
			perror("ts_end: strdup");
			exit(-1);
		}
		break;
	case 'P': /* ptn_id-mask */
		ptn_ids = strdup(optarg);
		if (!ptn_ids) {
			perror("ptn_ids: strdup");
			exit(-1);
		}
		break;
	default:
		fprintf(stderr, "Unknown argument %s\n", argv[optind - 1]);
	}
	goto next_arg;
out:
	return;
}

int bq_local_msg_routine(struct bq_store *s)
{
	int rc = 0;
	struct bquery *q = bquery_create(s, hst_ids, ptn_ids, ts_begin, ts_end,
					 1, 0, &rc);
	const static int N = 4096;
	char buff[N];
loop:
	rc = bq_query_r(q, buff, N);
	if (rc)
		goto out;
	printf("%s\n", buff);
	goto loop;
out:
	if (rc == ENOENT)
		rc = 0;
	return rc;
}

int bq_local_routine()
{
	int rc = 0;
	struct bq_store *s;
	if ((s = bq_open_store(store_path)) == NULL) {
		berr("bq_open_store error %d: %s\n", rc, strerror(rc));
		goto out;
	}

	msg_sos = s->msg_sos;
	comp_store = s->cmp_store;
	tkn_store = s->tkn_store;
	ptn_store = s->ptn_store;

	switch (query_type) {
	case BQ_TYPE_MSG:
		rc = bq_local_msg_routine(s);
		break;
	case BQ_TYPE_PTN:
		rc = bq_local_ptn_routine(s);
	default:
		rc = EINVAL;
	}
out:
	return rc;
}

int bq_daemon_routine()
{
	/* Consider using Ruby or other scripting language to handle this. */
	berr("bq_daemon_routine unimplemented!!!\n");
	return 0;
}

int bq_remote_routine()
{
	/* Deprecated design ... left here just for a chance of resurviving. */
	berr("bq_remote_routine unimplemented!!!\n");
	return 0;
}

int main(int argc, char **argv)
{
	process_args(argc, argv);
	running_mode = bq_get_mode();
	switch (running_mode) {
	case BQ_MODE_LOCAL:
		bq_local_routine();
		break;
	case BQ_MODE_DAEMON:
		bq_daemon_routine();
		break;
	case BQ_MODE_REMOTE:
		bq_remote_routine();
		break;
	default:
		printf("Cannot determine the running mode from the "
				"arguments.\n");
	}
	return 0;
}
#endif /*BIN*/

