/**
 *  The Gemini Performance Counter Reporting driver.
 *
 *  Copyright (C) 2013 Cray Inc. All Rights Reserved.
 */

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/sysfs.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>

#include "ghal.h"
#ifdef XXX
#include <ghal_pub.h>
#include <gni_pub.h>
#include <gni_priv.h>
#endif

#include "gpcd_kapi.h"
#include "gpcd_lib.h"
#include "gpcdr_pub.h"


MODULE_AUTHOR("Cray Inc.");
MODULE_DESCRIPTION("Cray(R) Gemini Performance Counter Reporting Interface");
MODULE_LICENSE("GPL");

char gpcdr_driver_string[] = "Cray(R) Gemini Performance Counter Reporting Device";
char gpcdr_driver_copyright[] = "Copyright (C) 2013 Cray Inc.";
char gpcdr_driver_name[] = "gpcdr";

static spinlock_t gpcdr_lock;

typedef struct gpcdr_device_s {
	struct pci_dev	*pdev;
	uint32_t        minor_number;
} gpcdr_device_t;

/*
 * Array of regs to pass to gpcd.  Each metric will track its regs as
 * indices into this array.  This way we can make one call to
 * gpcd_read_regs().  Each metric will read from the one result set,
 * with the same indices.
 *
 * Access to the array is protected by the gpcdr_lock spinlock.
 * This would be natural for rcu if there were actually any contention.
 *
 * If a metric is removed, we do not try to remove its registers from
 * gpcdr_regs.
 */
int            gpcdr_num_regs;  // number of registers in use
int            gpcdr_regs_size; // size of array
uint64_t *gpcdr_regs;

uint64_t *gpcdr_current_sample;
static struct timespec gpcdr_current_sample_ts;

static unsigned int gpcdr_sample_lifetime_ms = 0;
static struct timespec gpcdr_sample_lifetime_ts;

/*
 * register rollover detection/reporting
 *
 * Three arrays shadowing gpcdr_regs, all protected by the gpcdr_lock:
 *
 * gpcdr_oldregs - previous contents of gpcdr_regs.  Any rolling-over
 * registers whose values are less in gpcdr_regs than in gpcdr_oldregs
 * are deemed to have rolled over.
 *
 * gpcdr_rollovers - previously identified rollover.  This contains
 * the amount to add to the corresponding value in gpcdr_regs to
 * report to the consumer.
 *
 * gpcdr_rolloverinc - rollover increment.  This contains the amount
 * to increment gpcdr_rollovers by when a rollover is detected.  This
 * comes from the configuration of the first metric that uses this
 * register.  Any other metric using this register must specify the
 * same rollover details.
 *
 * If we're not tracking rollover for a register (e.g., lane status),
 * gpcdr_rolloverincr[i] will be 0.
 */
static uint64_t *gpcdr_prev_sample;
static uint64_t *gpcdr_rollovers;
static uint64_t *gpcdr_rolloverincr;

/*
 * gpcdr_lock must be held, except for call from gpcdr_init().
 * 
 * Returns 0 on success, -errno on error.
 */
static int gpcdr_resize_arrays(int nregs, int gfp_flags)
{
	void *t;
	int numnewregs = nregs - gpcdr_regs_size;


	/* Won't have to worry about a middle krealloc() failing when
	 * we're shrinking, because we won't be shrinking. */
	BUG_ON(numnewregs < 0);

	t = krealloc(gpcdr_regs, nregs * sizeof(gpcdr_regs[0]), gfp_flags);
	if (!t)
		return -ENOMEM;
	gpcdr_regs = t;

	t = krealloc(gpcdr_current_sample,
		     nregs * sizeof(gpcdr_current_sample[0]), gfp_flags);
	if (!t)
		return -ENOMEM;
	gpcdr_current_sample = t;

	t = krealloc(gpcdr_prev_sample,
		     nregs * sizeof(gpcdr_prev_sample[0]), gfp_flags);
	if (!t)
		return -ENOMEM;
	gpcdr_prev_sample = t;

	t = krealloc(gpcdr_rollovers,
		     nregs * sizeof(gpcdr_rollovers[0]), gfp_flags);
	if (!t)
		return -ENOMEM;
	gpcdr_rollovers = t;

	t = krealloc(gpcdr_rolloverincr,
		     nregs * sizeof(gpcdr_rolloverincr[0]), gfp_flags);
	if (!t)
		return -ENOMEM;
	gpcdr_rolloverincr = t;

	/* All fields must be initialized by gpcdr_num_regs incrementer */

	gpcdr_regs_size = nregs;

	return 0;
}

/*
 * gpcdr_lock would need to be held, but we're called only from
 * gpcdr_exit and the gpcdr_init() error case.
 */
static void gpcdr_free_arrays(void)
{
	gpcdr_regs_size = 0;
	kfree(gpcdr_regs);
	kfree(gpcdr_current_sample);
	kfree(gpcdr_prev_sample);
	kfree(gpcdr_rollovers);
	kfree(gpcdr_rolloverincr);

	gpcdr_regs = NULL;
	gpcdr_current_sample = NULL;
	gpcdr_prev_sample = NULL;
	gpcdr_rollovers = NULL;
	gpcdr_rolloverincr = NULL;
}


void gpcdr_sample(void);


/*
 * Fill in idxs[] with the index into gpcdr_regs[] of each reg in regs[].
 *
 * If regs[i] isn't already present in gpcdr_regs[], add it first.
 *
 * Uses rolloverincr for all regs.  Currently assumes it's the same
 * for all added together.
 *
 * Returns 0 on success, -errno on failure.
 *
 * gpcdr_lock must be held.
 */
int gpcdr_track_regs(int numregs, uint64_t *regs, unsigned int *idxs,
			    unsigned long rolloverincr)
{
	int i, j;
	int num_new_regs = 0;

	/* May overcount if regs[] includes the same new reg more than once.
	 * This is not expected, and would cause only a few wasted bytes. */
	for (i = 0; i < numregs; i++) {
		for (j = 0; j < gpcdr_num_regs; j++) {
			if (gpcdr_regs[j] == regs[i]) {
				if (gpcdr_rolloverincr[j] != rolloverincr)
					return -EINVAL;
				idxs[i] = j;
				goto next_i;
			}
		}
		num_new_regs++;
		idxs[i] = -1;
	next_i:
		(void)0;
	}

	if (gpcdr_num_regs + num_new_regs >= gpcdr_regs_size) {
		int rc;

		rc = gpcdr_resize_arrays(gpcdr_num_regs + num_new_regs,
					 GFP_ATOMIC);
		if (rc != 0) {
			return rc;
		}
	}

	for (i = 0; i < numregs; i++) {
		if (idxs[i] == -1) {
			gpcdr_regs[gpcdr_num_regs] = regs[i];
			gpcdr_current_sample[gpcdr_num_regs] = 0;
			gpcdr_prev_sample[gpcdr_num_regs] = 0;
			gpcdr_rollovers[gpcdr_num_regs] = 0;
			gpcdr_rolloverincr[gpcdr_num_regs] = rolloverincr;
			
			idxs[i] = gpcdr_num_regs;
			gpcdr_num_regs++;
		}
	}

	return 0;
}

#ifdef CONFIG_CRAY_GEMINI

/* https://svn.us.cray.com/svn/diags/baker_diags/vregs/trunk/includes/gm_mmap.h */
#define GM_LCB_BASE         0x0002000000ull
#define GM_TILE_BASE        0x0002001000ull


// For GM_LCB_STATUS2_0:
#include "vregs/gm_lcb_defs.h"

// For GM_Y_TILE_LINK_ALIVE_SHADOW
#include "vregs/gm_tile_defs.h"

/* copied from gmnetwatch.h */
#define GM_LCB_ADDR(r,c) (uint64_t)(GM_LCB_BASE | (((r)&7)<<17) | (((c)&7)<<13))

 /* not copied from anywhere */
#define GM_TILE_ADDR(r,c) (uint64_t)(GM_TILE_BASE | (((r)&7)<<17) | (((c)&7)<<13))

static uint64_t laneenable_mmr_addr_from_tile(int tile)
{
	int row, col;

	col = tile & 7;
	row = (tile >> 3) & 7;

	return GM_LCB_ADDR(row, col) + (GM_LCB_STATUS2_0 - GM_LCB_BASE);
}

static uint64_t link_alive_mmr_addr_from_tile(int tile)
{
	int row, col;

	col = tile & 7;
	row = (tile >> 3) & 7;

	return GM_TILE_ADDR(row, col) + (GM_Y_TILE_LINK_ALIVE_SHADOW - GM_TILE_BASE);
}

#else /* !CONFIG_CRAY_GEMINI */
#ifdef XXX
// For AR_SLB_BASE/AR_LCB_BASE AR_NT_BASE
#include "aries/misc/ar_mmap.h"

// For AR_SLB_LCB_CFG_STARTUP_0
#include "aries/vregs/ar_lcb_defs.h"

// For AR_RTR_INQ_CFG_LINK_ALIVE_BITS
#include "aries/vregs/ar_nt_defs.h"
#endif


/* xtcablecheck.c: */
#define AR_COL_BITPOS_START 19
// Compute starting MMR address of LCB
static uint64_t
ar_tile_addr(int tile)
{
	return AR_LCB_BASE | ((tile & 0x3F) << AR_COL_BITPOS_START);
}

uint64_t laneenable_mmr_addr_from_tile(int tile)
{
	return ar_tile_addr(tile) + (AR_SLB_LCB_STS_STARTUP_0 - AR_LCB_BASE);
}

/* following the example of the above */
uint64_t
ar_rtr_addr(int tile)
{
	/* Same bitpos as the LCB COL_BITPOS */
	return AR_NT_BASE | ((tile & 0x3F) << AR_COL_BITPOS_START);
}

uint64_t link_alive_mmr_addr_from_tile(int tile)
{
	return ar_rtr_addr(tile) + (AR_RTR_INQ_CFG_LINK_ALIVE_BITS - AR_NT_BASE);
}
#endif /* CONFIG_CRAY_GEMINI */



/* gpcdr_lock must be held */
static void gpcdr_update_rollovers(void)
{
	int i;

	for (i = 0; i < gpcdr_num_regs; i++) {
		if (gpcdr_rolloverincr[i] &&
		    gpcdr_current_sample[i] < gpcdr_prev_sample[i])
			gpcdr_rollovers[i] += gpcdr_rolloverincr[i];
	}
}

/* gpcdr_lock must be held */
void gpcdr_sample(void)
{
	int rc;
	uint64_t *t;
	struct timespec now;

	if (!gpcdr_regs)
		return;

	getnstimeofday(&now);

	/* If there's a sample lifetime defined, just return if we
	 * haven't exceeded it yet.  But don't return if we haven't
	 * yet taken a sample. */
	if (gpcdr_sample_lifetime_ms != 0) {
		if (gpcdr_current_sample_ts.tv_sec != 0 ||
		    gpcdr_current_sample_ts.tv_nsec != 0) {
			struct timespec expires;

			/* 
			 * timespec_add_safe isn't EXPORT_SYMBOLed,
			 * and timespec_add() doesn't exist in sp1.
			 *
			 * Open-code timespec_add() from SP2.
			 */
			set_normalized_timespec(
				&expires, 
				gpcdr_current_sample_ts.tv_sec +
				  gpcdr_sample_lifetime_ts.tv_sec,
				gpcdr_current_sample_ts.tv_nsec + 
				  gpcdr_sample_lifetime_ts.tv_nsec);

			if (timespec_compare(&now, &expires) <= 0)
				return;
		}
	}

	t = gpcdr_current_sample;
	gpcdr_current_sample = gpcdr_prev_sample;
	gpcdr_prev_sample = t;

	rc = gpcd_read_registers(gpcdr_regs_size,
				 gpcdr_regs, gpcdr_current_sample);
	if (rc != gpcdr_regs_size) {
		printk(KERN_ERR "gpcd_read_regs returned %d out of %d\n",
		       rc, gpcdr_num_regs);
	}

	gpcdr_current_sample_ts = now;

	gpcdr_update_rollovers();

}

void gpcdr_remove(struct pci_dev *pdev)
{
	gpcdr_device_t *sdev = (gpcdr_device_t*)
		ghal_get_subsys_data(pdev, GHAL_SUBSYS_GPCD_REPORTING);

	// changed order vs gpcd_remove().  Improvement?
	ghal_set_subsys_data(pdev, GHAL_SUBSYS_GPCD, NULL);

	kfree(sdev);

	printk(KERN_INFO "GEMINI Performance Counter Daemon Reporting"
	       " removed\n");
}

static int gpcdr_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	gpcdr_device_t *sdev;

	sdev = kzalloc(sizeof(gpcdr_device_t), GFP_KERNEL);
	if (!sdev) {
		printk(KERN_ERR "gpcdr sdev alloc failed\n");
		return -ENOMEM;
	}

	sdev->pdev = pdev;

	ghal_set_subsys_data(pdev, GHAL_SUBSYS_GPCD_REPORTING, sdev);
	printk(KERN_DEBUG "GEMINI Performance Counter Daemon Reporting.\n");
	return 0;
}

/* gpcdr_init - set up character driver for gpcdr
 * device, and register device major number.  Follows gpcd_init().
 *
 * Returns 0 on success, -errno on error.
 */
int gpcdr_init(void)
{
	int error;
	int initi_regs_size = 384;

	if (ghal_get_subsystem(GHAL_SUBSYS_IPOGIF) ||
		ghal_get_subsystem(GHAL_SUBSYS_GNI)) {
		// then what?  Just cribbed from gpcd_init()
	}

	error = gpcdr_resize_arrays(init_regs_size, GFP_KERNEL);
	if (error != 0) {
		printk(KERN_ERR "initial resize to %d failed\n", init_regs_size);
		return error;
	}
	
	printk(KERN_DEBUG "Cray(R) Gemini Performance Counter Daemon Reporting- version 0.1\n");

	spin_lock_init(&gpcdr_lock);
	gpcd_hal_init();

	return ghal_add_subsystem(GHAL_SUBSYS_GPCD_REPORTING,
				  gpcdr_probe, gpcdr_remove);
}

void gpcdr_exit(void) {
	ghal_remove_subsystem(GHAL_SUBSYS_GPCD_REPORTING);

	gpcdr_free_arrays();
}

