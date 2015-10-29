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


#define RC_T 0
#define RAW_T 1

#define GEMINI_MAX_ROW 6
#define GEMINI_MAX_COL 8
#define ARIES_MAX_ROW 6
#define ARIES_MAX_COL 8
#define GEMINI_T 0
#define ARIES_T 1

//NOTE: that I've wired the aries type and headers into libgpcd

#define MAX_LEN 256

/**
 * parses 2 config files:
 * 1) raw names that go in as is
 * 2) r/c metrics.
 * VC's should be explictly put in separately.
 */

static int num_rcmetrics = 0;
static char** rcmetrics = NULL;
static int num_rawmetrics = 0;
static char** rawmetrics = NULL;

struct met{
	char* name;
	LIST_ENTRY(met) entry;
};

LIST_HEAD(raw_list, met) raw_list;
LIST_HEAD(rc_list, met) rc_list;

static gpcd_context_t *rc_ctx = NULL;
static gpcd_context_t *raw_ctx = NULL;
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
		fseek(mf, 0, SEEK_SET);
		count = 0;
		temp = calloc(count, sizeof(*temp));
		//parse again to populate the metrics;
		do {
			s = fgets(lbuf, sizeof(lbuf), mf);
			if (!s)
				break;
			rc = sscanf(lbuf," %s", name);
			if ((rc != 1) || (strlen(name) == 0) || (name[0] == '#')){
//				printf("Warning: skipping input <%s>\n", lbuf);
				continue;
			}
//			printf("<%s> Should add input <%s>\n", lbuf, name);
			temp[count] = strdup(name);
			count++;
		} while(s);
	}

	if (mtype == RC_T){
		num_rcmetrics = count;
		rcmetrics = temp;
	} else {
		num_rawmetrics = count;
		rawmetrics = temp;
	}

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

	for (i = 0; i < num; i++){
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
	for (k = 0; k < ibase; k++){
		for (i = 0; i < rmax; i++) {
			for (j = 0; j < cmax; j++) {
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
	char* rawfile = NULL;
	char* rcfile = NULL;
	int rawmax = 0;
	int rcmax = 0;
	int num;
	int opt;
	int rc;

	while ((opt = getopt(argc, argv, "r:o:")) != -1){
		switch(opt){
		case 'o':
			rawfile = optarg;
			rc = parseConfig(rawfile, RAW_T);
			if (rc){
				printf("Cannot parse config <%s>\n", rawfile);
				exit(-1);
			}
			break;
		case 'r':
			rcfile = optarg;
			rc = parseConfig(rcfile, RC_T);
			if (rc){
				printf("Cannot parse config <%s>\n", rcfile);
				exit(-1);
			}
			break;
		default:
			printf("Usage ./main_gpcd -r rcfile -o otherfile\n");
			exit(-1);
		}
	}

	raw_ctx = create_context_list(rawmetrics, num_rawmetrics, &rawmax);
	if (rawmax && !raw_ctx){
		printf("Cannot create context\n");
		exit (-1);
	}
	printf("Added %d raw\n", rawmax);

	rc_ctx = create_context_rc(rcmetrics, num_rcmetrics, ARIES_T, &rcmax);
	if (rcmax && !rc_ctx){
		printf("Cannot create context\n");
		exit (-1);
	}
	printf("Added %d rc\n", rcmax);

	gettimeofday(&tv1, NULL);

	//only read if we have metrics
	if (rawmax){
		int error = gpcd_context_read_mmr_vals(raw_ctx);
		if (error){
			printf("Cannot read mmr vals\n");
			exit(-1);
		}
	}

	if (rcmax){
		int error = gpcd_context_read_mmr_vals(rc_ctx);
		if (error){
			printf("Cannot read mmr vals\n");
			exit(-1);
		}
	}
	gettimeofday(&tv2, NULL);
	timersub(&tv2, &tv1, &diff);
	printf("time = %lu.%06lu\n", diff.tv_sec, diff.tv_usec);


	for (num = 0; num < 2; num++){
		struct met *np;
		int ctr;
		if (!num){
			ctr = rawmax;
			if (!ctr)
				continue;
			listp = raw_ctx->list;
			np = raw_list.lh_first;
			if (np == NULL){
				printf("No name\n");
				exit(-1);
			}
		} else {
			ctr = rcmax;
			if (!ctr)
				continue;
			listp = rc_ctx->list;
			np = rc_list.lh_first;
			if (np == NULL){
				printf("No name\n");
				exit(-1);
			}
		}
		ctr--;

		if (!listp) {
			printf("No list!\n");
			exit(-1);
		}

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
			ctr--;
		}
		printf("\n\n");
	}

	return 0;
}
