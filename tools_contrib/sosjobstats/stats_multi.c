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
#include <pthread.h>
#include <math.h>

#include "sos/sos.h"
#include "mds.h"


#undef STATSTHREAD
//#define STATSTHREAD

struct analysis_data {
  uint64_t sum;
  uint64_t sumsq;
  int numrecords;
  uint64_t max;
  uint64_t min;
  sos_obj_t maxobj;
  sos_obj_t minobj;
};

struct thread_data{
  int index; //thread index = store index
  char* comp_type;
  char* metric_name;
};

//FIXME: dont make this fixed
#define MAXCOMPS 400
struct analysis_data adata[MAXCOMPS];
#ifdef STATSTHREAD
static pthread_mutex_t adatalock[MAXCOMPS];
#endif
struct analysis_data groupdata;

int compidlist[MAXCOMPS];
int numcompids;

//FIXME: dont make this fixed
#define MAXSTORES 5
struct thread_data tdata[MAXSTORES];
char root_path[MAXSTORES][128];
int numstores;


//global params
uint32_t tv_minsec = -1;
uint32_t tv_maxsec = -1;

//gnuplot
char outputbase[256];
#ifdef STATSTHREAD
static pthread_mutex_t outputlock[MAXCOMPS];
#endif
FILE** outputfp;

#ifdef STATSTHREAD
static pthread_mutex_t stdoutlock;
#endif

#define FMT "c:p:m:M:o:"

void usage(int argc, char *argv[])
{
  printf("usage: %s [OPTION]... {COMP_TYPE:METRIC}...\n"
	 "        -p <path>      - Path to files\n"
	 "        -c nodelist    - Specify nodelist (csv, no spaces)\n"
	 "        -m <mintime>   - Specify mintime\n"
	 "        -M <maxtime>   - Specify maxtime\n"
	 "        -o <outputbase(fullpath)> - Output gnuplot-able files\n",
	 argv[0]);
  exit(1);
}


int lookup_compid(uint32_t x){
  //FIXME: make btree if we get a lot of components
  int i;
  int xx = (int) x;

  for (i = 0; i < numcompids; i++){
    if (compidlist[i] == xx){
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
	  if (numcompids == (MAXCOMPS-1)){
	    printf("Too many nodes");
	    exit(-1);
	  }
	}
      } else {
	compidlist[numcompids++] = atoi(res);
	if (numcompids == (MAXCOMPS-1)){
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
}

void zerogroupdata(){
  groupdata.sum = 0;
  groupdata.sumsq = 0;
  groupdata.numrecords = 0;
  groupdata.min =  1000000000;
  groupdata.max = 0;
  groupdata.minobj =  NULL;
  groupdata.maxobj = NULL;
}


void print_gnuplot_header(FILE *fp){
  fprintf(fp, "# m/d/Y H:M:S component_id value\n");
}


void write_gnuplotdat(char* comp_type, char* metric_name){
  //write the gnuplotter file
  FILE *gnufp;
  char gnufname[256];
  snprintf(gnufname, 255, "%s_gnuplot_%s_%s.dat", outputbase, comp_type,
	   metric_name);
  gnufp = fopen(gnufname,"w");
  if (!gnufp){
    printf("Error: cannot open output file <%s> for writing\n", gnufname);
  } else {
    fprintf(gnufp,"# in gnuplot, load this file\n");
    fprintf(gnufp,"set timefmt '%%m/%%d/%%y %%H:%%M:%%S\n");
    //perhaps will want day....
    fprintf(gnufp, "set format x \"%%H:%%M:%%S\"\n");
    fprintf(gnufp, "set xdata time\n");
    int j;
    for (j = 0; j < numcompids; j++){
      char fname[256];
      snprintf(fname, 255, "%s_%s_%s_%d", outputbase, comp_type,
	       metric_name, compidlist[j]);
      fprintf(gnufp, "%s", (j == 0? "plot": "replot"));
      fprintf(gnufp, " \"%s\" using 1:4 with linespoints\n", fname);
      //add some labels...
    }
    fclose(gnufp);
  }
}

void create_outputfiles(char* comptype, char *metricname){

  if (strlen(outputbase)){
    outputfp = (FILE**)malloc(numcompids * sizeof(FILE*));
    if (!outputfp){
      printf("Error: cannot alloc for output files\n");
      exit(-1);
    }
    
    int j;
    for (j = 0; j < numcompids; j++){
      char fname[256];
      snprintf(fname, 255, "%s_%s_%s_%d", outputbase, comptype,
	       metricname, compidlist[j]);
      outputfp[j] = fopen(fname,"w");
      if (!outputfp[j]){
	printf("Error: cannot open output file <%s> for writing\n", fname);
	exit(-1);
      }
      print_gnuplot_header(outputfp[j]);
    }
  }

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

void print_record(FILE *fp, sos_t sos, sos_obj_t obj, int useusec)
{

  if (fp == NULL){
    return;
  }

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
  if (useusec){
    sprintf(tv_s, "%s.%d", t_s, tv_usec);
  } else {
    sprintf(tv_s, "%s", t_s);
  }

  SOS_OBJ_ATTR_GET(comp_id, sos, MDS_COMP_ID, obj);
  SOS_OBJ_ATTR_GET(value, sos, MDS_VALUE, obj);
  fprintf(fp, "%-24s %12d %16ld\n", tv_s, comp_id, value);
}


void calc_stats(){
  int i;

  //at this point all the adata is populated from all the stores

  zerogroupdata();

  for (i = 0; i < numcompids; i++){
    if (adata[i].numrecords == 0){
      printf("Compid: %3d - no data\n", compidlist[i]);
    } else {
      double lavg = (double)(adata[i].sum)/adata[i].numrecords;
      double lstd = sqrt((double)(adata[i].sumsq)/adata[i].numrecords - ((double)(adata[i].sum)/adata[i].numrecords)*((double)(adata[i].sum)/adata[i].numrecords));
      printf("Compid: %3d  ave: %6.4g  std: %6.4g  min: %16ld  max: %16ld \n", compidlist[i], lavg, lstd, adata[i].min, adata[i].max );

      //populate groupdata with this component's data
      groupdata.numrecords+= adata[i].numrecords;
      groupdata.sum += adata[i].sum;
      groupdata.sumsq += adata[i].sumsq;
      if (groupdata.min > adata[i].min){
	groupdata.min = adata[i].min;
      }
      if (groupdata.max < adata[i].max){
	groupdata.max = adata[i].max;
      }
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
};


void *do_one(void *t){
  sos_t sos;
  sos_iter_t iter;
  sos_iter_t tv_iter;
  struct sos_key_s tv_minkey;
  struct sos_key_s tv_maxkey;

  char* comp_type;
  char* metric_name;
  int istore;

  struct thread_data* td = (struct thread_data*) t;
  istore = td->index;
  comp_type = td->comp_type;
  metric_name = td->metric_name;

  char tmp_path[PATH_MAX];
  sprintf(tmp_path, "%s/%s/%s", root_path[istore], comp_type, metric_name);

  printf("Starting thread/loop %d on store <%s>\n",istore, tmp_path);

  sos = sos_open(tmp_path, O_RDWR);
  if (!sos) {
    printf("Could not open SOS '%s'\n", tmp_path);
#ifdef STATSTHREAD
    pthread_exit((void*) t);
#else
    return NULL;
#endif
  }

  uint32_t tv_startsec = tv_minsec;
  sos_obj_attr_key_set(sos, MDS_TV_SEC, &tv_startsec, &tv_minkey);
  sos_obj_attr_key_set(sos, MDS_TV_SEC, &tv_maxsec, &tv_maxkey);
  tv_iter = sos_iter_new(sos, MDS_TV_SEC);

  // FIXME: still need a way to find the max time in the database...
  while (!sos_iter_seek(tv_iter, &tv_minkey)){
    //increment by seconds to find the first time >= that value
    //FIXME: may want a better way to hone in on this...
    tv_startsec++;
    sos_obj_attr_key_set(sos, MDS_TV_SEC, &tv_startsec, &tv_minkey);
  }

  iter = tv_iter;
  sos_obj_t obj;
    

  //ASSUMING we have tons of times and not so many nodes so best to start the iterator at the time

  //  int counter = 0;
  for (obj = sos_iter_next(iter); obj; obj = sos_iter_next(iter)) {
    /*
     * If the user specified a key on the index
     * we need to stop when the iterator passes the key.
     */
    if (sos_obj_attr_key_cmp(sos, MDS_TV_SEC, obj, &tv_maxkey) > 0) {
      break;
    }

    //    printf("TEST thread %d iter %d\n", istore,counter++);

    //is it a compid we are interested in?
    uint32_t comp_id;
    SOS_OBJ_ATTR_GET(comp_id, sos, MDS_COMP_ID, obj);
    int index = lookup_compid(comp_id);
    if (index != -1){
      uint64_t value;
      SOS_OBJ_ATTR_GET(value, sos, MDS_VALUE, obj);

      //      printf("thread %d read data for comp %d\n", istore, comp_id);

#ifdef STATSTHREAD
      pthread_mutex_lock(&adatalock[index]);
#endif
      update_statstruct(&adata[index], value);
#ifdef STATSTHREAD
      pthread_mutex_unlock(&adatalock[index]);
#endif


      if (strlen(outputbase)){
#ifdef STATSTHREAD
	pthread_mutex_lock(&outputlock[index]);
#endif
	print_record(outputfp[index], sos, obj, 0);
#ifdef STATSTHREAD
	pthread_mutex_unlock(&outputlock[index]);
#endif
      }

	
#ifdef STATSTHREAD
      pthread_mutex_lock(&stdoutlock);
#endif
      print_record(stdout, sos, obj, 1);
#ifdef STATSTHREAD
      pthread_mutex_unlock(&stdoutlock);
#endif
	
    } //if index
  } //for iter ...

  sos_iter_free(tv_iter);
  //  sos_close(sos); //FIXME: valgrind doesnt like this
    
#ifdef STATSTHREAD
  pthread_exit((void*) t);
#else
  return NULL;
#endif

}


int main(int argc, char *argv[])
{
  char comp_type[128];
  char metric_name[128];
  int cnt;
  int op;
#ifdef STATSTHREAD
  pthread_t thread[MAXSTORES];
  pthread_attr_t attr;
  void* status;
#endif

  extern int optind;
  extern char *optarg;
  
  
  outputbase[0] = '\0';
  opterr = 0;
  while ((op = getopt(argc, argv, FMT)) != -1) {
    switch (op) {
    case 'p':
      {
	//comma separated list of stores
	char* temparg = strdup(optarg);
	char *pch;
	numstores = 0;
	pch = strtok(temparg, " ,");
	while (pch != NULL){
	  snprintf(root_path[numstores++], 127, "%s", pch);
	  pch = strtok(NULL, " ,");
	}
	if (numstores >= MAXSTORES){
	  usage(argc, argv);
	}
	free(temparg);
      }
      break;
    case 'c':
      parse_compidlist(optarg); //sets numcompids
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
    case 'o':
      {
	snprintf(outputbase,127,"%s", optarg);
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

#ifdef STATSTHREAD
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
#endif
	
  for (op = optind; op < argc; op++) {
    zerodata();
    
    cnt = sscanf(argv[op], "%128[^:]:%128s", comp_type, metric_name);
    if (cnt != 2)
      usage(argc, argv);

    printf("\n\n======================================================\n");
    printf("COMP_TYPE: %s METRIC_NAME: %s\n",
	   comp_type, metric_name);
    printf("======================================================\n");
    printf("%-24s %-12s %-16s\n", "Timestamp", "Component", "Value");
    printf("------------------------ ------------ ----------------\n");

    //FIXME: will need to revisit the writing of the output files, since if the
    //data for a given node is mixed in different stores, then the data may come in out of order
    if (strlen(outputbase)){
      create_outputfiles(comp_type, metric_name);
    }

    int i;
    for (i = 0; i < numstores; i++){
      tdata[i].index = i;
      tdata[i].comp_type = strdup(comp_type);
      tdata[i].metric_name = strdup(metric_name);
#ifdef STATSTHREAD
      int rc = pthread_create(&thread[i], &attr, do_one, (void *)&tdata[i]); 
      if (rc) {
	printf("ERROR; return code from pthread_create() is %d\n", rc);
	exit(-1);
      }
#else
      do_one((void *)&tdata[i]);
#endif
    }

#ifdef STATSTHREAD
    /* Free attribute and wait for the other threads */
    pthread_attr_destroy(&attr);
    for(i=0; i<numstores; i++) {
      int rc = pthread_join(thread[i], &status);
      if (rc) {
	printf("ERROR; return code from pthread_join() is %d\n", rc);
	exit(-1);
      }
      printf("Main: completed join with thread %ld having a status of %ld\n",i,(long)status);
    }
#endif

    for (i = 0; i < numstores; i++){
      free(tdata[i].comp_type);
      free(tdata[i].metric_name);
    }

    printf("------------------------ ------------ ----------------\n");
    printf("------------------------ ------------ ----------------\n");

    calc_stats();

    int j;
    if (strlen(outputbase)){
      write_gnuplotdat(comp_type, metric_name);
      for (j = 0; j < numcompids; j++){
	if (outputfp[j]) fclose(outputfp[j]);
	outputfp[j] = 0;
      }
      free(outputfp);
      outputfp = 0;
    }

  } //for opt

#ifdef STATSTHREAD
  pthread_exit(NULL);
#endif

  return 0;
} //main
