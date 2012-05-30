#include <stdio.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include "metricmap.h"

//FIXME: assumes hostname/setname/metricname

#define MAXMETRICDATAFILES 10

int setLDMSValues(char* cmd){
   //will need to parse the ldms_ls results to extract machinename, setname, metricname
   //format:
   //shuttlers_1/meminfo
   // U64 1                component_id
   // U64 8194816          MemTotal
   // U64 4830748          MemFree
   // U64 224248           Buffers
   // U64 2129480          Cached
   //
   //shuttlers_1/junk
   // U64 3                Cpu0_ERR
   // U64 0                Cpu0_MIS


  FILE* fpipe;
  char buf[MAXBUFSIZE];

  //  printf("trying to execute <%s>\n", cmd);
  printf("\n\n===========================================================\n\n");
  if (!(fpipe = (FILE*)popen(cmd, "r"))){
    perror("Cant exec ldms command");
    exit (-1);
  }

  char hostname[MAXLONGNAME];
  char metricset[MAXLONGNAME];
  char metricname[MAXLONGNAME];
  char metricshortset[MAXLONGNAME];
  unsigned long val;

  while (fgets(buf, sizeof buf, fpipe)){
    //if there is 1 item on the line, its a setname, if there are 3, its data
    //       printf("read <%s>\n",buf);
    int rc = sscanf(buf,"%s\t%lu %s", metricset, &val, metricname);
    switch (rc){
    case 1:
      {
	char *p  = strstr(metricset,"/"); //FIXME: assume this is hostname/metricsetname
	snprintf(metricshortset, strlen(p), "%s", p+1);
	snprintf(hostname, strlen(metricset)-strlen(p)+1,"%s", metricset);
      }
      break;
    case 3:
      {
	int i = setMetricValueFromLDMS(hostname,metricshortset,metricname, val);
	if (i != 0){
	  printf("WARNING: No metric for oid <%s><%s><%s>\n", hostname, metricshortset, metricname);
	}
      }
      break;
    default:
      //      printf("<rc = %d>\n",rc);
      //do nothing
      break;
    }
  }

  if (fpipe) fclose(fpipe);

  return 1;
}

int main(int argc, char* argv[])
{
  int i = 0;
  int numdatafiles = 0;

  char metricdatafiles[MAXHWLOCLEVELS][MAXBUFSIZE];
  
  if (argc < 4){
    printf("Usage: hwloc_metric_mapper hwloc_file machine_file [metricdata_files] ldms_ls_flags\n"); 
    exit (-1);
  }

  //NOTE: assuming the machine is homogeneous so can use 1 hwloc for all.
  //only difference is the hostname (Machine) translation 

  for (i = 3; i < argc-1; i++){
    if (numdatafiles >= MAXHWLOCLEVELS){
      printf("Error: Too many metricdata files\n");
      exit (-1);
    }
    snprintf( metricdatafiles[numdatafiles++], MAXBUFSIZE, "%s", argv[i]);
  }

  parseData(argv[2], argv[1], metricdatafiles, numdatafiles);

  printComponents(1);
  printTree(-1);

  setLDMSValues(argv[argc-1]);
  printf("before print tree\n");
  printTree(2);

   cleanup();
   return 1;
}

