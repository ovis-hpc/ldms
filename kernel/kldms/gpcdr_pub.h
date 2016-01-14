/**
 * The Gemini Performance Counter Daemon Reporting driver.
 *
 * Copyright (C) 2013 Cray Inc. All Rights Reserved.
 */

#ifndef _GPCD_REPORTING_PUB_H_
#define _GPCD_REPORTING_PUB_H_

/* Userspace interfaces */

#define GPCD_REPORTING_DEV_PATH "/dev/gpcdr0"

#define GPCDR_IOC_CREATE_METRICSET    _IO('g', 0x01)
#define GPCDR_IOC_DELETE_METRICSET    _IO('g', 0x02)
#define GPCDR_IOC_NEW_ADDSUB_METRIC   _IO('g', 0x03)
#define GPCDR_IOC_NEW_TIME_METRIC     _IO('g', 0x04)
#define GPCDR_IOC_NEW_LINKSTAT_METRIC _IO('g', 0x05)
#define GPCDR_IOC_NEW_COMPOSITE_METRIC _IO('g', 0x06)
#define GPCDR_IOC_DELETE_METRIC       _IO('g', 0x10)
#define GPCDR_IOC_SET_SAMPLE_LIFETIME _IO('g', 0x11)
#define GPCDR_IOC_MAP_METRICSET       _IO('g', 0x12)
#define GPCDR_IOC_UNMAP_METRICSET     _IO('g', 0x13)


/* Debugging */
#define GPCDR_IOC_GET_NUM_SETS        _IO('g', 0xff)
#define GPCDR_IOC_PRINTK_SETS         _IO('g', 0xfe)
#define GPCDR_IOC_PRINTK_SET_METRICS  _IO('g', 0xfd)

void gpcdr_remove(struct pci_dev *pdev);
int gpcdr_init(void);
void gpcdr_exit(void);
void gpcdr_sample(void);

extern int            gpcdr_regs_size; // size of array
extern uint64_t *gpcdr_current_sample;
extern uint64_t *gpcdr_regs;

typedef struct {
	char *metricsetname;
} gpcdr_create_metricset_args;

typedef struct {
	char *metricsetname;
} gpcdr_delete_metricset_args;

typedef struct {
	char *metricsetname; // metricset to add metric to
	char *metricname;
	char *metricunitsname;

	int   metricbytes; // number of bytes to report

	int   visible;     // whether the metric should be reported.

	int   scale_mult;  // scaling factor
	int   scale_div;   // scaling divisor

	int   numaddregs;
	uint64_t *addregs;

	int   numsubregs;
	uint64_t *subregs;

	int   regs_num_bits; // how many bits before register rollover.
	                     // 0 to not track rollover.
} gpcdr_new_addsub_metric_args;

typedef enum {
	TMU_SECONDS,
	TMU_MS,
	TMU_US,
	TMU_NS,
} gpcdr_timeunit;

typedef struct {
	char *metricsetname;
	char *metricname;
	char *metricunitsname;

	int metricbytes;

	int visible;

	gpcdr_timeunit timeunit;
} gpcdr_new_time_metric_args;


typedef enum {
	GPCDR_LS_DIR_SEND,
	GPCDR_LS_DIR_RECV,
} gpcdr_linkstatus_dir;

/* The registers used for link status aren't performance counters, so
 * they're not available through gpcd.  Pass in the tile numbers here,
 * and gpcd_reporting will find the underlying status regs. */
typedef struct {
	char *metricsetname;
	char *metricname;
	char *metricunitsname;

	int metricbytes;

	int visible;

	int num_tiles;
	uint64_t tiles[48];

	gpcdr_linkstatus_dir dir;
} gpcdr_new_linkstatus_metric_args;

typedef struct {
	char *metricsetname;
	char *metricname;
	char *metricunitsname;

	int metricbytes;

	int visible;

	int   scale_mult;  // scaling factor
	int   scale_div;   // scaling divisor

	int   numaddmets;
	char  **addmets;

	int   numsubmets;
	char  **submets;
} gpcdr_new_composite_metric_args;


typedef struct {
	char *metricsetname;
	char *metricname;
} gpcdr_delete_metric_args;

typedef struct {
	unsigned int lifetime_ms; // milliseconds
} gpcdr_set_sample_lifetime_args;


/* debugging args */

typedef struct {
	int *numsets;
} gpcdr_get_num_sets_args;

typedef struct {
} gpcdr_printk_metricsets_args;

typedef struct {
	char *metricsetname;
} gpcdr_printk_set_metrics_args;

#endif /* _GPCD_REPORTING_PUB_H_ */
