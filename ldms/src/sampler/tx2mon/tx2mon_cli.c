// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Marvell International Ltd.
 */
/* Extracted from tx2mon.c in
 * https://github.com/jchandra-cavm/tx2mon/
 */
#include "tx2mon.h"
#include "assert.h"
#include "stdlib.h"
#include <sys/types.h>
#include <unistd.h>

int tx2mon_read_node(struct cpu_info *d)
{
	assert(d!=NULL);
	int rv;
	struct mc_oper_region *op = &d->mcp;
	rv = lseek(d->fd, 0, SEEK_SET);
	if (rv == (off_t) -1)
		return rv;
	rv = read(d->fd, op, sizeof(*op));
	if (rv < sizeof(*op))
		return 2;
	if (CMD_STATUS_READY(op->cmd_status) == 0)
		return 0;
	if (CMD_VERSION(op->cmd_status) > 0)
		d->throttling_available = 1;
	else
		d->throttling_available = 0;
	return 1;
}

