/**
 * \file bnum.h
 * \author Narate Taerat (narate at ogc dot us)
 */
#ifndef __BNUM_H
#define __BNUM_H
#include "baler/btypes.h"
#include "baler/butils.h"
struct bnum {
	union {
		uint64_t u64;
		int64_t i64;
	};
	double d;
};

static inline
void bnum_swap(struct bnum *n0, struct bnum *n1)
{
	struct bnum tmp;
	tmp = *n0;
	*n0 = *n1;
	*n1 = tmp;
}

static inline
int bnum_cmp(struct bnum *n0, struct bnum *n1)
{
	if (n0->i64 < n1->i64)
		return -1;
	if (n0->i64 > n1->i64)
		return 1;
	if (n0->d < n1->d)
		return -1;
	if (n0->d > n1->d)
		return 1;
	return 0;
}

#endif
