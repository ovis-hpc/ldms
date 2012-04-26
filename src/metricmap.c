#include <stdio.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include "metricmap.h"

int numlevels = 0;
int numsets = 0;
int treesize = 0;


//NOTE: that Machine0 will be everyhosts base host
//FIXME: assumes hostname/setname/metricname
//FIXME: is it a correct assumption that the order the assocs are reached (and therefore assigned in the sets) is the depth? I think so.


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
  //free the metrics thru hwloc because there are metrics via hwloc that are not ldms metrics
  int i, j, k;
  for (i = numlevels-1; i > numlevels; i--){
    for (j = 0; j < hwloc[i].numinstances; j++){
      for (k = 0; k < hwloc[i].instances[j]->nummetrics; k++){
	free(hwloc[i].instances[j]->metrics[k]);
      }
      free(hwloc[i].instances[j]);
    }
  }
  
  return 0;
}

int instanceEquals(struct Linfo* a, struct Linfo *b){
  int ret = strcmp(a->assoc, b->assoc);
  if (ret == 0){
    if (a < b) return -1;
    if (a > b) return +1;
    if (a == b) return 0;
  }
  return ret;
}

int HwlocToLDMS(char* hwlocname, char* setname, char* metricname, int dottedstring){
  setname[0] = '\0';
  metricname[0] = '\0';

  struct Linfo* tr = NULL;

  char* pch;
  int i;
  pch = strtok(hwlocname, ".");

  while (pch != NULL){
    //first one is the treeroot
    if (tr == NULL){
      if (numlevels < 1){
	printf("Error: no hwloc info\n");
	return -1;
      }
      for (i = 0; i < hwloc[0].numinstances; i++){
	if (dottedstring){
	  if (!strcmp(hwloc[0].instances[i]->Lval, pch)){
	    tr = hwloc[0].instances[i];
	    break;
	  }
	} else {
	  char assoc[MAXSHORTNAME]; 
	  snprintf(assoc, MAXSHORTNAME, "%s%s", hwloc[0].assoc, hwloc[0].instances[i]->Lval);
	  if (!strcmp(assoc, pch)){
	    tr = hwloc[0].instances[i];
	    break;
	  }
	}
      }
      if (tr == NULL){
	printf("Error: cant find tree root for <%s>\n", hwlocname);
	return -1;
      }

      pch = strtok(NULL, ".");
      continue;
    }

    //these walk the tree
    int found = 0;
    for (i = 0; i < tr->numchildren; i++){
      if (dottedstring){
	if (!strcmp(tr->children[i]->Lval, pch)){
	  tr = tr->children[i];
	  found = 1;
	  break;
	}
      } else {
	char assoc[MAXSHORTNAME]; 
	snprintf(assoc, MAXSHORTNAME, "%s%s", tr->assoc, tr->children[i]->Lval);
	if (!strcmp(assoc, pch)){
	  tr = tr->children[i];
	  break;
	}
      }
    }
    if (found){
      pch = strtok(NULL, ".");
    } else {
      //is it a metric?
      int val = (dottedstring == 1? (atoi(pch) == MIBMETRICCATAGORYUID) : strcmp(pch, MIBMETRICCATAGORYNAME));
      if (!val){
	pch = strtok(NULL, ".");
	if (pch == NULL){
	  printf("Error: bad hwloc string <%s>\n", hwlocname);
	  return -1;
	}
	struct MetricInfo* mi = NULL;
	if (dottedstring){
	  //NOTE: the metric uid is also the metric index
	  int val = atoi(pch);
	  if (val < 0 || val > tr->nummetrics){
	    printf("Error: bad hwloc string <%s>\n", hwlocname);
	    return -1;
	  }
	  mi = tr->metrics[val];
	} else {
	  for (i = 0; i < tr->nummetrics; i++){
	    if (!strcmp(tr->metrics[i]->MIBmetricname,pch)){
	      mi = tr->metrics[i];
	      break;
	    }
	  }
	}
	if (mi == NULL){
	  printf("Error: bad hwloc string <%s>\n", hwlocname);
	  return -1;
	}
	if (mi->ldmsparent == NULL){
	  printf("WARNING: Not an ldms metric\n");
	  return -1;
	}
	snprintf(metricname, strlen(mi->ldmsname), "%s", mi->ldmsname);
	snprintf(setname, strlen(mi->ldmsparent->setname), "%s", mi->ldmsparent->setname);
	return 0;
      }

      //dont know what this is
      printf("Error: bad hwloc string <%s>\n", hwlocname);
      return -1;
    }
  }
  
  printf("Error: bad hwloc string <%s>\n", hwlocname);
  return -1;
}


//FIXME: make one where you dont have to parse thru the sets each time?
int LDMSToHwloc(char* setname, char* metricname, char* hwlocname, int dottedstring){

  hwlocname[0] = '\0';
  //given setname metricname get the hwlocname
  int i;

  int setnum = -1;
  for (i = 0; i < numsets; i++){
    if (!strcmp(sets[i].setname,setname)){
      setnum = i;
      break;
    }
  }
  if (setnum == -1){
    //    printf("Error: dont have set <%s>\n",setname);
    return -1;
  }

  //process this metric
  if (dottedstring){
    for (i = 0; i < sets[setnum].nummetrics; i++){
      if (!(strcmp(sets[setnum].metrics[i]->ldmsname,metricname))){
	snprintf(hwlocname,MAXBUFSIZE,"%s%d",
		 sets[setnum].metrics[i]->instance->metricdottedprefix, sets[setnum].metrics[i]->MIBmetricUID);
	return 0;
      }
    }
  } else {
    for (i = 0; i < sets[setnum].nummetrics; i++){
      if (!(strcmp(sets[setnum].metrics[i]->ldmsname,metricname))){
	snprintf(hwlocname,MAXBUFSIZE,"%s%s",
		 sets[setnum].metrics[i]->instance->metricprefix, sets[setnum].metrics[i]->MIBmetricname);
	return 0;
      }
    }
  }

  //  printf("Error: dont have metric <%s>\n",metricname);
  return -1;
};

int LDMSToHwlocWHost(char* hostname, char* setname, char* metricname, char* hwlocname, int dottedstring){
  char buf[MAXBUFSIZE];
  hwlocname[0] = '\0';

  int rc = LDMSToHwloc(setname, metricname, buf, dottedstring);
  if (rc < 0){
    return rc;
  }

  //FIXME: else replace the Machine0 with the hostname
  char* p = strstr(buf,".");
  if (p == NULL)
    return -1;

  snprintf(hwlocname,MAXBUFSIZE,"%s.%s",hostname,p+1);
  return 1;
};


int getInstanceMetricNames(char* orig, char* Lval, char* ldmsname, char* hwlocname){
  //the metric name MUST have an LVAL to replace, expect for where there is only 1 instance of that component involved
  //eg the metricname might be CPU(LVAL)_user_raw -> ldmsname of CPU3_user_raw and hwlocname of CPU_user_raw
  //dont currently have a good way to do functions of that

  //FIXME: this has not yet been tested for multiple replacements

  snprintf(ldmsname, MAXSHORTNAME, "%s", orig);
  snprintf(hwlocname, MAXSHORTNAME, "%s", orig);
  char *p;
  char buf[MAXSHORTNAME];

  //  printf("considering <%s>\n", orig);
  p = strstr(ldmsname, LVALPLACEHOLDER);
  while ( p != NULL){
    strncpy(buf, ldmsname, p-ldmsname);
    buf[p-ldmsname] = '\0';
    sprintf(buf+(p-ldmsname), "%s%s", Lval, p+strlen(LVALPLACEHOLDER));

    strncpy(ldmsname, buf, strlen(buf));
    ldmsname[strlen(buf)] = '\0';
    p = strstr(ldmsname, LVALPLACEHOLDER);
  }

  //  printf("considering <%s>\n", orig);
  p = strstr(hwlocname, LVALPLACEHOLDER);
  while ( p != NULL){
    strncpy(buf, hwlocname, p-hwlocname);
    buf[p-hwlocname] = '\0';
    sprintf(buf+(p-hwlocname), "%s", p+strlen(LVALPLACEHOLDER));

    strncpy(hwlocname, buf, strlen(buf));
    hwlocname[strlen(buf)] = '\0';
    p = strstr(hwlocname, LVALPLACEHOLDER);
  }

  return 0;
}


int parseMetricData(char* inputfile){
  //user metric data is in a file.
  //first line of the file is the hwloc component type
  //all subsequent lines are ldms setname/metricname

  //FIXME: check for repeats

  //if a line starts with # it will be skipped
  char buf[MAXBUFSIZE];
  char tempbuf[MAXBUFSIZE];
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

  while (fgets(tempbuf, (MAXBUFSIZE-1), fp) != NULL){
    int n =  sscanf(tempbuf,"%s",buf); //remove whitespace
    if (n!= 1){
      continue;
    }
    if (buf[0] == '#'){ //its a comment
      continue;
    }
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
	printf("Error: dont know assoc <%s>\n", assoc);
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
	char ldmsname[MAXSHORTNAME];
	char hwlocname[MAXSHORTNAME];
	int rc = getInstanceMetricNames(metricname, hwloc[comptypenum].instances[i]->Lval, ldmsname, hwlocname);
	if (rc != 0){
	  printf("Error: Cannot parse the metric regex. Exiting\n");
	  exit (-1);
	}
	//with the current constraints, GUARENTEED that each InstanceLDMSName will be unique and result in a new metric
	//that is, for example, cpu_util on the node is one metric, assoc with the node while
	//cpu_util for each core is each a different metric, assoc with the node
	//a metric can only be associated with a single instance.
	struct MetricInfo* mi = (struct MetricInfo*)malloc(sizeof(struct MetricInfo));
	snprintf(mi->ldmsname,MAXSHORTNAME,"%s",ldmsname);
	snprintf(mi->MIBmetricname,MAXSHORTNAME,"%s",hwlocname);

	//update the hw structs
	hwloc[comptypenum].instances[i]->metrics[hwloc[comptypenum].instances[i]->nummetrics] = mi;
	mi->MIBmetricUID = hwloc[comptypenum].instances[i]->nummetrics++;
	mi->instance = hwloc[comptypenum].instances[i];

	//update the ldms structs
	sets[setnum].metrics[sets[setnum].nummetrics++] = mi;
	mi->ldmsparent = &sets[setnum];

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

void  addComponent(char* hwlocAssocStr, int Lval, int Pval, char keys[MAXATTR][MAXSHORTNAME], int* attr, int numAttr){
  char prefix[1024];
  char dottedprefix[1024];
  int found = 0;
  int i;

  struct Linfo* li = (struct Linfo*)malloc(sizeof(struct Linfo));
  strncpy(li->assoc, hwlocAssocStr, MAXSHORTNAME);
  snprintf(li->Lval,5,"%d",Lval);
  snprintf(li->Pval,5,"%d",Pval);
  li->nummetrics = 0;
  li->numchildren = 0;

  // tree is really the current branch, used to build the prefix
  for (i=0; i<treesize; i++) {
    if ( !strncmp(tree[i]->assoc, hwlocAssocStr, MAXSHORTNAME) ) {
      tree[i] = li;
      treesize = i + 1;
      found = 1;
      break;
    }
  } 
  if (!found) {
    tree[treesize++] = li;
  }
  if (treesize > (MAXHWLOCLEVELS - 1)) {
    printf ("treesize exceeds limits\n");
    exit(0);
  }

  strcpy(prefix,"");
  strcpy(dottedprefix,"");
  for (i=0; i<treesize; i++) { 
    strcat(prefix,tree[i]->assoc);
    strcat(prefix,tree[i]->Lval); //do we want the Pval?
    strcat(prefix,".");
    //NOTE: when use the LVAL for the naming convention, it is easier to read but then there
    //are some missing components -- for example
    //NUMANode:
    //  Machine0.Socket0.NUMANode0. 0.0.0.
    //	Machine0.Socket0.NUMANode1. 0.0.1.
    //	Machine0.Socket1.NUMANode2. 0.1.2.
    //	Machine0.Socket1.NUMANode3. 0.1.3.
    // there is NO 0.1.0 NOR 0.1.1
    strcat(dottedprefix,tree[i]->Lval); 
    strcat(dottedprefix,".");
  }
  strncpy(li->prefix,prefix,MAXLONGNAME);
  strncpy(li->dottedprefix,dottedprefix,MAXLONGNAME);
  snprintf(li->metricprefix, MAXLONGNAME, "%s%s.", li->prefix,MIBMETRICCATAGORYNAME);
  snprintf(li->metricdottedprefix, MAXLONGNAME, "%s%d.", li->dottedprefix,MIBMETRICCATAGORYUID);

  li->parent = (treesize == 1 ? NULL: tree[treesize-2]);
  if (li->parent != NULL){
    li->parent->children[li->parent->numchildren++] = li;
  }

  //update the level interface as well 
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
    numlevels++; //this should be the same as the tree size
  }

  //add the attrs, if any as metrics
  for (i = 0; i < numAttr; i++){
    struct MetricInfo* mi = (struct MetricInfo*)malloc(sizeof(struct MetricInfo));
    snprintf(mi->ldmsname,MAXSHORTNAME,"%s","NONE");
    snprintf(mi->MIBmetricname,MAXSHORTNAME,"%s%s",HWLOCSTATICMETRICPREFIX,keys[i]); //note this is *not* an LDMS metric
    mi->ldmsparent = NULL;

    //update the hw structs
    li->metrics[li->nummetrics] = mi;
    mi->MIBmetricUID = li->nummetrics++;
    mi->instance = li;

    //NOTE: do NOT update the ldms structs
  }
  
  //add the component
  hwloc[found].instances[hwloc[found].numinstances++] = li;

}


void printComponents(int printMetrics){
  int i,j,k;
  printf("Components:\n");
  for (i = 0; i < numlevels; i++){
    printf("%s:\n", hwloc[i].assoc);
    for (j = 0; j < hwloc[i].numinstances; j++){
      printf("\t%-80s %-30s\n",hwloc[i].instances[j]->prefix, hwloc[i].instances[j]->dottedprefix);
      if (printMetrics){
	printf("\tMetrics:\n");
	for (k = 0; k < hwloc[i].instances[j]->nummetrics; k++){
	  printf("\t\t%-20s %30s%d\n",
		 hwloc[i].instances[j]->metrics[k]->MIBmetricname,
		 hwloc[i].instances[j]->metricdottedprefix, 
		 hwloc[i].instances[i]->metrics[k]->MIBmetricUID);
	  //there may be some that arent LDMS metrics
	}
      }
    }
  }
  printf("\n");
}


void printLDMSMetricsAsHwloc(){
  int i, j;
  printf("LDMS Metrics:\n");
  for (i = 0; i < numsets; i++){
    printf("%s:\n", sets[i].setname);
    for (j = 0; j < sets[i].nummetrics; j++){
      printf("\t%s%-20s %30s%d\n",
	     sets[i].metrics[j]->instance->metricprefix, sets[i].metrics[j]->MIBmetricname,
	     sets[i].metrics[j]->instance->metricdottedprefix, sets[i].metrics[j]->MIBmetricUID
	     );
    }
  }
  printf("\n");
}

void printTree(struct Linfo* tr){
  int i;
  if (tr == NULL){
    tr = hwloc[0].instances[0];
    printf("Tree:\n");
  }

  printf("\t%-120s %-20s (%d direct children) (%d metrics)\n", tr->prefix, tr->dottedprefix, tr->numchildren, tr->nummetrics);
  for (i = 0; i < tr->nummetrics; i++){
    printf("\t%s%s %20s%-2d\n", tr->metricprefix, tr->metrics[i]->MIBmetricname, tr->metricdottedprefix, tr->metrics[i]->MIBmetricUID);
  }

  for (i = 0; i < tr->numchildren; i++){
    printTree(tr->children[i]);
  }
}

int parseHwlocfile(char* file){
   FILE *fd;
   char *s;
   char lbuf[MAXLONGNAME];
   char hwlocAssocStr[MAXSHORTNAME];
   char keys[MAXATTR][MAXSHORTNAME];
   int attrib[MAXATTR];
   int numAttrib;
   int Lval, Pval;

   if (tree[0] != NULL){
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
       addComponent(hwlocAssocStr, Lval, Pval, keys, attrib, numAttrib);
     }
   } while (s);
   fclose (fd);
   
   return 0;
}

