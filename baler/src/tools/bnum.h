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
#endif
