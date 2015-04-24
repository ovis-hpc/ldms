/**
 * \file bnumvec.c
 * \author Narate Taerat (narate at ogc dot us)
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <assert.h>

#include "baler/btypes.h"
#include "baler/butils.h"
#include "baler/bmvec.h"
#include "baler/bqueue.h"

#include "bnum.h"

const char *op_str;
int op = 0;
const char *path = NULL;
int sort_thread_num = 1;

struct bmvec_char *bnumvec = NULL;

typedef void (*op_proc)(void);

enum bnumvec_op_e{
	BNUMVEC_OP_FIRST = 1,
	BNUMVEC_OP_DUMP = BNUMVEC_OP_FIRST,
	BNUMVEC_OP_SORT,
	BNUMVEC_OP_STAT,
	BNUMVEC_OP_LAST,
};

const char *__bnumvec_op_str[] = {
	[BNUMVEC_OP_DUMP] = "DUMP",
	[BNUMVEC_OP_SORT] = "SORT",
	[BNUMVEC_OP_STAT] = "STAT",
};

const char *bnumvec_op_str(int op)
{
	if (op < BNUMVEC_OP_FIRST || BNUMVEC_OP_LAST <= op)
		return "UNKNOWN";
	return __bnumvec_op_str[op];
}

int bnumvec_op_from_str(const char *str)
{
	int i, rc;
	for (i = BNUMVEC_OP_FIRST; i < BNUMVEC_OP_LAST; i++) {
		if (strcasecmp(str, __bnumvec_op_str[i]) == 0) {
			break;
		}
	}
	return i;
}

const char *short_opt = "o:t:?";
struct option long_opt[] = {
	{"op",      1,  0,  'o'},
	{"thread",  1,  0,  't'},
	{"help",    0,  0,  '?'},
	{0,         0,  0,  0}
};

void usage()
{
	printf(
"\n"
"SYNOPSIS bnumvec -o OPERATION [OPERATION_OPTIONS] BNUMVEC_FILE\n"
"\n"
"OPERATION:\n"
"	dump	Dump the bmvec in text format into STDOUT.\n"
"	sort	Sort data in BNUMVEC_FILE.\n"
"	stat	Report statistics about data in the BNUMVEC_FILE.\n"
"\n"
"OPTIONS for SORT\n"
"	-t,--thread NUMBER	The number of threads for sorting.\n"
	      );
}

void handle_args(int argc, char **argv)
{
	char c;
loop:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case -1:
		goto out;
	case 'o':
		op_str = optarg;
		break;
	case 't':
		sort_thread_num = atoi(optarg);
		if (sort_thread_num < 1)
			sort_thread_num = 1;
		break;
	case '?':
	default:
		usage();
		exit(-1);
	}
	goto loop;
out:
	if (optind < argc) {
		path = argv[optind];
	} else {
		berr("A path to the bnumvec file is needed");
		exit(-1);
	}
}

void op_proc_dump()
{
	uint64_t i;
	uint64_t len = bnumvec->bvec->len;
	struct bnum *bnum;
	printf("length: %lu\n", len);
	printf("-----------------------\n");
	for (i = 0; i < len; i++) {
		bnum = bmvec_generic_get(bnumvec, i, sizeof(*bnum));
		printf("%ld (%lf)\n", bnum->i64, bnum->d);
	}
	printf("-----------------------\n");
}

/**
 * Partition <code>vec[lidx..ridx]</code> into two partitions with a pivot in
 * the range.
 *
 * \retval pivot_idx the index to the partition pivot.
 */
uint64_t bnumvec_partition(struct bmvec_char *vec, uint64_t lidx, uint64_t ridx)
{
	struct bnum *p;
	struct bnum *q;
	struct bnum *x;
	uint64_t i, r;
	int c;

	q = bmvec_generic_get(vec, (lidx+ridx)/2, sizeof(*p));
	p = bmvec_generic_get(vec, lidx, sizeof(*q));
	bnum_swap(q, p);
	/* now p is the pivot for partitioning */

	i = lidx+1;
	r = ridx;

	q = bmvec_generic_get(vec, i, sizeof(*q));
	/* q will always point to vec[i] */
	while (i <= r) {
		c = bnum_cmp(p, q);
		switch (c) {
		case 0:
		case 1:
			/* p <= q */
			i++;
			q = bmvec_generic_get(vec, i, sizeof(*q));
			break;
		case -1:
			/* p > q */
			if (i != r) {
				x = bmvec_generic_get(vec, r, sizeof(*x));
				bnum_swap(q, x);
			}
			r--;
			break;
		}
	}

	/* vec[r + 1 .. ridx] are greater than p */
	/* move p to vec[r] */
	if (r != lidx) {
		x = bmvec_generic_get(vec, r, sizeof(*x));
		bnum_swap(p, x);
		p = x;
	}

	return r;
}

struct bnumvec_sort_qentry {
	struct bqueue_entry qent;
	uint64_t lidx;
	uint64_t ridx;
};

int __qref_count = 0;
pthread_mutex_t __qref_mutex = PTHREAD_MUTEX_INITIALIZER;
struct bqueue *sort_queue;

static
void get_qref()
{
	pthread_mutex_lock(&__qref_mutex);
	__qref_count++;
	pthread_mutex_unlock(&__qref_mutex);
}

static
void put_qref()
{
	pthread_mutex_lock(&__qref_mutex);
	__qref_count--;
	if (__qref_count == 0)
		bqueue_term(sort_queue);
	pthread_mutex_unlock(&__qref_mutex);
}

void *sort_thread_proc(void *arg)
{
	struct bnumvec_sort_qentry *qent, *qent_r, *qent_l;
	uint64_t pidx;
loop:
	qent_r = NULL;
	qent = (void*)bqueue_dq(sort_queue);
	if (!qent)
		goto out;
	pidx = bnumvec_partition(bnumvec, qent->lidx, qent->ridx);
	if (pidx && qent->lidx < (pidx-1)) {
		qent_l = malloc(sizeof(*qent_l));
		if (!qent_l) {
			berror("malloc()");
			exit(-1);
		}
		qent_l->lidx = qent->lidx;
		qent_l->ridx = pidx-1;
		bqueue_nq(sort_queue, (void*)qent_l);
		get_qref();
	}
	if ((pidx+1) < qent->ridx) {
		qent_r = malloc(sizeof(*qent_r));
		if (!qent_r) {
			berror("malloc()");
			exit(-1);
		}
		qent_r->lidx = pidx+1;
		qent_r->ridx = qent->ridx;
		bqueue_nq(sort_queue, (void*)qent_r);
		get_qref();
	}
	free(qent);
	put_qref();
	goto loop;
out:
	return NULL;
}

void op_proc_sort()
{
	int extra_thread_num = sort_thread_num - 1;
	struct bnumvec_sort_qentry *qent;
	pthread_t *thrd = NULL;
	int i, rc;

	if (bnumvec->bvec->len == 0) {
		/* Nothing to sort */
		berr("No element to be sorted");
		exit(-1);
	}

	qent = malloc(sizeof(*qent));
	if (!qent) {
		berror("malloc()");
		exit(-1);
	}
	qent->lidx = 0;
	qent->ridx = bnumvec->bvec->len-1;

	sort_queue = bqueue_new();
	if (!sort_queue) {
		berror("bqueue_new()");
		exit(-1);
	}

	bqueue_nq(sort_queue, &qent->qent);

	if (extra_thread_num) {
		thrd = malloc(extra_thread_num*sizeof(*thrd));
		if (!thrd) {
			berror("malloc()");
			exit(-1);
		}
	}
	for (i = 0; i < extra_thread_num; i++) {
		rc = pthread_create(&thrd[i], NULL, sort_thread_proc, NULL);
		if (rc) {
			berrorrc("pthread_create()", rc);
			exit(-1);
		}
	}

	sort_thread_proc(NULL);

	for (i = 0; i < extra_thread_num; i++) {
		pthread_join(thrd[i], NULL);
	}
}

void op_proc_stat()
{
	berr("Not implemented");
}

int main(int argc, char **argv)
{
	static op_proc op_proc[] = {
		[BNUMVEC_OP_DUMP] = op_proc_dump,
		[BNUMVEC_OP_SORT] = op_proc_sort,
		[BNUMVEC_OP_STAT] = op_proc_stat,
	};
	handle_args(argc, argv);
	op = bnumvec_op_from_str(op_str);
	switch (op) {
	case BNUMVEC_OP_DUMP:
	case BNUMVEC_OP_SORT:
	case BNUMVEC_OP_STAT:
		if (!bfile_exists(path)) {
			berr("File not exist: %s", path);
			exit(-1);
		}
		bnumvec = bmvec_generic_open(path);
		if (!bnumvec) {
			berror("bmvec_generic_open()");
			exit(-1);
		}
		op_proc[op]();
		break;
	default:
		berr("Unknown operation: %s", op_str);
		exit(-1);
	}
	return 0;
}
