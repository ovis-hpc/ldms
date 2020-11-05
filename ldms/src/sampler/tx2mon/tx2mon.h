#ifndef tx2mon_h
#define tx2mon_h
/*
 * tx2mon.h -	LDMS sampler for basic Marvell TX2 chip telemetry.
 */

/*
 *  Copyright [2020] Hewlett Packard Enterprise Development LP
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to:
 *
 *   Free Software Foundation, Inc.
 *   51 Franklin Street, Fifth Floor
 *   Boston, MA 02110-1301, USA.
 */

/*
 * Header file from github.com/jchandra-cavm/tx2mon.git, which
 * is required to be installed elsewhere on the build system,
 * and pointed to during the configure phase.
 *
 * configure --enable-tx2mon
 *
 * If there is no tx2mon/mc_oper_region.h in /usr/include,
 * provide CFLAGS=$(tx2mon_include_path) pointing to the header location.
 */
#include <stdint.h>
#include <tx2mon/mc_oper_region.h>

#include <limits.h>
#include <stdio.h>

/*
 * Location of the sysfs entries created by the kernel module.
 *
 * NOTE: TX2MON_NODE_PATH is a snprintf() string to create actual path.
 */
#define	TX2MON_SYSFS_PATH	"/sys/bus/platform/devices/tx2mon/"
#define	TX2MON_SOCINFO_PATH	TX2MON_SYSFS_PATH "socinfo"
#define TX2MON_NODE_PATH      TX2MON_SYSFS_PATH "node%d_raw"
/*
 * Max number of CPUs (TX2 chips) supported.
 */
#define	TX2MON_MAX_CPU	(2)

/*
 * Per-CPU record keeping
 */
struct cpu_info {
	int	fd;		/* fd of raw file opened */
	int	metric_offset;	/* starting offset into schema for this CPU */
	unsigned int throttling_available;
	int	node;
	struct	mc_oper_region mcp;	/* mmapped data structure (from fd) */
};

/*
 * Per-sampler record keeping
 */
struct tx2mon_sampler {
	int	n_cpu;		/* number of CPUs (e.g. TX2 chips) present */
	int	n_core;		/* cores *per CPU* */
	int	n_thread;	/* threads *per core* (unused currently) */
	FILE	*fileout;
	int 	samples;
	struct cpu_info cpu[TX2MON_MAX_CPU];
} ;

/* Read the information located in th the node file directory.
 * @return 1 if ok, 2 if read is short, 0 if data is unready,
 * -1 if error (see errno). */
int tx2mon_read_node(struct cpu_info *d);

#endif
