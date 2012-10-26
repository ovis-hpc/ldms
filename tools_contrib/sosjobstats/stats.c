#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#include <time.h>
#include <endian.h>
#include <math.h>

#include "sos/sos.h"
#include "mds.h"

char *root_path;
char tmp_path[PATH_MAX];

#define FMT "c:p:m:M:"
int unique;
void usage(int argc, char *argv[])
{
	printf("usage: %s [OPTION]... {COMP_TYPE:METRIC}...\n"
	       "        -p <path>      - Path to files\n"
	       "        -c nodelist    - Specify nodelist (csv, no spaces)\n"
	       "        -m <mintime>   - Specify mintime\n"
	       "        -M <maxtime>   - Specify maxtime\n",
	       argv[0]);
	exit(1);
}

struct analysis_data {
  uint64_t sum;
  uint64_t sumsq;
  int numrecords;
  uint64_t max;
  uint64_t min;
  sos_obj_t maxobj;
  sos_obj_t minobj;
};

#define MAXCOMPS 10
struct analysis_data adata[MAXCOMPS];
struct analysis_data groupdata;
int compidlist[MAXCOMPS];
int numcompids;

int lookup_compid(uint32_t x){
  int i;
  for (i = 0; i < numcompids; i++){
    if (compidlist[i] == x){
      return i;
    }
  }

  return -1;
}

void parse_compidlist(char* str){
  //format: [1-5,7,10] (no spaces)

  char *copy = strdup(str);
  char *delim = "-,";
  int currval = -1;
  char *res = strtok( str, delim );
  while (res) {
    if (copy[res-str+strlen(res)] == '-'){
      currval = atoi(res);
    } else {
      if (currval != -1){
	int i;
	for (i = currval; i <= atoi(res); i++){
	  compidlist[numcompids++] = i;
	  if (numcompids == 9){
	    printf("Too many nodes");
	    exit(-1);
	  }
	}
      } else {
	compidlist[numcompids++] = atoi(res);
	if (numcompids == 9){
	  printf("Too many nodes");
	  exit(-1);
	}
      }
      currval = -1;
    }
    res = strtok( NULL, delim );
  }
  free(copy);
}

void parse_time(char* str, uint32_t* tv_sec, uint32_t* tv_usec){
  //input format: 12/31/69 16:00:01.0
  //WARNING -- this does not handle any time zone issues

  //  printf("testing: parse_time <%s> -->",str);

  //split on the dot
  char* pch;
  pch = strtok(str, ".");
  if (pch != NULL){
    char* dtime = pch;

    FILE *fpipe;
    char cmd[256];
    snprintf(cmd, 255, "date +%%s -d \'%s\'", dtime);
    char line[256];
    if (!(fpipe = (FILE*)popen(cmd,"r"))){
      printf("Problems with pipe");
      exit(-1);
    }
    
    if (fgets(line, sizeof line, fpipe)){
      *tv_sec = atoi(line);
    } else {
      printf("Cant get time");
      exit(-1);
    }
    pclose(fpipe);
  }
  pch = strtok( NULL, ".");
  if (pch != NULL){
    *tv_usec = atoi(pch);
  } else {
    *tv_usec = 0;
  }

  //  printf("sec = <%12ld> usec = <%12ld>\n", *tv_sec, *tv_usec);

}

void zerodata(){
  int j;
  for (j = 0; j < MAXCOMPS; j++){
    adata[j].sum = 0;
    adata[j].sumsq = 0;
    adata[j].numrecords = 0;
    adata[j].min = 1000000000;
    adata[j].max = 0;
    adata[j].minobj = NULL;
    adata[j].maxobj = NULL;
  }

  groupdata.sum = 0;
  groupdata.sumsq = 0;
  groupdata.numrecords = 0;
  groupdata.min =  1000000000;
  groupdata.max = 0;
  groupdata.minobj =  NULL;
  groupdata.maxobj = NULL;

}

void update_statstruct(struct analysis_data *data, uint64_t value){
  //FIXME -- change this to pass in the obj and retain a ptr to it
  //so we can have all info on the min/max

  data->numrecords++;
  data->sum += value;
  data->sumsq += value*value;
  if (value < data->min)
    data->min = value;
  if (value > data->max)
    data->max = value;
}


void print_record(FILE *fp, sos_t sos, sos_obj_t obj)
{
	uint32_t tv_sec;
	uint32_t tv_usec;
	char t_s[128];
	char tv_s[128];
	struct tm *tm_p;
	time_t t;
	uint32_t comp_id;
	uint64_t value;

	SOS_OBJ_ATTR_GET(tv_sec, sos, MDS_TV_SEC, obj);
	SOS_OBJ_ATTR_GET(tv_usec, sos, MDS_TV_USEC, obj);

	/* Format the time as a string */
	t = tv_sec;
	tm_p = localtime(&t);
	strftime(t_s, sizeof(t_s), "%D %T", tm_p);
	sprintf(tv_s, "%s.%d", t_s, tv_usec);

	SOS_OBJ_ATTR_GET(comp_id, sos, MDS_COMP_ID, obj);
	SOS_OBJ_ATTR_GET(value, sos, MDS_VALUE, obj);
	fprintf(fp, "%-24s %12d %16ld\n", tv_s, comp_id, value);
}


int main(int argc, char *argv[])
{
	char comp_type[128];
	char metric_name[128];
	int cnt;
	int op;
	uint32_t tv_minsec = -1;
	uint32_t tv_maxsec = -1;
	extern int optind;
	extern char *optarg;
	struct sos_key_s tv_minkey;
	struct sos_key_s tv_maxkey;
	//	struct sos_key_s comp_key;

	opterr = 0;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'p':
			root_path = strdup(optarg);
			break;
		case 'c':
		  parse_compidlist(optarg);
		  break;
		case 'm':
		  {
		    //we would like to take these in more readable formats
		    //			tv_minsec = strtol(optarg, NULL, 0);
		    uint32_t junk;
		    parse_time(optarg, &tv_minsec, &junk);
		  }
		  break;
		case 'M':
		  {
		  //we would like to take these in more readable formats
		  //			tv_maxsec = strtol(optarg, NULL, 0);
		    uint32_t junk;
		    parse_time(optarg, &tv_maxsec, &junk);
		  }
		  break;
		case '?':
		default:
			usage(argc, argv);
		}
	}
	if (optind >= argc)
		usage(argc, argv);

	if (tv_minsec == -1 || tv_maxsec == -1 || numcompids == 0){
	  usage(argc, argv);
	}
	
	for (op = optind; op < argc; op++) {
		sos_t sos;
		sos_iter_t iter;
		sos_iter_t tv_iter;
		//		sos_iter_t comp_iter;

		zerodata();

		cnt = sscanf(argv[op], "%128[^:]:%128s",
			     comp_type, metric_name);
		if (cnt != 2)
			usage(argc, argv);

		printf("COMP_TYPE: %s METRIC_NAME: %s\n\n",
		       comp_type, metric_name);
		printf("%-24s %-12s %-16s\n", "Timestamp", "Component", "Value");
		printf("------------------------ ------------ ----------------\n");
		if (root_path)
			sprintf(tmp_path, "%s/%s/%s", root_path, comp_type, metric_name);
		else
			sprintf(tmp_path, "%s/%s", comp_type, metric_name);

		sos = sos_open(tmp_path, O_RDWR);
		if (!sos) {
			printf("Could not open SOS '%s'\n", tmp_path);
			continue;
		}

		sos_obj_attr_key_set(sos, MDS_TV_SEC, &tv_minsec, &tv_minkey);
		sos_obj_attr_key_set(sos, MDS_TV_SEC, &tv_maxsec, &tv_maxkey);

		tv_iter = sos_iter_new(sos, MDS_TV_SEC);
		
		
		if (!sos_iter_seek(tv_iter, &tv_minkey))
		  goto out;
		iter = tv_iter;

		sos_obj_t obj;
		//ASSUMING we have tons of times and not so many nodes so best to start the iterator at the time
		for (obj = sos_iter_next(iter); obj; obj = sos_iter_next(iter)) {
		  /*
		   * If the user specified a key on the index
		   * we need to stop when the iterator passes the key.
		   */
		  if (sos_obj_attr_key_cmp(sos, MDS_TV_SEC, obj, &tv_maxkey) > 0) {
		    //		    printf("found maxkey -- breaking\n");
		    break;
		  }

		  //is it a compid we are interested in?
		  uint32_t comp_id;
		  SOS_OBJ_ATTR_GET(comp_id, sos, MDS_COMP_ID, obj);
		  int index = lookup_compid(comp_id);
		  if (index != -1){
		    uint64_t value;
		    SOS_OBJ_ATTR_GET(value, sos, MDS_VALUE, obj);
		    update_statstruct(&adata[index], value);
		    update_statstruct(&groupdata, value);

		    print_record(stdout, sos, obj);
		  } //if index
		} //for iter ...
	out:
		printf("------------------------ ------------ ----------------\n");
		printf("------------------------ ------------ ----------------\n");

		int i;
		for (i = 0; i < numcompids; i++){
		  if (adata[i].numrecords == 0){
		    printf("Compid: %3d - no data\n", compidlist[i]);
		  } else {
		    double lavg = (double)(adata[i].sum)/adata[i].numrecords;
		    double lstd = sqrt((double)(adata[i].sumsq)/adata[i].numrecords - ((double)(adata[i].sum)/adata[i].numrecords)*((double)(adata[i].sum)/adata[i].numrecords));
		    printf("Compid: %3d  ave: %6.4g  std: %6.4g  min: %16ld  max: %16ld \n", compidlist[i], lavg, lstd, adata[i].min, adata[i].max );
		  }
		}
		  
		if (groupdata.numrecords == 0){
		  printf("Group: -- no data\n");
		} else {
		  double lavg = (double)(groupdata.sum)/groupdata.numrecords;
		  double lstd = sqrt((double)(groupdata.sumsq)/groupdata.numrecords - ((double)(groupdata.sum)/groupdata.numrecords)*((double)(groupdata.sum)/groupdata.numrecords));
		  printf("------------------------ ------------ ----------------\n");
		  printf("Group:       ave: %6.4g  std: %6.4g  min: %16ld  max: %16ld \n", 
			 lavg, lstd, groupdata.min, groupdata.max );
		}
	} //for


	return 0;
}

