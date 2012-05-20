/*
 * This is the junk data provider
 */
#include <glib.h>
#include <inttypes.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include "ldms.h"
#include "ldmsd.h"

#define MAXMETRICSPERSET 100

struct fset {
  ldms_set_t sd;
  ldms_metric_t metrichandles[MAXMETRICSPERSET]; //FIXME: make this not fixed 
};
static char sedcheaders[MAXMETRICSPERSET][LDMS_MAX_CONFIG_STR_LEN]; //FIMXE: make this not fixed
static int metric_count = 0; 
static int numhosts = 0;
GHashTable* compidmap;
GHashTable* setmap;

char datafile[LDMS_MAX_CONFIG_STR_LEN];
char setshortname[LDMS_MAX_CONFIG_STR_LEN];
char filetype[LDMS_MAX_CONFIG_STR_LEN];

FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
int minindex = 2; //the min index in the header file

static pthread_mutex_t cfg_lock;
static size_t tot_meta_sz = 0;
static size_t tot_data_sz = 0;


static void printCompIdMap(gpointer key, gpointer value, gpointer user_data){
  FILE* outfile = fopen("/home/brandt/ldms/outfile", "a");
  fprintf(outfile, "<%s> <%d>", (char*)key, *(int*)value);
  fflush(outfile);
  fclose(outfile);
}

static int processCompIdMap(char * fname){
 //FIXME: can we have a function for this (note have to handle L0, node, though not the full
  //set of options since remote assoc will be handled at insert)? can this be a type and
  //offset or something??


  FILE* outfile = fopen("/home/brandt/ldms/outfile", "a");
  fprintf(outfile, "entered process compid map <%s>", fname);
  fflush(outfile);
  fclose(outfile);

  compidmap = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

  FILE *cid = fopen(fname, "r");
  if (!cid) {
    msglog("Could not open the junk file '%s'...exiting\n", fname);
    return ENOENT;
  }

  outfile = fopen("/home/brandt/ldms/outfile", "a");
  fprintf(outfile, "should be looking at file <%s>", fname);
  fflush(outfile);
  fclose(outfile);


  char lbuf[1024];
  while(fgets(lbuf, sizeof(lbuf), cid) != NULL){
    char* compname = (char*) g_malloc(20*sizeof(char));
    int* val = (int*) g_malloc(sizeof(int));
    int rc = sscanf(lbuf,"%s %d\n",compname,val);
    if (rc == 2){
      g_hash_table_replace(compidmap, (gpointer)compname, (gpointer)val);
      FILE* outfile = fopen("/home/brandt/ldms/outfile", "a");
      fprintf(outfile, "<%s> <%d>", compname, *val);
      fflush(outfile);
      fclose(outfile);
      numhosts++;
    } else {
      FILE* outfile = fopen("/home/brandt/ldms/outfile", "a");
      fprintf(outfile, "cant add <%s>\n", lbuf);
      fflush(outfile);
      fclose(outfile);
    }
  }

  fclose(cid);
  g_hash_table_foreach(compidmap, printCompIdMap, NULL);
  return 0;
}

static int processSEDCHeader(char* lbuf){
  //header will look just like that on the SEDC file( 2 extra non-metric fields)
  //split the line into tokens based on comma set
  size_t meta_sz, data_sz;

  /*
   * Process the header file to determine the metric set size.
   */

  FILE* outfile = fopen("/home/brandt/ldms/outfile", "a");
  fprintf(outfile, "%s", "determining the metric set size\n");
  fflush(outfile);
  fclose(outfile);

  tot_meta_sz = 0;
  tot_data_sz = 0;
  int rc = ldms_get_metric_size("component_id", LDMS_V_U64, &tot_meta_sz, &tot_data_sz);
  if (rc){
    return rc;
  }
  metric_count = 0;
  int count = 0;
  
  char *pch = strtok(lbuf, ",\n");
  //	  outfile = fopen("/home/brandt/ldms/outfile", "a");
  //	  fprintf(outfile, "read <%s>\n", lbuf);
  //	  fflush(outfile);
  //	  fclose(outfile);
  while (pch != NULL){
    if (count >= minindex){
      outfile = fopen("/home/brandt/ldms/outfile", "a");
      fprintf(outfile, "counting metric <%s>\n", pch);
      fflush(outfile);
      fclose(outfile);
	
      rc = ldms_get_metric_size(pch, LDMS_V_U64, &meta_sz, &data_sz);
      if (rc)
	return rc;
	
      tot_meta_sz += meta_sz;
      tot_data_sz += data_sz;
      snprintf(sedcheaders[metric_count],LDMS_MAX_CONFIG_STR_LEN, "%s", pch);
      metric_count++;
    } else {
      outfile = fopen("/home/brandt/ldms/outfile", "a");
      fprintf(outfile, "NOT counting metric <%s>\n", pch);
      fflush(outfile);
      fclose(outfile);
    }
    count++;
    pch = strtok(NULL,",\n");
  }

  return 0;
};

static int config(char *str)
{
  enum {
    HEADERFILE,
    COMPIDMAP,
    DATAFILE,
  } action;

  int rc = 0;

  pthread_mutex_lock(&cfg_lock);
  if (0 == strncmp(str, "datafile", 8)){
    action = DATAFILE;
  } else if (0 == strncmp(str, "headerfile", 10)){
    action = HEADERFILE;
  } else if (0 == strncmp(str, "compidmap", 9)){
    FILE *outfile;
	outfile = fopen("/home/brandt/ldms/outfile", "w");
	fprintf(outfile, "action should be compidmap\n");
	fflush(outfile);
	fclose(outfile);
    action = COMPIDMAP;
  } else {
    msglog("junk: Invalid configuration string '%s'\n", str);
    rc = EINVAL;
    return rc;
  }

  switch (action) {
  case DATAFILE:
    {
      rc = sscanf(str, "datafile=%[^&]&%s", datafile, filetype);
      if (rc != 2){
	rc = EINVAL;
      } else {
	if ((strcmp(filetype, "sedc") != 0) && (strcmp(filetype, "rsyslog"))){
	  rc = EINVAL;    
	}
      }
      break;
    }
  case HEADERFILE:
    {
      char junk[LDMS_MAX_CONFIG_STR_LEN];
      char lbuf[10240]; //how big does this have to be? 
      sscanf(str, "headerfile=%s", junk);
      mf = fopen(junk, "r");
      if (!mf) {
	msglog("Could not open the junk file '%s'...exiting\n", junk);
	return ENOENT;
      }

      fseek(mf, 0, SEEK_SET);
      if (fgets(lbuf, sizeof(lbuf), mf) != NULL){
	rc = processSEDCHeader(lbuf);
      }
      if (mf) fclose(mf);

      break;
    }
  case COMPIDMAP:
    {
    FILE *outfile;
	outfile = fopen("/home/brandt/ldms/outfile", "w");
	fprintf(outfile, "action should be compidmap (2)\n");
	fflush(outfile);
	fclose(outfile);

      char junk[LDMS_MAX_CONFIG_STR_LEN];
      sscanf(str, "compidmap=%s", junk);

	outfile = fopen("/home/brandt/ldms/outfile", "w");
	fprintf(outfile, "calling compid map\n");
	fflush(outfile);
	fclose(outfile);

      processCompIdMap(junk);
      break;
    }
  default:
    msglog("Invalid config statement '%s'\n", str);
    rc = EINVAL;
  }

  pthread_mutex_unlock(&cfg_lock);
  return rc;

}

static ldms_set_t get_set()
{
  //FIXME: can something work if there are multiple sets?
  //  return set;
  return NULL;
}

static int init(const char *path)
{
  //  FILE *outfile;
  //	outfile = fopen("/home/brandt/ldms/outfile", "w");
  //	fprintf(outfile, "entered init (junk)\n");
  //	fflush(outfile);
  //	fclose(outfile);

  snprintf(setshortname, LDMS_MAX_CONFIG_STR_LEN, "%s", path);
  setmap = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  return 0;
}

int createMetricSet(char* hostname, int compid, char* shortname){
  //create the metric set for a new compid
  char setnamechar[1024];
  int i;

  //FIXME: setname will be <hostname>/<setshortname>
  //can we get nid if we want that?
  snprintf(setnamechar,1024,"%s/%s",hostname,setshortname);

  struct fset *currfset = (struct fset*)g_malloc(sizeof(struct fset));

  /* Create a metric set of the required size */
  int rc = ldms_create_set(setnamechar, tot_meta_sz, tot_data_sz, &(currfset->sd));
  if (rc != 0){
    printf("Error %d creating metric set '%s'.\n", rc, setnamechar);
    exit(1);
  } else {
    printf("Created set <%s>\n", setnamechar);
  }
	   
  ldms_metric_t currmetric =  ldms_add_metric(currfset->sd, "component_id", LDMS_V_U64);
  if (currmetric == 0){
    printf("Error creating the metric %s.\n", "component_id");
    exit(1);
  } else {
    printf("Created metric component_id\n");
  }	
  currfset->metrichandles[0] = currmetric;

  for (i = 0; i < metric_count; i++){
    ldms_metric_t currmetric = ldms_add_metric(currfset->sd, sedcheaders[i], LDMS_V_U64);
    if (currmetric == 0){
      printf("Error creating the metric %s.\n", sedcheaders[i]);
      exit(1);
    } else {
      printf("Created metric <%s>\n",sedcheaders[i]);
    }
    currfset->metrichandles[i+1] = currmetric;
    printf("added a metric handle to the vector\n");
  }

  //fill in the comp id for ldms (for file it is filled in elsewhere)
  printf("fill in the compid value\n");
  union ldms_value v;
  v.v_u64 = compid;
  ldms_set_metric(currfset->metrichandles[0], &v);
  g_hash_table_replace(setmap, (gpointer)compid, (gpointer)currfset);

  printf("returing from create metric set\n");

  return 0;
};


static char* stripRsyslogHeaders(char* bufin){
 // Example output:
  //  "<"<syslog priority>">1" <timestamp> <hostname> <application> <pid> <bootsessionid> "["<msg_type>"@34]" <message>
  //the msg type will be FILESOURCE  (not FILESOURCE::METRICNAME). message vals will be the csv
  //the timestamp will not be used

  int imessage = 7;
  char* bufptr;
  int count = 0;

  char* p = strchr(bufin, ' ');
  while (p != NULL){
    if (count == imessage){
      return p;
    }
    bufptr = p+1;
    p = strchr(bufptr, ' ');
    count++;
  }

  return NULL;
};

/*
int processSEDCData(char* line){
  COMPONENT_ID:
    {
      char junk[128];
      int rc;
      union ldms_value v;
      rc = sscanf(str, "component_id %" PRIu64 "%s\n", &v.v_u64, junk);
      if (rc < 1) {
	pthread_mutex_unlock(&cfg_lock);
	return EINVAL;    
      }
      ldms_set_metric(compid_metric_handle, &v);  
      break;
    }
}


====================================


  //split the line into tokens based on comma sep
  std::cout << "In ProcessSEDCData <" << line << ">\n";

  std::stringstream ss;
  ss << line;

  int compid = -1;
  fset* currfset;
  int valid = 0;
  int count = 0;
  int countprev = minheaderidx-1;
  std::string val;
  std::string hostname;

  std::ofstream outfile;

  while (getline(ss,val,',')){
    if ((val.size() > 0) && (count < (int) sedcheaders.size())){
      //check for blank line
      if (val == "\n"){
	break;
      }
      //skip if its a header
      if (val == "service id"){
	break;
      }
      valid = 1;
      switch (count){
      case 0: //compname
	{
	  std::string hostname = val;
	  std::map<std::string,int>::iterator it = compidmap.find(val); 
	  if (it == compidmap.end()){
	    printf("Error: cannot find compname to id assoc %s\n", val.c_str());
	    cleanup(1);
	    //	  return -1;
	  }
	  compid = it->second;

	  std::map<int, fset>::iterator iter;
	  iter = setmap.find(compid);
	  if (iter == setmap.end()){
	    int rc = createMetricSet(hostname,compid,setshortname);
	    if (rc != 0 ){
	      printf("Error: cannot create a metricset\n");
	      cleanup(1);
	    }
	    iter = setmap.find(compid);
	  }
	  currfset = &(iter->second);
	}
	break;
      case 1: //time
	//      NOTE: we do *not* use the time and thus cannot do historical data.
	break;
      default: //its data
	{
	  //FIXME: revisit this now that we know that empty values mean repeat of past value (would not have to put in new val but would want to bump datagn)
	  char *pEnd;
	  unsigned long long llval;
	  llval = strtoll(val.c_str(),&pEnd,10);
	  union ldms_value v;
	  v.v_u64 = llval;
	  //	  printf("should be processing the data <%llu>\n", llval);
	  ldms_set_metric((*(currfset->metrichandles))[count-minheaderidx+1], &v);
	}
	break;
      }
    }
    count++;
  } //while



==================================================================
  
  int rc;
  int metric_no;
  char *s;
  char lbuf[256];
  char metric_name[128];
  char junk[128];
  union ldms_value v;

	metric_no = 0;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %"PRIu64 " %s\n", metric_name, &v.v_u64, junk);
		if (rc != 2 && rc != 3)
			return EINVAL;

		ldms_set_metric(metric_table[metric_no], &v);
		metric_no++;
	} while (s);
 	return 0;

  pthread_mutex_unlock(&cfg_lock);
  return 0;

};

static int sample(void)
{
  //NOTE: Sample should be set to some long time. This has the loop.
  //Currently: get the headers from the file.

  if (strlen(datafile) == 0){
    msglog("junk: No data file\n");
    return ENOENT;
  }

  char lbuf[10240]; //how big does this have to be? (L0_XT5_VOLTS_log on cds has 4K char)
  char command[LDMS_MAX_CONFIG_STR_LEN];
  snprintf(command,LDMS_MAX_CONFIG_STR_LEN-1,"tail -F %s", datafile);

  if (mf = (FILE*)popen(command, "r")){
    while (fgets( lbuf sizeof(lbuf), mf)){
      char* p = NULL;
      if (filetype == RSYSLOG){
	p = stripRsyslogHeaders(lbuf);
	if (p == NULL){
	  printf("Error stripping syslog headers\n");
	  cleanup(1);
	}
      } else {
	p = lbuf;
      }
      processSEDCData(p);
    }
  } else {
    msglog(1, "Could not open the sedc file '%s' for data ...exiting\n", datafile);
  }

  pthread_mutex_lock(&cfg_lock);
  return 0;


}
*/

static int sample(void){
  return 0;
}

static void cleanupset(gpointer key, gpointer value, gpointer user_data){
  struct fset *fs = (struct fset*) value;
  //FIXME: do we need to do anything with the metrics?
  ldms_destroy_set(fs->sd);
}

static void term(void)
{
	g_hash_table_destroy(compidmap);
	g_hash_table_foreach(setmap, cleanupset, NULL);
	g_hash_table_destroy(setmap);
}

static const char *usage(void)
{
  return  "    config junk component_id <comp_id>\n"
          "        - Set the component_id value in the metric set.\n"
          "        comp_id     The component id value\n"
          "    config junk datafile <datafile> <filetype>\n"
          "        - Set the datafile and datafile type\n"
          "        datafile    Path of the datafile\n"
          "        filetype    sedc or rsyslog\n"
          "    note: the setname is part of the init\n";
}

static struct ldms_plugin junk_plugin = {
	.name = "junk",
	.init = init,
	.term = term,
	.config = config,
	.get_set = get_set,
	.sample = sample,
	.usage = usage,
};

struct ldms_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &junk_plugin;
}
