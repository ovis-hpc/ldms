/**
 * \file bnumvec.c
 * \author Narate Taerat (narate at ogc dot us)
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <assert.h>
#include <math.h>

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

struct __bpivot_idx {
	uint64_t pidx0;
	uint64_t pidx1;
};

/**
 * Partition <code>vec[lidx..ridx]</code> into two partitions with a pivot in
 * the range.
 *
 * \retval pivot_idx the index to the partition pivot.
 */
struct __bpivot_idx bnumvec_partition(struct bmvec_char *vec, uint64_t lidx, uint64_t ridx)
{
	struct bnum *p;
	struct bnum *q;
	struct bnum *x;
	uint64_t i, r, l;
	struct __bpivot_idx pidx;
	int c;

	q = bmvec_generic_get(vec, (lidx+ridx)/2, sizeof(*p));
	p = bmvec_generic_get(vec, lidx, sizeof(*q));
	bnum_swap(q, p);
	/* now p is the pivot for partitioning */

	i = lidx+1;
	l = i;
	r = ridx;

	/*
	 * In the while loop:
	 * [lidx] is pivot
	 * [lidx+1 .. l-1] contains left partition
	 * [r+1 .. ridx] contain right partition
	 */

	q = bmvec_generic_get(vec, i, sizeof(*q));
	/* q will always point to vec[i] */
	while (i <= r) {
		c = bnum_cmp(p, q);
		switch (c) {
		case 1:
			/* p > q */
			/* put q to the left partition */
			if (i != l) {
				x = bmvec_generic_get(vec, l, sizeof(*x));
				bnum_swap(q, x);
			}
			l++;
			/* let through */
		case 0:
			/* p == q */
			/* just go to next entry */
			i++;
			q = bmvec_generic_get(vec, i, sizeof(*q));
			break;
		case -1:
			/* p < q */
			/* put q to the right partition */
			if (i != r) {
				x = bmvec_generic_get(vec, r, sizeof(*x));
				bnum_swap(q, x);
			}
			r--;
			/* q is now pointed to the new element (b/c of swapping) */
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

	/* [lidx .. l-1] is the left partition */
	/* [l .. r] is the elements equal to pivot */
	/* [r+1 .. ridx] is the right partition */
	pidx.pidx0 = l;
	pidx.pidx1 = r;

	return pidx;
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
	struct __bpivot_idx pidx;
loop:
	qent_r = NULL;
	qent = (void*)bqueue_dq(sort_queue);
	if (!qent)
		goto out;
	pidx = bnumvec_partition(bnumvec, qent->lidx, qent->ridx);
	if (pidx.pidx0 && qent->lidx < (pidx.pidx0-1)) {
		qent_l = malloc(sizeof(*qent_l));
		if (!qent_l) {
			berror("malloc()");
			exit(-1);
		}
		qent_l->lidx = qent->lidx;
		qent_l->ridx = pidx.pidx0-1;
		bqueue_nq(sort_queue, (void*)qent_l);
		get_qref();
	}
	if ((pidx.pidx1+1) < qent->ridx) {
		qent_r = malloc(sizeof(*qent_r));
		if (!qent_r) {
			berror("malloc()");
			exit(-1);
		}
		qent_r->lidx = pidx.pidx1+1;
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

struct bnum *bnumvec_select_nth(struct bmvec_char *vec, uint64_t lidx,
				uint64_t ridx, uint64_t nth)
{
	struct __bpivot_idx pidx = {lidx, lidx};

	while (lidx < ridx) {
		pidx = bnumvec_partition(vec, lidx, ridx);

		if (pidx.pidx0 <= nth && nth <= pidx.pidx1)
			/* pivot is the nth element */
			break;

		/* recursive, excluding the pivot */
		if (nth < pidx.pidx0) {
			ridx = pidx.pidx0-1;
		} else {
			lidx = pidx.pidx1+1;
		}
	}

	assert(lidx <= ridx);
	return bmvec_generic_get(vec, nth, sizeof(struct bnum));
}

/**
 * Kahan summation algorithm.
 */
double bnumvec_kahan_sum(struct bmvec_char *vec,
			 double (*fn)(struct bnum*, void*), void *arg)
{
	struct bnum *num;
	double sum = 0;
	double x;
	double tmp;
	double comp = 0.0; /* error compensation */
	uint64_t i, n;
	n = vec->bvec->len;
	for (i = 0; i < n; i++) {
		num = bmvec_generic_get(vec, i, sizeof(*num));
		if (fn)
			x = fn(num, arg) - comp;
		else
			x = num->d - comp;
		tmp = sum + x;
		comp = (tmp - sum) - x;
		sum = tmp;
	}
	return sum;
}

static
double bnum_diff(struct bnum *num, void *arg)
{
	return num->d - *(double*)arg;
}

static
double bnum_abs_diff(struct bnum *num, void *arg)
{
	return fabs(num->d - *(double*)arg);
}

static
double bnum_square_diff(struct bnum *num, void *arg)
{
	double tmp = num->d - *(double*)arg;
	return tmp*tmp;
}

void op_proc_stat()
{
	uint64_t i;
	uint64_t len = bnumvec->bvec->len;
	struct bnum *x, *_mad;
	struct bnum tmp;
	struct bnum q1, med, q3;
	struct bnum min, max;
	double mean;
	double sd;
	double sse;
	double sum;
	uint64_t len1, len2, len3;

	struct bmvec_char *bmvec_mad;

	struct bdstr *bdstr = bdstr_new(4096);

	if (!bdstr) {
		berror("bdstr_new()");
		exit(-1);
	}

	bdstr_reset(bdstr);
	bdstr_append_printf(bdstr, "%s.bmad", bmvec_generic_get_path(bnumvec));

	bmvec_mad = bmvec_generic_open(bdstr->str);
	if (!bmvec_mad) {
		berror("bmvec_generic_open()");
		exit(-1);
	}

	bmvec_generic_reset(bmvec_mad);

	len1 = (len-1)/4;
	len2 = (len-1)/2;
	len3 = ((len-1)/4.0) * 3.0;

	med = *bnumvec_select_nth(bnumvec, 0, len-1, len2);
	q1 = *bnumvec_select_nth(bnumvec, 0, len2-1, len1);
	q3 = *bnumvec_select_nth(bnumvec, len2+1, len-1, len3);
	min = *bnumvec_select_nth(bnumvec, 0, len1-1, 0);
	max = *bnumvec_select_nth(bnumvec, len3+1, len-1, len-1);

	printf("min: %ld (%lf)\n", min.i64, min.d);
	printf("q1: %ld (%lf)\n", q1.i64, q1.d);
	printf("median: %ld (%lf)\n", med.i64, med.d);
	printf("q3: %ld (%lf)\n", q3.i64, q3.d);
	printf("max: %ld (%lf)\n", max.i64, max.d);

	/* populate absolute deviation vector */
	for (i = 0; i < len; i++) {
		x = bmvec_generic_get(bnumvec, i, sizeof(*x));
		tmp.d = fabs(x->d - med.d);
		tmp.i64 = x->i64 - med.i64;
		if (tmp.i64 < 0)
			tmp.i64 = -tmp.i64;
		bmvec_generic_append(bmvec_mad, &tmp, sizeof(tmp));
	}

	/* pick Median of Absolute Deviation (MAD) */
	_mad = bnumvec_select_nth(bmvec_mad, 0, len-1, (len-1)/2);

	printf("MAD: %lf\n", _mad->d);

	sum = bnumvec_kahan_sum(bnumvec, NULL, NULL);
	mean = sum / len;
	printf("mean: %lf\n", mean);

	sse = bnumvec_kahan_sum(bnumvec, bnum_square_diff, &mean);
	printf("sse: %lf\n", sse);

	sd = sqrt(sse/(len-1));
	printf("sd: %lf\n", sd);
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
