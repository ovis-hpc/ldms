#include <stdio.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>

#define MAXSHORTNAME 32
#define MAXLONGNAME 1024
#define MAXBUFSIZE 8192
#define MAXHWLOCLEVELS 10
#define MAXMETRICSPERLEVEL 50
#define MAXATTR 10

//enum if want specific line parses based upon these names
//let 0 be unknown/default, let negative be something you want to skip, let positive be something handled specifically
enum hwlocAssoc{
  PU = -1,
  Machine = 1,
  L3Cache = 2,
  L2Cache = 3,
  L1Cache = 4,
  Socket = 5,
  NUMANode = 6,
};


int getHwlocAssoc( char *assoc ){
  if (!strncmp(assoc, "PU", MAXSHORTNAME)){
    return PU;
  }
  if (!strncmp(assoc, "Machine", MAXSHORTNAME)){
    return Machine;
  }
  if (!strncmp(assoc, "Socket", MAXSHORTNAME)){
    return Socket;
  }
  if (!strncmp(assoc, "NUMANode", MAXSHORTNAME)){
    return NUMANode;
  }
  if (!strncmp(assoc, "L3Cache", MAXSHORTNAME)){
    return L3Cache;
  }
  if (!strncmp(assoc, "L2Cache", MAXSHORTNAME)){
    return L2Cache;
  }
  if (!strncmp(assoc, "L1Cache", MAXSHORTNAME)){
    return L1Cache;
  }
  return 0;
}

struct Linfo {
  char name[MAXSHORTNAME]; 
  char Lval[5];
  char Pval[5];
};

   
struct MetricInfo{
  char localSetName[MAXLONGNAME];
  char ldmsMetricName[MAXSHORTNAME];
};

struct MetricArrayInfo{
  char assoc[MAXSHORTNAME];
  int numVals;
};

struct MetricInfo metricData[MAXHWLOCLEVELS][MAXMETRICSPERLEVEL]; //fixed size array for storing the user data
struct MetricArrayInfo metricMapData[MAXHWLOCLEVELS]; //fixed size array for index and info on the userMetricData
int numMetricHwlocLevels;
int treesize = 0;

struct Linfo tree[MAXHWLOCLEVELS];

int parseMetricData(char* inputfile){
  //user metric data is in a file. 
  //each line of the file is a space delimited list of: hwlocassoc localsetname {any number of ldmsmetricname} 
  //if a line starts with # it will be skipped
  //blank lines will be skipped
  //FIXME: repeats of hwlocassoc and setnames are allowed, but there is no check for repeat metric names

  char buf[MAXBUFSIZE];
  char assoc[MAXSHORTNAME];
  char setname[MAXLONGNAME];
  int numVals = 0;
  int index = -1;
  int count;  
  int i;

  numMetricHwlocLevels = 0;
  FILE *fp = fopen(inputfile, "r");
  if (fp == NULL){
    printf("Error: Can't open metric data file. exiting.\n");
    exit (-1);
  }
  while (fgets(buf, (MAXBUFSIZE-1), fp) != NULL){
    count = 0;
    index = -1;
    char* pch;
    pch = strtok(buf, " \n");
    while (pch != NULL){
      if (count == 0){
	if (pch[0] == '#'){ //its a comment
	  break;
	}
	strncpy(assoc, pch, MAXSHORTNAME);
	count++;
	//get the index for this assoc if it exists
	for (i = 0; i < numMetricHwlocLevels; i++){
	  if (!strncmp(metricMapData[i].assoc, assoc, MAXSHORTNAME)){
	    index = i;
	    break;
	  }
	}
      } else if (count == 1){
      	strncpy(setname, pch, MAXSHORTNAME);
	count++;
      } else {
	//its a metric

	//add the assoc if necessary
	if (index < 0){
	  index = numMetricHwlocLevels++;
	  strncpy(metricMapData[index].assoc, assoc, MAXSHORTNAME);
	  metricMapData[index].numVals = 0;
	}
	  
	//add the value
	int valindex = metricMapData[index].numVals++;
	strncpy(metricData[index][valindex].localSetName, setname, MAXLONGNAME);
	strncpy(metricData[index][valindex].ldmsMetricName, pch, MAXSHORTNAME);
	numVals++;
	count++;
      }
      pch = strtok( NULL, " \n");
    }
  } //while
  fclose(fp);

  return numVals;
} 

void printMetricData(){
  int i, j;

  for (i = 0; i < numMetricHwlocLevels; i++){
    printf("%s:\n", metricMapData[i].assoc);
    for (j = 0; j < metricMapData[i].numVals; j++){
      printf("\t%s %s \n", metricData[i][j].localSetName, metricData[i][j].ldmsMetricName);
    }
    printf("\n");
  }
}

int parse_line(char* lbuf, char* comp_name, int* Lval, int* Pval, char keys[MAXATTR][MAXSHORTNAME], int* attr, int* numAttr){
  enum hwlocAssoc assoc;
  *Lval = -1; 
  *Pval = -1;
  *numAttr = 0;
  int minindex = 0;
  char* ptr;

  //  printf("Raw line <%s>\n",lbuf);
  while(lbuf[minindex] == ' ') {   //strip any leading whitespace
    minindex++;
  }
  ptr = lbuf+minindex;
  if (ptr[0] == '\n'){ //skip blank lines
    return -1;
  }
  //split into the header and the attributes
  char header[MAXBUFSIZE];
  char attrs[MAXBUFSIZE];
  int len = strcspn(ptr, "(");
  strncpy(header, ptr, len);
  header[len] = '\0';
  if (len == strlen(ptr)){
    attrs[0] = '\0';
  } else {
    strncpy(attrs, ptr+len+1, strlen(ptr)-len-1);
    attrs[strlen(ptr)-len-2] = '\0'; //strip the newline
  }

  //  printf("\n\nsplitline header <%s>\n", header);
  //  printf("splitline attrs <%s>\n", attrs);

  //parse header - comptype and optional Lval
  len = strcspn(header, " ");
  strncpy(comp_name, header, len);
  comp_name[len] = '\0';
  assoc = getHwlocAssoc(comp_name);
  if (assoc < 0){ //we dont care about this component
    return -1; 
  }
  ptr = header+len+1;
  if (ptr[0] == 'L' && ptr[1] == '#'){
    *Lval = atoi(ptr+2); //this will handle any extra whitespace
  }

  //parse attrs - optional Pval and key value pairs
  if (attrs[0] == 'P' && attrs[1] == '#'){
    *Pval = atoi(attrs+2);
    len = strcspn(attrs, " ");
    ptr = attrs+len+1;
  } else {
    ptr = attrs;
  }

  //now key-value pairs.
  char* pch;
  int key = 1;
  pch = strtok(ptr, "=)");
  while (pch != NULL){
    //    printf("considering <%s> (%d)\n",pch, key);
    if (key){
      //strip any leading whitespace
      minindex = 0;
      while(pch[minindex] == ' '){
	minindex++;
      }
      strncpy(keys[*numAttr],pch+minindex,strlen(pch)-minindex);
      keys[*numAttr][strlen(pch)] = '\0';

      //some name changes
      switch (assoc) {
      case L3Cache:
      case L2Cache:
      case L1Cache:
	if (strcmp(keys[*numAttr], "size")){
	  strcpy(keys[*numAttr], "cache_size");
	} else if (strcmp(keys[*numAttr], "ways")){
	  strcpy(keys[*numAttr], "cache_ways");
	} 
	break;
      case Machine:
      case Socket:
      case NUMANode:
	if (strcmp(keys[*numAttr], "total")){
	  strcpy(keys[*numAttr], "mem_total");
	} else if (strcmp(keys[*numAttr], "local")){
	  strcpy(keys[*numAttr], "mem_local");
	} 
	break;
      default:
	;
      }

      key = 0;
      pch = strtok(NULL," )");
    } else {
      //its the value. no good way to handle partial names
      if (pch[0] == '\"'){ //wont be a number
	pch = strtok(NULL,"\""); //each the rest
      } else {
	char *endptr;
	long val;
	val  = strtol(pch, &endptr, 10);
	if (endptr != pch){
	  attr[*numAttr] = (int) val;
	  //	  printf("adding <%s> <%d> <%d>\n", keys[*numAttr], *numAttr, attr[*numAttr]);
	  (*numAttr)++;
	}
      }
      key = 1;
      pch = strtok(NULL,"=)");
    } // else (value)
  }

  //special cases
  switch(assoc){
  case Machine:
    *Lval = 0;
    break;
  default:
    break;
  }

  return 1;
}

void  printLineData(char* hwlocAssocStr, int Lval, int Pval, char keys[MAXATTR][MAXSHORTNAME], int* attrib, int numattrib){
  //  static int treesize;
  char prefix[1024];
  int found = 0;
  int i;
   
  for (i=0; i<treesize; i++) {
    if ( !strncmp(tree[i].name, hwlocAssocStr, MAXSHORTNAME) ) {
      snprintf(tree[i].Lval,5,"%d",Lval);
      snprintf(tree[i].Pval,5,"%d",Pval);
      treesize = i + 1;
      found = 1;
      break;
    }
  } 
  if (!found) {
    strcpy(tree[treesize].name, hwlocAssocStr);
    snprintf(tree[treesize].Lval,5,"%d",Lval);
    snprintf(tree[treesize].Pval,5,"%d",Pval);
    treesize++;
  }
  if (treesize > (MAXHWLOCLEVELS - 1)) {
    printf ("treesize exceeds limits\n");
    exit(0);
  }

  strcpy(prefix,"");
  for (i=0; i<treesize; i++) {
    strcat(prefix,tree[i].name);
    strcat(prefix,tree[i].Lval); //do we want the Pval?
    strcat(prefix,".");
  }
  

  //print your static data
  for (i = 0; i < numattrib; i++){
    printf ("%s%s=%d\n", prefix, keys[i],attrib[i]);
  }
  found = numattrib;
  
  //print your ldms data
  int index = -1;
  //get the index for this assoc if it exists
  for (i = 0; i < numMetricHwlocLevels; i++){
    if (!strncmp(metricMapData[i].assoc, tree[(treesize-1)].name, MAXSHORTNAME)){
      index = i;
      break;
    }
  }
  if (index != -1){
    for (i = 0; i < metricMapData[index].numVals; i++){
      printf("%s%s=%d\n", prefix, metricData[index][i].ldmsMetricName, 0);
    }
    found+= metricMapData[index].numVals;
  }
  if (!found){
    printf("%s has no metrics\n", prefix);
  }
}


int main(int argc, char* argv[])
{
   FILE *fd;
   char lbuf[MAXLONGNAME];
   char *s;
   char hwlocAssocStr[MAXSHORTNAME];
   char keys[MAXATTR][MAXSHORTNAME];
   int attrib[MAXATTR];
   int numAttrib;
   int Lval, Pval;
   
   if (argc != 3){
     printf("Usage: hwloc_metric_mapper metricdata_file hwloc_file\n");
     exit (-1);
   }
   
   parseMetricData(argv[1]);
   //   printMetricData();

// Args are machine name e.g. nid00001 and hwloc filename representing that nodes hardware
   fd = fopen(argv[2], "r");
   if (!fd) {
//      msglog("Could not open the file hwloc.out...exiting\n");
      printf("Could not open the file hwloc.out...exiting\n");
      return ENOENT;
   }

   fseek(fd, 0, SEEK_SET);
   do {
      s = fgets(lbuf, sizeof(lbuf), fd);
      if (!s)
         break;
      //      printf("fgets: <%s>\n", lbuf);
      if (parse_line(lbuf, hwlocAssocStr, &Lval, &Pval, keys, attrib, &numAttrib) > 0){
	//   printf("comp_name = %s, L = %d, P = %d, cache_size = %d, cache_linesize = %d, cache_ways = %d\n", hwlocAssocStr, attrib[0], attrib[1], attrib[2], attrib[3], attrib[4]);
	printLineData(hwlocAssocStr, Lval, Pval, keys, attrib, numAttrib);
      }
   } while (s);
   fclose (fd);

   return 1;
}

