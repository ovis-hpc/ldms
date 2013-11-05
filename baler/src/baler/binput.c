#include "binput.h"
#include "binput_private.h"

struct bwq *binq;

int binq_post(struct bwq_entry *e)
{
	bwq_nq(binq, e);
	return 0;
}

