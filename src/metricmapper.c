#include <stdio.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>


//FIXME: want to turn this into a library and then the ldms_ls wrapper will call ldms_ls and then use this to do the translation.
//FIXME: for the library, can we build it with the metric file, rather than this re-reading of the config files and rebuilding the map
//NOTE: assuming the machine is homogeneous so can use 1 hwloc for all. only difference is the hostname (Machine) translation 
//FIXME: still have to handle the hostname translation


#define MAXSHORTNAME 32
#define MAXLONGNAME 1024
#define MAXBUFSIZE 8192
#define MAXATTR 10

#define LVALPLACEHOLDER "(LVAL)"

//ldms
#define MAXSETS 10
#define MAXMETRICSPERSET 200

//hwloc
#define MAXHWLOCLEVELS 10
#define MAXCOMPONENTSPERLEVEL 20


//hwloc
struct Linfo {
  char assoc[MAXSHORTNAME];
  char Lval[5];
  char Pval[5];
  char prefix[MAXLONGNAME];
};

struct CompTypeInfo{
  char assoc[MAXSHORTNAME];
  struct Linfo* instances[MAXCOMPONENTSPERLEVEL];
  int numinstances;
};

struct CompTypeInfo hwloc[MAXHWLOCLEVELS];
int numlevels = 0;


//ldms
struct MetricInfo{
  char ldmsname[MAXSHORTNAME];
  struct Linfo* instance;
};

struct SetInfo{
  char setname[MAXLONGNAME];
  struct MetricInfo metrics[MAXMETRICSPERSET];
  int nummetrics;
};

struct SetInfo sets[MAXSETS];
int numsets = 0;

//temporary for parsing
struct Linfo tree[MAXHWLOCLEVELS];
int treesize = 0;

int numhwlocfiles = 0;
int nummetricfiles = 0;


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


int cleanup(){
  int i, j;
  for (i = 0; i < numlevels; i++){
    for (j = 0; j < hwloc[i].numinstances; j++){
      free(hwloc[i].instances[j]);
    }
  }
}


int getInstanceLDMSName(char* orig, char* Lval, char* newname){
  //the metric name must have an LVAL to replace, expect for where there is only 1 instance of that component involved
  //eg the metricname might be CPU(LVAL)_cpu_user_idle -> CPU3_cpu_user_idle
  //dont currently have a good way to do functions of that

  //FIXME: this has not yet been tested for multiple replacements

  strncpy(newname, orig, MAXSHORTNAME);
  char *p;
  char buf[MAXSHORTNAME];

  //  printf("considering <%s>\n", orig);

  while (p = strstr(newname, LVALPLACEHOLDER)){
    strncpy(buf, newname, p-newname);
    buf[p-newname] = '\0';
    sprintf(buf+(p-newname), "%s%s", Lval, p+strlen(LVALPLACEHOLDER));

    strncpy(newname, buf, strlen(buf));
    newname[strlen(buf)] = '\0';
  }
  
  //  printf("returning <%s>\n", newname);

  return 0;
}


int parseMetricData(char* inputfile){
  //user metric data is in a file.
  //first line of the file is the hwloc component type
  //all subsequent lines are ldms setname/metricname

  //if a line starts with # it will be skipped
  char buf[MAXBUFSIZE];
  char assoc[MAXSHORTNAME]; 
  int haveassoc = 0;
  char setname[MAXLONGNAME];
  char metricname[MAXSHORTNAME];
  int comptypenum = -1;

  int numVals = 0;
  int i;
  int setnum = -1;

  FILE *fp = fopen(inputfile, "r");
  if (fp == NULL){
    printf("Error: Can't open metric data file. exiting.\n");
    exit (-1);
  }

  while (fgets(buf, (MAXBUFSIZE-1), fp) != NULL){
    //    printf("read <%s>\n", buf);
    if (buf[0] == '#'){ //its a comment
      continue;
    }
    if (strlen(buf) < 1){
      continue;
    }
    buf[strlen(buf)-1] = '\0'; //remove the new line
    if (haveassoc == 0){
      //      printf("checking component <%s>\n", buf);
      sscanf(buf, "%s", assoc);
      if (strlen(assoc) == 0){
	continue;
      } 
      comptypenum = -1;
      for (i = 0; i < numlevels; i++){
	if (!strcmp(hwloc[i].assoc, assoc)){
	  comptypenum = i;
	  break;
	}
      }
      if (comptypenum == -1){
	printf("Error: dont know assoc <%s>\n");
	exit (-1);
      }
      haveassoc = 1 ;
    } else {
      if (buf[0] == '#'){ //its a comment
	continue;
      }
      char *p  = strstr(buf,"/"); //FIXME: assume this is setname/metricname
      if (p == NULL){
	continue;
      }
      strncpy(metricname, p+1, strlen(p));
      metricname[strlen(p)] = '\0';
      strncpy(setname, buf, strlen(buf)-strlen(p));
      setname[strlen(buf)-strlen(p)] = '\0';
      //      printf("<%s><%s>\n",setname, metricname);

      setnum = -1;
      for (i = 0; i < numsets; i++){
	if (!strcmp(sets[i].setname,setname)){
	  setnum = i;
	}
      }

      if (setnum == -1){
	strncpy(sets[numsets].setname,setname,MAXLONGNAME);
	sets[numsets].nummetrics = 0;
	setnum = numsets;
	numsets++;
      }

      for (i = 0; i < hwloc[comptypenum].numinstances; i++){
	int metricnum = sets[setnum].nummetrics;
	char newname[MAXSHORTNAME];
	int rc = getInstanceLDMSName(metricname, hwloc[comptypenum].instances[i]->Lval, newname);
	if (rc != 0){
	  printf("Error: Cannot parse the metric regex. Exiting\n");
	  exit (-1);
	}
	strncpy(sets[setnum].metrics[metricnum].ldmsname, newname, MAXSHORTNAME);
	sets[setnum].metrics[metricnum].instance = hwloc[comptypenum].instances[i];
	sets[setnum].nummetrics++;
	numVals++;
      }
    }
  } //while
  fclose(fp);

  return numVals;
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

void  addComponent(char* hwlocAssocStr, int Lval, int Pval){
  //  static int treesize;
  char prefix[1024];
  int found = 0;
  int i;
   
  // tree is really the current branch, used to build the prefix
  for (i=0; i<treesize; i++) {
    if ( !strncmp(tree[i].assoc, hwlocAssocStr, MAXSHORTNAME) ) {
      snprintf(tree[i].Lval,5,"%d",Lval);
      snprintf(tree[i].Pval,5,"%d",Pval);
      treesize = i + 1;
      found = 1;
      break;
    }
  } 
  if (!found) {
    strcpy(tree[treesize].assoc, hwlocAssocStr);
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
    strcat(prefix,tree[i].assoc);
    strcat(prefix,tree[i].Lval); //do we want the Pval?
    strcat(prefix,".");
  }

  //retain the component
  struct Linfo* li = (struct Linfo*)malloc(sizeof(struct Linfo));
  strncpy(li->assoc, hwlocAssocStr, MAXSHORTNAME);
  snprintf(li->Lval,5,"%d",Lval);
  snprintf(li->Pval,5,"%d",Pval);
  strncpy(li->prefix,prefix,MAXLONGNAME);

  found = -1;
  for (i = 0; i < numlevels; i++){
    if (!strcmp(li->assoc,hwloc[i].assoc)){
      found = i;
      break;
    }
  }
  if (found == -1){
    strncpy(hwloc[numlevels].assoc,strdup(li->assoc),MAXSHORTNAME);
    hwloc[numlevels].numinstances = 0;
    found = numlevels;
    numlevels++;
  }
  hwloc[found].instances[hwloc[found].numinstances++] = li;
}


int parseLDMSOutput(char* cmd){
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
  printf("trying to execute <%s>\n", cmd);
  if (!(fpipe = (FILE*)popen(cmd, "r"))){
    perror("Cant exec ldms command");
    exit (-1);
  }

  char metricset[MAXLONGNAME];
  char metricshortset[MAXLONGNAME];
  int setnum;
  char A[3][MAXLONGNAME];

  while (fgets(buf, sizeof buf, fpipe)){
    //if there is 1 item on the line, its a setname, if there are 3, its data
    //    printf("read <%s>\n",buf);
    int i;

    char* pch;
    pch = strtok(buf, " \t\n");
    int idx = -1;
    while (pch != NULL){
      idx++;
      if (idx == 3){
	break;
      }
      strncpy(A[idx],pch,strlen(pch));
      A[idx][strlen(pch)] = '\0';
      //      printf("assigned <%s>\n", A[idx]);
      pch = strtok(NULL, " \t\n");
    }
    if (idx == 0){
      strncpy(metricset,A[0],strlen(A[0]));
      metricset[strlen(A[0])] = '\0';
      char *p  = strstr(metricset,"/"); //FIXME: assume this is hostname/metricsetname
      strncpy(metricshortset, p+1, strlen(p));
      metricshortset[strlen(p)] = '\0';
      setnum = -1;
      for (i = 0; i < numsets; i++){
	if (!strcmp(sets[i].setname,metricshortset)){
	  setnum = i;
	  break;
	}
      }
      if (setnum == -1){
	printf("Error: dont know set: <%s>. Printing defaults\n", metricshortset);
      }
      printf("%s\n",A[0]);
    } else if (idx == 2){
      //process this metric
      int metricnum = -1;
      if (setnum != -1){
	for (i = 0; i < sets[setnum].nummetrics; i++){
	  if (!(strcmp(sets[setnum].metrics[i].ldmsname,A[2]))){
	    metricnum = i;
	    break;
	  }
	}
      }
      if (metricnum > -1){
	printf("%s %40s %40s %s%s\n",A[0],A[1],A[2],sets[setnum].metrics[i].instance->prefix,sets[setnum].metrics[i].ldmsname);
      } else {
	printf("%s %40s %40s\n",A[0],A[1],A[2]);
      }
    } else {
      printf("\n");
    }
  }

  pclose(fpipe);

  return 0;
}


printComponents(){
  int i,j;
  printf("Components:\n");
  for (i = 0; i < numlevels; i++){
    printf("%s:\n", hwloc[i].assoc);
    for (j = 0; j < hwloc[i].numinstances; j++){
      printf("\t%s\n",hwloc[i].instances[j]->prefix);
    }
  }
  printf("\n");
}


printMetrics(){
  int i, j;
  printf("Metrics:\n");
  for (i = 0; i < numsets; i++){
    printf("%s:\n", sets[i].setname);
    for (j = 0; j < sets[i].nummetrics; j++){
      printf("\t%s%s\n", sets[i].metrics[j].instance->prefix, sets[i].metrics[j].ldmsname);
    }
  }
  printf("\n");
}

int setHwlocfile(char* file){
   FILE *fd;
   char *s;
   char lbuf[MAXLONGNAME];
   char hwlocAssocStr[MAXSHORTNAME];
   char keys[MAXATTR][MAXSHORTNAME];
   int attrib[MAXATTR];
   int numAttrib;
   int Lval, Pval;

   if (numhwlocfiles > 0){
     printf("Error: cannot set another hwlocfile\n");
     return -1;
   }

   fd = fopen(file, "r");
   if (!fd) {
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
       //ignore the attributes for now
       addComponent(hwlocAssocStr, Lval, Pval);
     }
   } while (s);
   fclose (fd);
   
   return 0;
}


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

  printf("%d %s %s %s <%s>\n", argc, argv[0], argv[1], argv[2], argv[3]);

  for (i = 2; i <= (argc-2); i++){
    parseMetricData(argv[i]);
    printMetrics();
  }


  if (0){
  //FIXME: want to turn this into a library and then the ldms_ls wrapper will call ldms_ls and then use this to do the translation.
  parseLDMSOutput(argv[argc-1]);


  //FIXME: for the library, can we build it with the metric file, rather than this re-reading of the config files and
  //rebuilding the map
  }

   cleanup();
   return 1;
}

