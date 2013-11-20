%module bquery
%{
#include "query/bquery.h"
%}

struct bq_store* bq_open_store(const char *path);

typedef enum bquery_status {
	BQ_STAT_INIT,
	BQ_STAT_QUERYING,
	BQ_STAT_DONE,
} bq_stat_t;

/**
 * Create query handle with given query conditions.
 *
 * \param store The store handle.
 * \param hst_ids The comma-separated list of host IDs. The range [##-##] in the
 * 	comma-separated list is also supported.
 * \param ptn_ids The comma-separated list of pattern IDs. The range is also
 * 	accepted.
 * \param ts0 The begin time stamp (format: YYYY-MM-DD hh:mm:ss)
 * \param ts1 The begin time stamp (format: YYYY-MM-DD hh:mm:ss)
 * \param is_text Non-zero if the query does not want just numbers in timestamp
 *		  and host fields.
 * \param sep Field separator (default ' ').
 * \param[out] rc The return code.
 * \return NULL on error.
 * \return newly created query handle on success.
 */
struct bquery* bquery_create(struct bq_store *store, const char *hst_ids,
			     const char *ptn_ids, const char *ts0,
			     const char *ts1, int is_text, char sep, int *rc);

struct bimgquery* bimgquery_create(struct bq_store *store, const char *hst_ids,
				const char *ptn_ids, const char *ts0,
				const char *ts1, const char *img_store_name,
				int *rc);

void bquery_destroy(struct bquery *q);

bq_stat_t bq_get_stat(struct bquery *q);

/**
 * Perform the query and return a single result.
 *
 * The result will be in the following format: DATE-TIME HOST MESSAGE.
 * If \c is_text flag is set in \c bquery_create, DATE-TIME will be "YYYY-mm-dd
 * HH:MM:SS.uuuuuu" and HOST will be hostname. If \c is_text flag is not set,
 * DATE-TIME will be "(secons since Epoc).(microsecond)" and HOST will be just a
 * number (host_id).
 *
 * If this function is called repeatedly on the same query handle \c q, it will
 * return the next result until there are no more results.
 *
 * ***REMARK*** This function will automatically allocate a buffer for the
 * result. The caller is responsible to free it.
 *
 * \param q The query handle.
 * \param rc The return code. 0 for successful query. \c ENOENT for no more
 * results and other error code for other errors.
 *
 * \return Result string.
 * \return NULL if there are no more results or error. \c *rc will be set
 * 	accordingly.
 */
char* bq_query(struct bquery *q, int *rc);
char* bq_imgquery(struct bimgquery *q, int *rc);

int bq_query_r(struct bquery *q, char *buff, size_t bufsz);

/**
 * \param store The store handle.
 * \returns on success, all patterns in the \c store. Patterns are separated
 * with '\\n'. The returned string is automatically allocated, and the caller is
 * responsible for freeing it.
 * \return NULL on error, and errno will be set properly.
 */
char* bq_get_ptns(struct bq_store *store);

int bq_get_ptns_r(struct bq_store *store, char *buf, size_t buflen);

char* bq_get_ptn_tkns(struct bq_store *store, int ptn_id, int arg_idx);
