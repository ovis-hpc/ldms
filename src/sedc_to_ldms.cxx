/*
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*
 * This is the sedc data provider. Runs as a separate executable reading from an SEDC stream
corresponding to a single SEDC data file. writes to ldms or text files. will run multiples of these - 1 for
each sedc file.
 */
#include <inttypes.h>
#include <unistd.h>
#include <stdarg.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <libgen.h>
#include <signal.h>
#include <search.h>
#include "ldms.h"
#include "un.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
#include <string>
#include <vector>

#define FMT "S:M:H:N:O:l:Fv"
#define METRIC_SOCKET "/var/run/ldmsd/metric_socket"
#define SEDC_DATAFILE "/opt/cray/sedc/sedcfile"
#define OVIS_COMPIDMAP "/opt/cray/sedc/ovismap"
#define LOGFILE "/var/log/sedc.log"
#define SAMPLE_INTERVAL 1000000

static char *sockname = METRIC_SOCKET;
static char *logfile = LOGFILE;
static char *sedc_datafile = SEDC_DATAFILE;
static char *sedc_headerfile = "";
static char *ovismap = OVIS_COMPIDMAP;
static char *setshortname = "";
static FILE *log_fp;
static int foreground;
static int verbose;
static std::vector<int> set_nos;
static int ldmsout = 1;
static int sample_interval = SAMPLE_INTERVAL;

//we have multiple sets - one for each distinct compid
static std::map<std::string,int> compidmap; //compname to id
static size_t tot_meta_sz = 0;
static size_t tot_data_sz = 0;
static std::vector<std::string> sedcheaders;
static int minheaderidx = 2; //the first few headers will not be metric vals (e.g., compname, time)

struct fset {
  ldms_set_t sd;
  std::vector<ldms_metric_t>* metrichandles; //one for each metric header plus the compid
};

static std::map<int, fset> setmap; //compid to fsetmap


static void msglog(int err, const char *fmt, ...)
{
  va_list ap;
  if (!err && !verbose)
    return;
  va_start(ap, fmt);
  vfprintf(log_fp, fmt, ap);
  fflush(log_fp);
}

void usage(char *argv[])
{
  printf("%s: [%s]\n"
	 "    -S <socket>      The UNIX socket that the ldms daemon is listening on.\n"
	 "                     [" METRIC_SOCKET "].\n"
	 "    -M <datafile>    The name of the sedc file to tail [" SEDC_DATAFILE "].\n"
	 "    -H <headerfile>  The name of the sedc header list file (if none, will look for it in the datafile).\n"
         "    -N <setshortname>     The metricset shortname (e.g., \"L0_VOLTS\" => host_cname/LO_VOLTS)\n"
	 "    -O <ovismap>     The name of the file that contains crayname to ovis compid pairs [" OVIS_COMPIDMAP "].\n"
	 "    -l log_file       The path to the log file for status messages.\n"
	 "                      [" LOGFILE "]\n"
	 "    -v                Verbose mode, i.e. print requests as they are processed.\n"
	 "                      [false].\n",
	 argv[0], FMT);
  printf("Example: ./lib/sedc_to_ldms -O /home/gentile/Work/opt/cray/sedc/ovismap -M /home/gentile/Work/opt/cray/sedc/sedcdatafile -N LO_G34_TEMPS  -l /home/gentile/Work/opt/cray/txtout/sedc.log\n");
  exit(1);
}

void cleanup(int x)
{
  if (log_fp){
    msglog(1, "sedc daemon exiting...status %d\n", x);
    fclose(log_fp);
  }
  std::map<int, fset>::iterator iter;
  for (iter == setmap.begin(); iter != setmap.end(); iter++){
    fset currset = iter->second;
    free(currset.metrichandles);
  }
  setmap.clear();
  sedcheaders.clear();
  compidmap.clear();

  exit(x);
}

void no_server(void)
{
  printf("Server is no longer available...exiting.\n");
  cleanup(1);
}

int processCompIdMap(std::string fname){
  //FIXME: can we have a function for this (note have to handle L0, node, though not the full
  //set of options since remote assoc will be handled at insert)? can this be a type and
  //offset or something??

  std::string line;
  std::ifstream myfile(fname.c_str());
  if (myfile.is_open()){
    while(myfile.good()){
      getline(myfile,line);
      std::stringstream ss;
      ss << line;
      std::string name;
      int val;
      //FIXME: check for blank lines
      ss >> name >> val;
      compidmap[name] = val;
    }
    myfile.close();
  } else {
    printf("Error processing the CompId map file %s.\n", fname.c_str());
    cleanup(1);
  }

  std::cout << "CompIdMap:\n";
  std::map<std::string,int>::iterator iter;
  for (iter = compidmap.begin(); iter != compidmap.end(); iter++){
    std::cout << "<" << iter->first << "><" << iter->second << ">\n";
  }
  std::cout << "\n";

};

int createMetricSet(std::string hostname, int compid, std::string shortname){
  //create the metricset for a new compid
  char lbuf[256];
  char metric_name[128];
  char junk[128];
  uint64_t metric_value;
  char setnamechar[1024];

  //FIXME: setname will be <hostname>/<setshortname>
  //can we get nid if we want that?
  std::string setname = hostname;
  setname.append("/");
  setname.append(shortname);
  snprintf(setnamechar,1024,"%s",setname.c_str());

  fset currfset;
  int set_no = -1;
  /* Create a metric set of the required size */
  int rc = ldms_create_set(setnamechar, tot_meta_sz, tot_data_sz, &(currfset.sd));
  if (rc != 0){
    printf("Error %d creating metric set '%s'.\n", rc, setnamechar);
    exit(1);
  }

  ldms_metric_t currmetric =  ldms_add_metric(currfset.sd, "component_id", LDMS_V_U64);
  if (currmetric == 0){
    printf("Error creating the metric %s.\n", "component_id");
    exit(1);
  }	
  currfset.metrichandles->push_back(currmetric);

  for (int i = minheaderidx; i < sedcheaders.size(); i++){
    char headerchar[256];
    snprintf(headerchar,255,"%s",sedcheaders[i].c_str());
    ldms_metric_t currmetric = ldms_add_metric(currfset.sd, headerchar, LDMS_V_U64);
    if (currmetric == 0){
      printf("Error creating the metric %s.\n", headerchar);
      exit(1);
    }
    currfset.metrichandles->push_back(currmetric);
  }

  //fill in the comp id for ldms (for file it is filled in elsewhere)
  union ldms_value v;
  v.v_u64 = compid;
  ldms_set_metric((*(currfset.metrichandles))[0], &v);
  
  setmap[compid]  = currfset;

  return 0;
};

int processSEDCHeader(char* line){
  //header will look just like that on the SEDC file (2 extra non-metric fields)
  //split the line into tokens based on comma sep

  int count = 0;
  std::stringstream ss;
  ss << line;
  std::string val;
  while (getline(ss,val,',')){
    if (count == 0){
      if (strcmp(val.c_str(), "service id") != 0){
	return 0;
      }
    }

    //there will a newline at the end - strip it
    size_t found = val.find("\n", val.size()-1, 1);
    if (found != std::string::npos){
      val.erase(found,1); 
    }
    sedcheaders.push_back(val);
    count++;
  }
  if (count > 0) {
    std::cout << "Headers: ";
    for (int i = 0; i < sedcheaders.size(); i++){
      std::cout << "<" << sedcheaders[i] << "> ";
    }
    std::cout << "\n";
  }

  return count;
}

int calcMetricSetSize(){
  if (sedcheaders.size() <= minheaderidx){
    printf("Invalid number of headers\n");
    cleanup(1);
  }

  size_t meta_sz = 0;
  size_t data_sz = 0;

  ldms_get_metric_size("component_id", LDMS_V_U64, &tot_meta_sz, &tot_data_sz);
  for (int i = minheaderidx; i < sedcheaders.size(); i++){
    ldms_get_metric_size(sedcheaders[i].c_str(), LDMS_V_U64, &meta_sz, &data_sz); //type for all metrics
    tot_meta_sz += meta_sz;
    tot_data_sz += data_sz;
  }

  return 0;
};

int processSEDCData(char* line){
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
	  ldms_set_metric((*(currfset->metrichandles))[count-minheaderidx+1], &v);
	}
	break;
      }
    }
    count++;
  } //while
};
  
int main(int argc, char *argv[])
{
  int op;
  int i;
  int rc;
  char *s;

  signal(SIGHUP, cleanup);
  signal(SIGINT, cleanup);
  signal(SIGTERM, cleanup);

  opterr = 0;
  while ((op = getopt(argc, argv, FMT)) != -1) {
    switch (op) {
    case 'S':
      sockname = strdup(optarg);
      break;
    case 'M':
      sedc_datafile = strdup(optarg);
      break;
    case 'N':
      setshortname = strdup(optarg);
      break;
    case 'H':
      sedc_headerfile = strdup(optarg);
      break;
    case 'O':
      ovismap = strdup(optarg);
      break;
    case 'l':
      logfile = strdup(optarg);
      break;
    case 'F':
      foreground = 1;
      log_fp = stdout;
      break;
    case 'v':
      verbose = 1;
      break;
    default:
      usage(argv);
    }
  }

  if (strlen(setshortname) <= 0){
    printf("Error: Need a set shortname\n");
    exit(1);
  };

  //FIXME: will this be written, will this be a function???
  if (processCompIdMap(ovismap) < 0){
    printf("Error processing ovis compid map.\n");
    exit(1);
  }


  //either read a separate header or read the file, throwing away data till we reach the header
  FILE *mf = NULL;
  char lbuf[10240]; //FIXME: how big does this have to be? (L0_XT5_VOLTS_log on cds has 4K char)
  std::string command = "tail -F ";
  command.append(sedc_datafile);

  if (strlen(sedc_headerfile) > 0 ){ 
    //get the headers from a file
    std::cout << "getting the headers from a file\n";
    std::ifstream myfile(sedc_headerfile);
    if (myfile.is_open() && myfile.good()){
      std::string line;
      getline(myfile, line);
      snprintf(lbuf,10239, "%s", line.c_str());
      myfile.close();
      if (processSEDCHeader(lbuf) <= 0){
	printf("Error processing the header file %s.\n", sedc_headerfile);
	cleanup(1);
      }
    } else {
	printf("Error processing the header file %s.\n", sedc_headerfile);
	cleanup(1);
    }
  } else { 
    //get the headers from the data file, throwing away data
    std::cout << "getting the headers from datafile\n";
    //FIXME: what is this going to be? is this a pipe/socket???
    if (mf = (FILE*)popen(command.c_str(), "r")){
      while (fgets( lbuf, sizeof(lbuf), mf)){
	if (processSEDCHeader(lbuf) > 0){
	  break;
	}
      }
    } else {
      pclose(mf);
      std::cout << "Could not open the sedc file\n";
      msglog(1, "Could not open the sedc file '%s'...exiting\n", sedc_datafile);
      cleanup(1);
    }
  }
  // will now have the headers

  //FIXME: what is this about basename?
  //  if (un_connect(basename(argv[0]), sockname) < 0) {
  //    printf("Error setting up connection with ldmsd.\n");
  //    exit(1);
  //  }

  if (!foreground) {
    log_fp = fopen(logfile, "a");
    if (!log_fp) {
      printf("Could not open the log file named '%s'\n", logfile);
      usage(argv);
    }
    daemon(0, 0);
  } else {
    log_fp = stdout;
    msglog(1, "sedcinfo service started...\n");
  }
  
  calcMetricSetSize(); //need an ldms connection to do this

  if (!mf){ //if we haven't already read it for the header
    std::cout << "should be opening datafile <" << command << ">\n";
    //FIXME: what is this going to be? is this a pipe/socket???
    mf = (FILE*)popen(command.c_str(), "r");
  }
  if (mf){
    //every component will have its own metric set. create each metric set as needed.
    //FIXME: see later about farming off the line processing to separate threads 
    while( fgets( lbuf, sizeof(lbuf), mf) ){
      processSEDCData(lbuf);
    }
  } else {
    std::cout << "Could not open the sedc file for data <" << sedc_datafile << ">\n";
    msglog(1, "Could not open the sedc file '%s' for data ...exiting\n", sedc_datafile);
    cleanup(1);
  }
};

