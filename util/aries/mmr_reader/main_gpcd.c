#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <linux/limits.h>
#include "gpcd_pub.h"
#include "gpcd_lib.h"


enum {RAW_T, RC_T, NIC_T, PTILE_T, END_T};

#define GEMINI_MAX_ROW 6
#define GEMINI_MAX_COL 8
#define ARIES_MAX_ROW 6
#define ARIES_MAX_COL 8
#define GEMINI_T 0
#define ARIES_T 1

//NOTE: that I've wired the aries type and headers into libgpcd

#define MAX_LEN 256

/**
 * parses 4 config files:
 * 1) raw names that go in as is
 * 2) r/c metrics.
 * 3) ptile metrics.
 * 4) nic metrics.
 * VC's should be explictly put in separately.
 */

struct mstruct{
	int num_metrics;
	char** metrics;
	int max;
	gpcd_context_t* ctx;
};

static struct mstruct mvals[END_T];


struct met{
	char* name;
	LIST_ENTRY(met) entry;
};

LIST_HEAD(raw_list, met) raw_list;
LIST_HEAD(rc_list, met) rc_list;
LIST_HEAD(nic_list, met) nic_list;
LIST_HEAD(ptile_list, met) ptile_list;


static gpcd_mmr_list_t *listp = NULL;


int parseConfig(char* fname, int mtype){
	FILE *mf;
	char *s;
	char** temp;
	char name[MAX_LEN];
	char lbuf[MAX_LEN];
	int count = 0;
	int rc;


	mf = fopen(fname, "r");
	if (!mf){
		printf("Cannot open file <%s>\n", fname);
		exit(-1);
	}

	fseek(mf, 0, SEEK_SET);
	//parse once to get the number of metrics
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf," %s", name);
		if ((rc != 1) || (strlen(name) == 0) || (name[0] == '#')){
//			printf("Warning: skipping input <%s>\n", lbuf);
			continue;
		}
//		printf("Should add input <%s>\n", name);
		count++;
	} while(s);

	if (count == 0){
		temp = NULL;
	} else {
		temp = calloc(count, sizeof(*temp));
		count = 0;
		fseek(mf, 0, SEEK_SET);
		//parse again to populate the metrics; since they will
		do {
			s = fgets(lbuf, sizeof(lbuf), mf);
			if (!s)
				break;
			rc = sscanf(lbuf," %s", name);
			if ((rc != 1) || (strlen(name) == 0) || (name[0] == '#')){
//				printf("Warning: skipping input <%s>\n", lbuf);
				continue;
			}
//			printf("<%d> <%s> Should add input <%s>\n", count, lbuf, name);
			temp[count] = strdup(name);
			count++;
		} while(s);
	}

	mvals[mtype].num_metrics = count;
	mvals[mtype].metrics = temp;

	if (mf)
		fclose(mf);

	return 0;

}




/**
 * Build linked list of tile performance counters we wish to get values for.
 * No aggregation.
 */
gpcd_context_t *create_context_list(char** met, int num, int* nmet)
{

	gpcd_context_t *lctx = NULL;
	gpcd_mmr_desc_t *desc;
	int count = 0;
	int i, status;

	lctx = gpcd_create_context();
	if (!lctx) {
	  printf("Could not create context\n");
	  return NULL;
	}

	//add them backwards, since we will read them off backwards
	for (i = num-1; i >= 0; i--){
		desc = (gpcd_mmr_desc_t *)
			gpcd_lookup_mmr_byname(met[i]);

		if (!desc) {
			printf("Could not lookup <%s>\n", met[i]);
			continue;
		}

		status = gpcd_context_add_mmr(lctx, desc);
		if (status != 0) {
			printf("Could not add mmr for <%s>\n", met[i]);
			gpcd_remove_context(lctx);
			return NULL;
		}
		struct met* e = calloc(1, sizeof(*e));
		e->name = strdup(met[i]);
		LIST_INSERT_HEAD(&raw_list, e, entry);
		count++;
	}

	*nmet = count;
	return lctx;

}

/**
 * Build linked list of tile performance counters we wish to get values for.
 * No aggregation.
 */
gpcd_context_t *create_context_np(char** met, int num, int nptype, int* nmet)
{

	gpcd_context_t *lctx = NULL;
	gpcd_mmr_desc_t *desc;
	int rangemax = -1;
	char key;
	int i, k, status;

	lctx = gpcd_create_context();
	if (!lctx) {
	  printf("Could not create context\n");
	  return NULL;
	}

	switch (nptype){
	case NIC_T:
		key = 'n';
		rangemax = 3;
		break;
	case PTILE_T:
		key = 'p';
		rangemax = 7;
		break;
	default:
		printf("Invalid type to create_context_np\n");
		return NULL;
		break;
	}

	int nvalid = 0;
	//add them backwards
	for (k = num-1; k >= 0; k--){
		char *ptr = strchr(met[k], key);
		if (!ptr){
			printf("invalid metricname: key <%c> not found in <%s>\n",
			       key, met[k]);
			continue;
		}
		for (i = rangemax; i >= 0 ; i--) {
			char* newname = strdup(met[k]);
			char* ptr = strchr(newname, key);
			if (!ptr) {
				printf("Bad!\n");
				exit (-1);
			}
			char ch[2];
			snprintf(ch, 2, "%d", i);
			ptr[0] = ch[0];

//				printf("NAME = <%s>\n", newname);

			desc = (gpcd_mmr_desc_t *)
				gpcd_lookup_mmr_byname(newname);
			if (!desc) {
				printf("\tCould not lookup <%s>\n", newname);
				free(newname);
				continue;
			}

			status = gpcd_context_add_mmr(lctx, desc);
			if (status != 0) {
				printf("Could not add mmr for <%s>\n", newname);
				gpcd_remove_context(lctx);
				return NULL;
			}

			struct met* e = calloc(1, sizeof(*e));
			e->name = newname;

			if (nptype == NIC_T)
				LIST_INSERT_HEAD(&nic_list, e, entry);
			else
				LIST_INSERT_HEAD(&ptile_list, e, entry);
			nvalid++;
		}
	}

	*nmet = nvalid;
	return lctx;

}


/**
 * Build linked list of tile performance counters we wish to get values for.
 * No aggregation.  Will add all 48 for all variables.
 */
gpcd_context_t *create_context_rc(char** basemetrics, int ibase, int ntype, int* nmet)
{
	int i, j, k, status;
	char name[MAX_LEN];
	gpcd_context_t *lctx;
	gpcd_mmr_desc_t *desc;
	int rmax;
	int cmax;

	switch (ntype){
	case GEMINI_T:
	  rmax = GEMINI_MAX_ROW;
	  cmax = GEMINI_MAX_COL;
	  break;
	case ARIES_T:
	  rmax = ARIES_MAX_ROW;
	  cmax = ARIES_MAX_COL;
	  break;
	default:
	  printf("Bad network type for gpcd: %d", ntype);
	  return NULL;
	}

	lctx = gpcd_create_context();
	if (!lctx) {
	  printf("Could not create context\n");
	  return NULL;
	}


	int nvalid = 0;
	//add them backwards
	for (k = ibase-1; k >= 0; k--){
		for (i = rmax-1 ; i >= 0; i--) {
			for (j = cmax-1; j >= 0 ; j--) {
				switch (ntype){
				case GEMINI_T:
					snprintf(name, MAX_LEN, "GM_%d_%d_TILE_%s", i, j, basemetrics[k]);
					break;
				case ARIES_T:
					snprintf(name, MAX_LEN, "AR_RTR_%d_%d_%s", i, j, basemetrics[k]);
					break;
				default:
					printf("Bad network type for gpcd: %d\n", ntype);
					return NULL;
				}
//				printf("NAME = <%s>\n", name);

				desc = (gpcd_mmr_desc_t *)
					gpcd_lookup_mmr_byname(name);
				if (!desc) {
					printf("\tCould not lookup <%s>\n", name);
					continue;
				}

				status = gpcd_context_add_mmr(lctx, desc);
				if (status != 0) {
					printf("Could not add mmr for <%s>\n", name);
					gpcd_remove_context(lctx);
					return NULL;
				}

//				printf("added <%s>\n", name);
				struct met* e = calloc(1, sizeof(*e));
				e->name = strdup(name);
				LIST_INSERT_HEAD(&rc_list, e, entry);
				nvalid++;
			}
		}
	}

	*nmet = nvalid;
	return lctx;
}

int main(int argc, char* argv[]){
	struct timeval tv1, tv2, diff;
	char* file = NULL;
	int opt;
	int rc;
	int i;

	for (i = 0; i < END_T; i++){
		mvals[i].num_metrics = 0;
		mvals[i].metrics = NULL;
		mvals[i].max = 0;
		mvals[i].ctx = NULL;
	}

	while ((opt = getopt(argc, argv, "r:o:n:p:")) != -1){
		switch(opt){
		case 'o':
			file = strdup(optarg);
			rc = parseConfig(file, RAW_T);
			if (rc){
				printf("Cannot parse config <%s>\n", file);
				exit(-1);
			}
			free(file);
			break;
		case 'r':
			file = strdup(optarg);
			rc = parseConfig(file, RC_T);
			if (rc){
				printf("Cannot parse config <%s>\n", file);
				exit(-1);
			}
			free(file);
			break;
		case 'n':
			file = strdup(optarg);
			rc = parseConfig(file, NIC_T);
			if (rc){
				printf("Cannot parse config <%s>\n", file);
				exit(-1);
			}
			break;
		case 'p':
			file = strdup(optarg);
			rc = parseConfig(file, PTILE_T);
			if (rc){
				printf("Cannot parse config <%s>\n", file);
				exit(-1);
			}
			free(file);
			break;
		default:
			printf("Usage ./main_gpcd -r rcfile -n nicfile -p ptilefile -o rawfile\n");
			exit(-1);
		}
	}

	for (i = 0; i < END_T; i++){
		switch (i){
		case RAW_T:
			mvals[i].ctx = create_context_list(mvals[i].metrics, mvals[i].num_metrics, &mvals[i].max);
			break;
		case RC_T:
			mvals[i].ctx = create_context_rc(mvals[i].metrics, mvals[i].num_metrics, ARIES_T, &mvals[i].max);
			break;
		case NIC_T:
			//fall thru
		case PTILE_T:
			mvals[i].ctx = create_context_np(mvals[i].metrics, mvals[i].num_metrics, i, &mvals[i].max);
			break;
		}

		if (mvals[i].max && !mvals[i].ctx){
			printf("Cannot create context\n");
			exit (-1);
		}
//		printf("Added %d type %d\n", mvals[i].max, i);
	}

	gettimeofday(&tv1, NULL);

	//only read if we have metrics
	for (i = 0; i < END_T; i++){
		if (mvals[i].max) {
			int error = gpcd_context_read_mmr_vals(mvals[i].ctx);
			if (error){
				printf("Cannot read mmr vals\n");
				exit(-1);
			}
		}
	}

	gettimeofday(&tv2, NULL);
	timersub(&tv2, &tv1, &diff);
	printf("time = %lu.%06lu\n", diff.tv_sec, diff.tv_usec);


	for (i = 0; i < END_T; i++){
		struct met *np;
		int ctr = mvals[i].max;
		if (!ctr)
			continue;
		listp = mvals[i].ctx->list;
		switch (i){
		case RAW_T:
			np = raw_list.lh_first;
			break;
		case RC_T:
			np = rc_list.lh_first;
			break;
		case NIC_T:
			np = nic_list.lh_first;
			break;
		case PTILE_T:
			np = ptile_list.lh_first;
			break;
		}
		if (np == NULL){
			printf("No name\n");
			exit(-1);
		}

		if (!listp) {
			printf("No list!\n");
			exit(-1);
		}

		ctr = 0;
		//NOTE: we get them in inverse order
		while (listp != NULL){
			unsigned long long val = listp->value;
			printf("%s (%d): %llu\n", np->name, ctr, val);

			if (listp->next != NULL){
				listp=listp->next;
			} else {
				break;
			}
			np = np->entry.le_next;
			if (np == NULL){
				printf("No name\n");
				exit(-1);
			}
			ctr++;
		}
		printf("\n\n");
	} // i

	return 0;
}
