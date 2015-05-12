#include <stdio.h>
#include <getopt.h>
#include <string.h>

#include "baler/butils.h"
#include "baler/btkn.h"

const char *store_path = NULL;

const char *short_opt = "s:?";
struct option long_opt[] = {
	{"store",  1,  0,  'o'},
	{"help",   0,  0,  '?'},
	{0,        0,  0,  0}
};

void usage()
{
	printf("SYNOPSIS btkn_dump -s BTKN_DIR\n");
}

void handle_args(int argc, char **argv)
{
	char c;
loop:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case -1:
		goto out;
	case 's':
		store_path = optarg;
		break;
	case '?':
	default:
		usage();
		exit(-1);
	}
out:
	return;
}

static
int cb(uint32_t tkn_id, const struct bstr *bstr, const struct btkn_attr *attr)
{
	printf("%10u ", tkn_id);

	printf("%10s ", btkn_attr_type_str(attr->type));

	printf("%.*s ", bstr->blen, bstr->cstr);

	printf("\n");

	return 0;
}

int main(int argc, char **argv)
{
	handle_args(argc, argv);
	if (!store_path) {
		berr("-s PATH is needed");
		exit(-1);
	}
	if (!bfile_exists(store_path)) {
		berr("File not found: %s", store_path);
		exit(-1);
	}
	struct btkn_store *tkn_store = btkn_store_open(store_path, 0);
	if (!tkn_store) {
		berror("btkn_store_open()");
		exit(-1);
	}

	btkn_store_iterate(tkn_store, cb);

	return 0;
}
