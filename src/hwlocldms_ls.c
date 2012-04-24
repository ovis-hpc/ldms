#include <stdio.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include "metricmap.h"


int main(int argc, char* argv[])
{
  int i = 0;
  
  if (argc < 4){
    printf("Usage: hwloc_metric_mapper hwloc_file [metricdata_files] ldmscmd\n"); 
    exit (-1);
  }

  //NOTE: assuming the machine is homogeneous so can use 1 hwloc for all.
  //only difference is the hostname (Machine) translation 
  //FIXME: still have to handle the hostname translation

  int rc = setHwlocfile(argv[1]);
  if (rc != 0){
    exit(-1);
  }
  printComponents();  

  for (i = 2; i <= (argc-2); i++){
    parseMetricData(argv[i]);
    printMetrics();
  }


  //FIXME: the ldms_ls wrapper will call ldms_ls and then use this to do the translation.

  parseLDMSOutput(argv[argc-1]);


   cleanup();
   return 1;
}

