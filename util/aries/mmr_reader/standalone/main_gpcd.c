#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <inttypes.h>
#include <unistd.h>
#include <linux/limits.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/queue.h>
#include "gpcd_pub.h"
#include "gpcd_lib.h"

//r,c,p,n,v are the only recognized vals. All other variables have to be put in explictly
//NOTE: will have to make a change for handling two char variables (na/nb and pa/pb).


#define USE_EXPANSION

#define MAX_LEN 256

#ifdef USE_EXPANSION
//char index
enum {ROW_MT, COL_MT, PTILE_MT, NIC_MT, VC_MT, END_MT};

typedef struct { // for the XXX_MT
	char key_chr;
	int have_chr;
	int min;
	int max;
	int curr;
	int th_max;
} aries_mmrtype_t;

static aries_mmrtype_t AMT[END_MT] = {
	{'r', 0, 0, 0, 0, 5},
	{'c', 0, 0, 0, 0, 7},
	{'p', 0, 0, 0, 0, 7},
	{'n', 0, 0, 0, 0, 3},
	{'v', 0, 0, 0, 0, 7},
};

#endif

//list types
enum {REQ_T, RC_T, PTILE_T, NIC_T, OTHER_T, END_T};
//matching string types
enum {REQ_LMT, RSP_LMT, RTR_LMT, PTILE_LMT, NIC_LMT, END_LMT};

struct listmatch_t{
	char* header;
	int len;
};

static struct listmatch_t LMT[END_LMT] = {
	{"AR_NL_PRF_REQ_", 14},
	{"AR_NL_PRF_RSP_", 14},
	{"AR_RTR_", 7},
	{"AR_NL_PRF_PTILE_", 16},
	{"AR_NIC_", 7}
};

struct met{ //for the XXX_T
	char* name;
	LIST_ENTRY(met) entry;
};

struct mstruct{ //for the XXX_T
	int num_metrics;
	gpcd_context_t* ctx;
};

static struct mstruct mvals[END_T];


LIST_HEAD(tmp_list, met) tmp_list;
LIST_HEAD(req_list, met) req_list;
LIST_HEAD(rc_list, met) rc_list;
LIST_HEAD(nic_list, met) nic_list;
LIST_HEAD(ptile_list, met) ptile_list;
LIST_HEAD(other_list, met) other_list;


static gpcd_mmr_list_t *listp = NULL;

#ifdef USE_EXPANSION
int replace(char** tmp, char c, int i){
	char* ptr = strchr(*tmp, c);
	if (ptr != NULL){
		char ch[2];
		snprintf(ch, 2, "%d", i);
		ptr[0] = ch[0]; //NOTE: can do this because we know min/max will be at most 1 digit.
		replace(&ptr, c, i);
	}

	return 0;
}
#endif


/**
 * Build linked list of tile performance counters we wish to get values for.
 * No aggregation.
 */
int addMetricToContext(gpcd_context_t* lctx, char* met)
{
	gpcd_mmr_desc_t *desc;
	int status;


	if (lctx == NULL){
		printf("NULL context\n");
		return -1;
	}

	desc = (gpcd_mmr_desc_t *)
		gpcd_lookup_mmr_byname(met);

	if (!desc) {
		printf("Could not lookup <%s>\n", met);
		return -1;
	}

	status = gpcd_context_add_mmr(lctx, desc);
	if (status != 0) {
		printf("Could not add mmr for <%s>\n", met);
		gpcd_remove_context(lctx);
		return -1;
	}

	return 0;

}



int addMetric(char* tmpname){

	int i;
	int rc;

//	printf("adding: <%s>\n", tmpname);

	struct met* e = calloc(1, sizeof(*e));
	e->name = strdup(tmpname);

	if (strncmp(LMT[NIC_LMT].header, e->name, LMT[NIC_LMT].len) == 0){
		rc = addMetricToContext(mvals[NIC_T].ctx,e->name);
		if (!rc) {
			LIST_INSERT_HEAD(&nic_list, e, entry);
			mvals[NIC_T].num_metrics++;
		}
		return rc;
	}

	if ((strncmp(LMT[REQ_LMT].header, e->name, LMT[REQ_LMT].len) == 0) ||
	    (strncmp(LMT[RSP_LMT].header, e->name, LMT[RSP_LMT].len) == 0)){
		rc = addMetricToContext(mvals[REQ_T].ctx,e->name);
		if (!rc) {
			LIST_INSERT_HEAD(&req_list, e, entry);
			mvals[REQ_T].num_metrics++;
		}
		return rc;
	}

	if (strncmp(LMT[PTILE_LMT].header, e->name, LMT[PTILE_LMT].len) == 0){
		rc = addMetricToContext(mvals[PTILE_T].ctx,e->name);
		if (!rc) {
			LIST_INSERT_HEAD(&ptile_list, e, entry);
			mvals[PTILE_T].num_metrics++;
		}
		return rc;
	}

	if (strncmp(LMT[RTR_LMT].header, e->name, LMT[RTR_LMT].len) == 0){
		rc = addMetricToContext(mvals[RC_T].ctx,e->name);
		if (!rc) {
			LIST_INSERT_HEAD(&rc_list, e, entry);
			mvals[RC_T].num_metrics++;
		}
		return rc;
	}

	//put it in the other list.....
	rc = addMetricToContext(mvals[OTHER_T].ctx, e->name);
	if (!rc){
		LIST_INSERT_HEAD(&other_list, e, entry);
		mvals[OTHER_T].num_metrics++;
	}

	return rc;

}

#ifdef USE_EXPANSION
int addMetricExpansion(const char* name){
	//expand any chars

	int r, c, p, n, v;
	int i, rc;

	if (name == NULL){
		printf("Trying to add NULL. Why?\n");
		exit(-1);
	}

//	printf("Expanding <%s>\n", name);

	for (i = 0 ; i < END_MT; i++){
		char* ptr = strchr(name, AMT[i].key_chr);
		if (ptr != NULL) {
			AMT[i].max = AMT[i].th_max;
			AMT[i].have_chr = 1;
		} else {
			AMT[i].max = 0;
			AMT[i].have_chr = 0;
		}
	}


	//if have row, have to have col as well
	if (AMT[ROW_MT].have_chr != AMT[COL_MT].have_chr){
		printf("If have row, need to have col and vice versa <%s>\n", name);
		return -1;
	}

	char* tmpname = NULL;

	//replace the vars and add the metrics. Note we want reverse order
	for (r = AMT[ROW_MT].max; r >= AMT[ROW_MT].min; r--){
		AMT[ROW_MT].curr = r;
		for (c = AMT[COL_MT].max; c >= AMT[COL_MT].min; c--){
			AMT[COL_MT].curr = c;
			for (p = AMT[PTILE_MT].max; p >= AMT[PTILE_MT].min; p--){
				AMT[PTILE_MT].curr = p;
				for (n = AMT[NIC_MT].max; n >= AMT[NIC_MT].min; n--){
					AMT[NIC_MT].curr = n;
					for (v = AMT[VC_MT].max; v >= AMT[VC_MT].min; v--){ //may be more than one of these....
						tmpname = strdup(name);
						for (i = 0 ; i < END_MT; i++){
							if (AMT[i].have_chr)
								replace(&tmpname, AMT[i].key_chr, AMT[i].curr);
						}
						rc = addMetric(tmpname);
						if (rc != 0)
							printf("Warning: could not add <%s>\n", tmpname);
						free(tmpname);
					} //VC_MT
				} //NIC_MT
			} //PTILE_MT
		} //COL_MT
	} //ROW_MT

	return 0;
}
#endif

int parseConfig(char* fname){
	FILE *mf;
	char *s;
	char name[MAX_LEN];
	char lbuf[MAX_LEN];
	int rc;

	mf = fopen(fname, "r");
	if (!mf){
		printf("Cannot open file <%s>\n", fname);
		exit(-1);
	}

	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf," %s", name);
		if ((rc != 1) || (strlen(name) == 0) || (name[0] == '#')){
//			printf("Warning: skipping input <%s>\n", lbuf);
			continue;
		}
		//add them backwards
		struct met* e = calloc(1, sizeof(*e));
		e->name = strdup(name);
		LIST_INSERT_HEAD(&tmp_list, e, entry);
//		printf("Added input <%s> to tmp_list\n", name);
	} while(s);

	if (mf)
		fclose(mf);

	return 0;
}



void term() {
	int i;

	for (i = 0; i < END_T; i++){
		struct met *np;
		switch(i){
		case RC_T:
			np = rc_list.lh_first;
			break;
		case NIC_T:
			np = nic_list.lh_first;
			break;
		case PTILE_T:
			np = ptile_list.lh_first;
			break;
		default:
			np = req_list.lh_first;
		}
		while (np != NULL) {
			struct met *tp = np->entry.le_next;
			free(np->name);
			LIST_REMOVE(np, entry);
			free(np);
			np = tp;
		}
	}

}

int addMetrics(){
	//now have the basic names in reverse order, feed them that way to the expansion
	//and throw them away as we go along
	int rc;

	struct met* np = tmp_list.lh_first;
	while (np != NULL){
		struct met *tp = np->entry.le_next;
#ifdef USE_EXPANSION
		rc = addMetricExpansion(np->name);
		if (rc)
			printf("Warning: Bad add metric expansion <%s>. Skipping\n", np->name);
#else
		rc = addMetric(np->name);
		if (rc)
			printf("Warning: Bad add metric <%s>. Skipping\n", np->name);
#endif
		free(np->name);
		free(np);
		np = tp;
	}

	return 0;
}


int main(int argc, char* argv[]){
	struct timeval tv1, tv2, diff;
	char* file = NULL;
	int opt;
	int rc;
	int i;

	for (i = 0; i < END_T; i++){
		mvals[i].num_metrics = 0;
		mvals[i].ctx = gpcd_create_context();
		if (!mvals[i].ctx) {
			printf("Could not create context\n");
			return -1;
		}
	}

	while ((opt = getopt(argc, argv, "f:")) != -1){
		switch(opt){
		case 'f':
			file = strdup(optarg);
			rc = parseConfig(file);
			if (rc){
				printf("Cannot parse config <%s>\n", file);
				exit(-1);
			}
			free(file);
			break;
		default:
			printf("Usage ./main_gpcd -f file\n");
			exit(-1);
		}
	}


	rc = addMetrics();
	if (rc){
		printf("Problem adding Metrics\n");
		exit(-1);
	}

	gettimeofday(&tv1, NULL);

	//only read if we have metrics
	for (i = 0; i < END_T; i++){
		if (mvals[i].num_metrics) {
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
		int ctr = mvals[i].num_metrics;
		if (!ctr)
			continue;
		listp = mvals[i].ctx->list;
		switch (i){
		case REQ_T:
			np = req_list.lh_first;
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
		//NOTE: we read them off the ctx in the reverse order that they were added
		//we also added to the head of the list so the orders should match
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

	term();

	return 0;
}
