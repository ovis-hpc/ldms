#include <inttypes.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include "gpcd_lib.h"
#include "gpcd_pub.h"

/**
 * ABOUT: this prints out the valid counters for use in aries_mmr_configurable.
 * It also illustrates the use of R/W a counter, permissions setting, and verbostity functions in gpcd
 *
 * TO BUILD Standalone:  gcc -L /opt/cray/gni/default/lib64 -lgpcd -I/opt/cray/gni/default/include/gpcd check_mmr_configurable.c
 * You can examine counter changes in dmesg:
 * [2792441.586200] GPCD:0 Register: 0xFFFFC9000D000010 Addr: 0x00007FFFFFFF74D8 wrote Value 0x0000000000000000
 */


gpcd_context_t* ctx;
typedef enum {
	CTR_UINT64,
	CTR_HEX
} ctr_type_t;


/**
 * Build list of performance counters we wish to get values for.
 * No aggregation.
 */
int addMetricToContext(gpcd_context_t* lctx, char* met)
{
	gpcd_mmr_desc_t *desc;
	int status;
	int i;

	if (lctx == NULL){
		printf( ": NULL context\n");
		return -1;
	}

	desc = (gpcd_mmr_desc_t *)
		gpcd_lookup_mmr_byname(met);
	if (!desc) {
		printf( ": Could not lookup (2) <%s>\n", met);
		return -1;
	}

	status = gpcd_context_add_mmr(lctx, desc);
	if (status != 0) {
		printf( ": Could not add mmr for <%s>\n", met);
		gpcd_remove_context(lctx); //some other option?
		return -1;
	}

	return 0;
}


int main(){
	int rc;
	int count;
	int usecontext = 1;
	int dowrite = 1;

	/* options of counters to test */
	/*	char* met = "AR_NIC_ORB_PRF_NET_RSP_TRACK_1"; */
	/* char* met = "AR_NIC_ORB_PRF_NET_RSP_TRACK_1_MAX_RSP_TIME_BP"; */

//	char* met = "AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_FLITS";
//	ctr_type_t met_type = CTR_UINT64;
//	uint64_t met_val = 666;
	char* met = "AR_NIC_ORB_CFG_NET_RSP_HIST_1";
	ctr_type_t met_type = CTR_HEX;
	uint64_t met_val = 0x000A000500010000;
	uint64_t temp;

	gpcd_print_valid_tile_mmrs();
	gpcd_print_valid_nic_mmrs();
	gpcd_print_valid_tile_filtering_mmrs();
	gpcd_print_valid_tile_static_mmrs();


	/**
	 * options to toggle perms, increase verbosity
	 * WARNING: disable perms. Note this is really jsut a toggle. So if it is enabled, this will actually disable this.
	 */
	if (0){
		printf("Disabling Perms checking (toggle 1)\n");
		rc = gpcd_disable_perms();
		if (rc){
			printf("Could not gpcd_disable_perms. Must be root.\n");
			exit(-1);
		}
		//this is a toggle, so we don't know which way we toggled it.
	}

	/* WARNING: don't know if this needs to be set back */
	rc = gpcd_verbose_debug(1);
	if (rc){
		printf("Could not gpcd_verbose_debug. Retoggling\n");
		rc = gpcd_disable_perms();
		if (rc){
			printf("Could not gpcd_disable_perms. Must be root.\n");
			exit(-1);
		}
		rc = gpcd_verbose_debug(1);
	}
	if (rc){
		printf("Could not gpcd_verbose_debug. Continuing\n");
	}

	if (usecontext){
		ctx = gpcd_create_context();
		if (!ctx){
			printf("Could not create context\n");
			exit(-1);
		}

	  rc = addMetricToContext(ctx,met);
	  if (rc){
		  printf("Could not add metric to context\n");
		  printf("Not using context --- only by name\n");
		  usecontext = 0;
	  } else {
		  printf("Context print before initial read\n");
		  gpcd_context_print(ctx);
	  }
	}

	if (usecontext){
		//read into the context
		rc = gpcd_context_read_mmr_vals(ctx);
		if (rc != 0){
			printf("cannot read_mmr_val '%s'\n", met);
			exit(-1);
		}
		printf("Context print after read but before write\n");
		gpcd_context_print(ctx);
	}


	gpcd_mmr_desc_t* desc = (gpcd_mmr_desc_t *)
		gpcd_lookup_mmr_byname(met);
	if (!desc) {
		printf("Could not lookup (1) <%s>\n", met);
		exit(-1);
	}
	printf("%s desc->addr = 0x%lx\n", desc->name, desc->addr);


	rc = gpcd_read_mmr_val(desc, &temp, 0);
	if (rc != 0){
		printf("cannot read_mmr_val '%s'\n", met);
		exit(-1);

 	}

	if (met_type == CTR_UINT64){
		printf("Initial read_mmr_val '%s' = %" PRIu64 "\n", met, temp);
	} else {
		printf("Initial read_mmr_val '%s' = 0x%lx\n", met, temp);
	}

	if (dowrite){
		uint64_t umet_val;
		if (met_type == CTR_UINT64){
			printf("Writing val '%s' = %" PRIu64 "\n", met, met_val);
		} else {
			printf("Writing val '%s' = 0x%lx\n", met, met_val);
		}

		umet_val = met_val;
		rc = gpcd_write_mmr_val(desc, &umet_val, 0);
		if (rc){
			printf("cannot write_mmr_val '%s'. Retoggling,\n", met);
			rc = gpcd_disable_perms();
			if (rc){
				printf("Could not gpcd_disable_perms. Must be root.\n");
				exit(-1);
			}
			rc = gpcd_write_mmr_val(desc, &umet_val, 0);
		}
		if (rc) {
			printf("cannot write_mmr_val '%s'.\n", met);
			exit(-1);
		}
	} else {
		printf("NOT writing!\n");
	}


	for (count = 0; count < 2; count++){
		//read single val
		rc = gpcd_read_mmr_val(desc, &temp, 0);
		if (rc != 0){
			printf("cannot read_mmr_val '%s'\n", met);
			exit(-1);
		}


		if (met_type == CTR_UINT64){
			printf("After writing, read_mmr_val '%s' = %" PRIu64 "\n", met, temp);
		} else {
			printf("After writing, read_mmr_val '%s' = 0x%lx\n", met, temp);
		}

		//read into the context
		if (usecontext){
			rc = gpcd_context_read_mmr_vals(ctx);
			if (rc != 0){
				printf("cannot read_mmr_val '%s'\n", met);
				exit(-1);
			}
			gpcd_context_print(ctx);
		} else {
			printf("not reading into nor printing context\n");
		}
	}

	/* WARNING: don't know if this needs to be set back */
	rc = gpcd_verbose_debug(-1);


	/* WARNING: don't know what state we are leaving this in (perms) */


	return 0;

}
