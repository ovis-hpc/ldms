/**
 * \file bnumvec.c
 * \author Narate Taerat (narate at ogc dot us)
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

#include "baler/btypes.h"
#include "baler/butils.h"
#include "baler/bmvec.h"

#include "bnum.h"

const char *op_str;
int op = 0;
const char *path = NULL;

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

const char *short_opt = "o:?";
struct option long_opt[] = {
	{"op", 1, 0, 'o'},
	{"help", 0, 0, '?'},
	{0, 0, 0, 0}
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

void op_proc_sort()
{
	berr("Not implemented");
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
