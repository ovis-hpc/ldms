#include <stdio.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include "metricmap.h"

/*****************************************************************************
 * This is a demonstration library with the ability to take the hwloc heirarchical
 * node and below level information and the ldms data information and build an
 * SNMP-inspired naming convention for component and variable data for the combination
 * of the two. This library also provides calls for ldms <-> SNMP naming convention
 * conversion. This library also currently supports instantaneous storage and retrieval
 * of the data values. It NOT intended to be the final structure, but to serve as
 * an initial workable test code upon which we can determine requirements that
 * we want in our eventual goal. This does NOT support upper level connectivity
 * amongst the machines.
 ****************************************************************************/

//NOTE: that Machine0 will be everyhosts base host, with the actual machines in a separate array.
//NOTE: dottedstring as a parameter means the full string text in the oid

//FIXME: assumes hostname/setname/metricname. will want to support multiply slashed metricnames

//TODO: this is not the final data structure. tradeoffs of hash table vs walking the structure for large numbers.
//Possibly the metric structure should be per component type...
//NOTE: have to think about revisions for localOIDName and localOIDNum to support asymmetric trees and
//trees with more than 1 child component type.

//TODO: currently this is set up for one architecture in common to many machines. Extend this to
//support multiple architectures that will be in common for sets of machines.

//NOTE: Currently the metric UIDS are assigned in the order in which they appear in the
//metric data files as they are processed, thus they may change from run to run. Only the
//machines support user-defined Lvals (which can then be fixed from run-to-run). This
//is a deliberate choice as machine Lvals can then be nids and then low, contiguous numerical
//values will be used for metrics when only a few data vals are being collected


int numlevels = 0;
int numsets = 0;
int numhosts = 0;
int treesize = 0;

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
  //need only free the instances once
  int i, j, k;
  for (i = numlevels-1; i > numlevels; i--){
    for (j = 0; j < hwloc[i].numinstances; j++){
      for (k = 0; k < hwloc[i].instances[j]->nummetrics; k++){
	free(hwloc[i].instances[j]->metrics[k]);
      }
      free(hwloc[i].instances[j]);
    }
  }

  //all other structs are not dynamically allocated

  g_hash_table_destroy(hostnameToHostInfo);
  g_hash_table_destroy(hostOIDToHostInfo);
  g_hash_table_destroy(genericOIDToLinfo);
  g_hash_table_destroy(genericOIDToMetricInfo);
  return 0;
}

int getLDMSName(struct MetricInfo* mi, int hostoid, char* hostname, char* setname, char* metricname){

  if (mi == NULL){
    printf("Error: NULL input metric\n");
    return -1;
  }

  hostname[0] = '\0';
  setname[0] = '\0';
  metricname[0] = '\0';

  struct HostInfo* hi = NULL;
  hi = g_hash_table_lookup(hostOIDToHostInfo,&hostoid);
  if (hi == NULL){
    printf("Error: no host for <%d>\n", hostoid);
    return -1;
  }
  snprintf(hostname,MAXSHORTNAME,"%s",hi->hostname);

  if (mi->ldmsparent == NULL){
    //may not be an ldms metric
    //    printf("Error: bad parent for metric <%s>\n", mi->ldmsname);
    return -1;
  }
  snprintf(setname, MAXSHORTNAME, "%s", mi->ldmsparent->setname);
  snprintf(metricname, MAXSHORTNAME, "%s", mi->ldmsname);

  return 0;
}

int OIDToLDMS(char* genericoid, int hostoid, char* hostname, char* setname, char* metricname){
  
  if (strlen(genericoid) == 0){
    printf("Error: no generic oid\n");
    return -1;
  }

  hostname[0] = '\0';
  setname[0] = '\0';
  metricname[0] = '\0';

  struct HostInfo* hi = NULL;
  hi = g_hash_table_lookup(hostOIDToHostInfo,&hostoid);
  if (hi == NULL){
    printf("Error: no host for <%d>\n", hostoid);
    return -1;
  }
  snprintf(hostname,MAXSHORTNAME,"%s",hi->hostname);

  struct MetricInfo *mi = NULL;
  mi = g_hash_table_lookup(genericOIDToMetricInfo, genericoid);

  if (mi == NULL){
    printf("Error: no metric for <%s>\n", genericoid);
    return -1;
  }

  if (mi->ldmsparent == NULL){
    //may not be an ldms metric
    //    printf("Error: bad parent for metric <%s>\n", mi->ldmsname);
    return -1;
  }
  snprintf(setname, MAXSHORTNAME, "%s",mi->ldmsparent->setname);
  snprintf(metricname, MAXSHORTNAME, "%s",mi->ldmsname);
  return 0;
}


int LDMSToOID(char *hostname, char* setname, char* metricname, char* genericoid, int* hostoid){

  genericoid[0] = '\0';
  if ((strlen(hostname) == 0) || (strlen(setname) == 0) || (strlen(metricname) == 0)){
    return -1;
  }

  int i;

  struct HostInfo* hi = g_hash_table_lookup(hostnameToHostInfo, hostname);
  if (hi == NULL){
    printf("Error: no host <%s>\n", hostname);
    return -1;
  }

  *hostoid = hi->localOIDNum;

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

  for (i = 0; i < sets[setnum].nummetrics; i++){
    if (!(strcmp(sets[setnum].metrics[i]->ldmsname,metricname))){
      snprintf(genericoid,MAXSHORTNAME,"%s",sets[setnum].metrics[i]->genericOIDNum);
      return 0;
    }
  }

  printf("Error: dont have metric <%s>\n",metricname);
  return -1;

};

int setMetricValueFromLDMS(char* hostname, char* setname, char* metricname, unsigned long val){

  if ((strlen(hostname) == 0) || (strlen(setname) == 0) || (strlen(metricname) == 0)){
    return -1;
  }

  int i;

  struct HostInfo* hi = g_hash_table_lookup(hostnameToHostInfo, hostname);
  if (hi == NULL){
    printf("Error: no host <%s>\n", hostname);
    return -1;
  }

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

  for (i = 0; i < sets[setnum].nummetrics; i++){
    if (!(strcmp(sets[setnum].metrics[i]->ldmsname,metricname))){
      sets[setnum].metrics[i]->values[hi->index] = val;
      return 0;
    }
  }

  printf("Error: dont have metric <%s>\n",metricname);
  return -1;

};


int setMetricValueFromOID(char* genericoid, int hostoid, unsigned long val){
  if (strlen(genericoid) == 0){
    printf("Error: bad arg no oid\n");
    return -1;
  }

  struct MetricInfo *mi = NULL;
  
  mi = g_hash_table_lookup(genericOIDToMetricInfo, genericoid);
  if (mi == NULL){
    printf("Error: no metric for <%s>\n", genericoid);
    return -1;
  }

  struct HostInfo* hi = NULL;
  hi = g_hash_table_lookup(hostOIDToHostInfo, &hostoid);
  if (hi == NULL){
    printf("Error: no host for <%d>\n", hostoid);
    return -1;
  }
  
  mi->values[hi->index] = val;
  return 0;
};


int getMetricValueFromOID(char* genericoid, int hostoid, unsigned long *val){
  if (strlen(genericoid) == 0){
    printf("Error: bad arg no oid\n");
    return -1;
  }

  struct MetricInfo *mi = NULL;
  
  mi = g_hash_table_lookup(genericOIDToMetricInfo, genericoid);
  if (mi == NULL){
    printf("Error: no metric for <%s>\n", genericoid);
    return -1;
  }

  struct HostInfo* hi = NULL;
  hi = g_hash_table_lookup(hostOIDToHostInfo,&hostoid);
  if (hi == NULL){
    printf("Error: no host for <%d>\n", hostoid);
    return -1;
  }
  
  *val = mi->values[hi->index];
  return 0;

};


void printMetric(struct MetricInfo* mi, int hostoid, char* prefix){
  if (mi == NULL){
    return;
  }

  char hostname[MAXLONGNAME];
  char setname[MAXLONGNAME];
  char metricname[MAXLONGNAME];
  char longldmsname[MAXLONGNAME];


  //  if (OIDToLDMS(oid, hostname, setname, metricname, 0) == 0){
  if (getLDMSName(mi, hostoid, hostname, setname, metricname) == 0){
    snprintf(longldmsname,MAXLONGNAME, "%s/%s/%s",hostname,setname,metricname);
  } else {
    snprintf(longldmsname, MAXLONGNAME, "%s", mi->ldmsname); //should be NONE
  }

  struct HostInfo* hi = g_hash_table_lookup(hostOIDToHostInfo,&hostoid);
  if (hi == NULL){
    printf("Error: no host <%d>\n", hostoid);
    return;
  }

  char oidstring[MAXLONGNAME], oid[MAXSHORTNAME];
  snprintf(oidstring,MAXLONGNAME,"%s.%s",hosts[hi->index].localOIDString,mi->genericOIDString);
  snprintf(oid,MAXSHORTNAME,"%d.%s",hostoid,mi->genericOIDNum);
  printf("%s %-120s %-30s (ldmsname: %s) (value %lu) \n", prefix, oidstring, oid, longldmsname, mi->values[hi->index]);

}


void printComponent(struct Linfo* tr, int hostoid, char* prefix){
  if (tr == NULL){
    return;
  }

  struct HostInfo* hi = g_hash_table_lookup(hostOIDToHostInfo,&hostoid);
  if (hi == NULL){
    printf("Error: no host <%d>\n", hostoid);
    return;
  }

  if (tr->parent == NULL){
    //its the host
    printf("%s (%-30s) %-120s %-30d\n",
	   prefix, hi->hostname, hi->localOIDString, hostoid);
    return;
  } else {
    char oidstring[MAXLONGNAME], oid[MAXSHORTNAME];
    snprintf(oidstring,MAXLONGNAME,"%s.%s",hosts[hi->index].localOIDString,tr->genericOIDString);
    snprintf(oid,MAXSHORTNAME,"%d.%s",hostoid,tr->genericOIDNum);
    printf("%s %-120s %-30s\n", prefix, oidstring, oid);
  }

};


void printComponents(int printMetrics){
  int i,j,k;
  printf("\n\nComponents:\n");
  //first level print all the machines:
  if (numhosts < 1){
    printf("WARNING: no hosts!\n");
  } else {
    printf("%s:\n", hwloc[0].instances[0]->assoc);
    for (i = 0; i < numhosts; i++){
      printComponent(hwloc[0].instances[0], hosts[i].localOIDNum,"\t");
      if (printMetrics){
	printf("\t\tMetrics:\n");
	for (k = 0; k < hwloc[0].instances[0]->nummetrics; k++){
	  printMetric(hwloc[0].instances[0]->metrics[k], hosts[i].localOIDNum,"\t\t"); 	  //there may be some that arent LDMS metrics
	}
      }
      printf("\n");
    }
  }
  printf("Generic subcomponents:\n");
  for (i = 1; i < numlevels; i++){
    printf("%s:\n", hwloc[i].assoc);
    for (j = 0; j < hwloc[i].numinstances; j++){
      //use the first legitmate host oid
      printComponent(hwloc[i].instances[j], 
		     (numhosts > 0 ? hosts[0].localOIDNum: hwloc[0].instances[0]->localOIDNum),
		      "\t");
      if (printMetrics){
	printf("\t\tMetrics:\n");
	for (k = 0; k < hwloc[i].instances[j]->nummetrics; k++){
	  printMetric(hwloc[i].instances[j]->metrics[k],
		     (numhosts > 0 ? hosts[0].localOIDNum: hwloc[0].instances[0]->localOIDNum),
		       "\t\t"); 	  //there may be some that arent LDMS metrics
	}
      }
    }
  }
  printf("\n");
}


void printTreeGuts(struct Linfo* tr, int hostoid){
  int i;

  if (tr == NULL){
    return;
  }

  struct HostInfo* hi = g_hash_table_lookup(hostOIDToHostInfo,&hostoid);
  if (hi == NULL){
    printf("Error: no host <%d>\n", hostoid);
    return;
  }

  if (tr->parent == NULL){
    //its the host
    printf("\t%-120s %-30d (%d direct children) (%d metrics)\n", hi->localOIDString, hostoid, tr->numchildren, tr->nummetrics);
  } else {
    char oidstring[MAXLONGNAME], oid[MAXSHORTNAME];
    snprintf(oidstring,MAXLONGNAME,"%s.%s",hi->localOIDString,tr->genericOIDString);
    snprintf(oid,MAXSHORTNAME,"%d.%s",hostoid,tr->genericOIDNum);
    printf("\t%-120s %-30s (%d direct children) (%d metrics)\n", oidstring, oid, tr->numchildren, tr->nummetrics);
  }

  for (i = 0; i < tr->nummetrics; i++){
    printMetric(tr->metrics[i], hostoid, "\t");
  }

  for (i = 0; i < tr->numchildren; i++){
    printTreeGuts(tr->children[i], hostoid);
  }

}


void printTree(int hostoid){
  //if hostoid < 0 print all otherwise print one
  int i;

  printf("\n\nTrees:\n");
  if (numhosts < 1){
    //print the generic tree
    printf("WARNING: no hosts!\n");
    printf("%s (%s):\n", hwloc[0].instances[0]->assoc, "NONAME");
    printTreeGuts(hwloc[0].instances[0], hwloc[0].instances[0]->localOIDNum);
    return;
  }

  if (hostoid < 0){
    //print all
    for (i = 0; i < numhosts; i++){
      printf("%s (%s):\n", hwloc[0].instances[0]->assoc, hosts[i].hostname);
      printTreeGuts(hwloc[0].instances[0], hosts[i].localOIDNum);
    }
    return;
  } else {
    //print one
    struct HostInfo* hi = g_hash_table_lookup(hostOIDToHostInfo,&hostoid);
    if (hi == NULL){
      printf("Error: no host <%d>\n", hostoid);
      return;
    }
    printf("%s (%s):\n", hwloc[0].instances[0]->assoc, hi->hostname);
    printTreeGuts(hwloc[0].instances[0], hostoid);
    return;
  }
}

int getInstanceMetricNames(char* orig, char* Lval, char* ldmsname, char* hwlocname){
  //the metric name MUST have an LVAL to replace, expect for where there is only 1 instance of that component involved
  //eg the metricname might be CPU(LVAL)_user_raw -> ldmsname of CPU3_user_raw and hwlocname of CPU_user_raw
  //dont currently have a good way to do functions of that

  snprintf(ldmsname, MAXSHORTNAME, "%s", orig);
  snprintf(hwlocname, MAXSHORTNAME, "%s", orig);
  char *p;
  char buf[MAXSHORTNAME];

  //  printf("considering <%s>\n", orig);

  //FIXME: this has not yet been tested for multiple replacements
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


int parseLDMSData(char* inputfile){
  //user metric data is in a file.
  //first line of the file is the hwloc component type
  //all subsequent lines are ldms setname/metricname (no hostname, these will be common to all hosts)

  //FIXME: need a way for this to add all the metrics of a set without having to put them in the file
  //or have it invoke ldms_ls to get them...

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

  //  printf("Parsing ldmsdata file <%s>\n", inputfile);

  FILE *fp = fopen(inputfile, "r");
  if (fp == NULL){
    printf("Error: Can't open metric data file. exiting.\n");
    exit (-1);
  }

  while (fgets(tempbuf, (MAXBUFSIZE-1), fp) != NULL){
    int n =  sscanf(tempbuf,"%s",buf); //remove whitespace
    if (n != 1){
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
	struct MetricInfo* mi = addLDMSMetric(hwloc[comptypenum].instances[i], &sets[setnum], ldmsname, hwlocname);
	if (mi == NULL){
	  printf("Error: Cant add LDMSMetic <%s>\n", hwlocname);
	  exit (-1);
	}
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
	if (!strcmp(keys[*numAttr], "size")){
	  strcpy(keys[*numAttr], "cache_size");
	} else if (!strcmp(keys[*numAttr], "linesize")){
	  strcpy(keys[*numAttr], "cache_linesize");
	} else if (!strcmp(keys[*numAttr], "ways")){
	  strcpy(keys[*numAttr], "cache_ways");
	} 
	break;
      case Machine:
      case Socket:
      case NUMANode:
	if (!strcmp(keys[*numAttr], "total")){
	  strcpy(keys[*numAttr], "mem_total");
	} else if (!strcmp(keys[*numAttr], "local")){
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

  return 0;
}

struct MetricInfo* addLDMSMetric(struct Linfo* li, struct SetInfo* si, char* ldmsname, char* MIBmetricname){
  if ((li == NULL) || (si == NULL) || !strlen(ldmsname) || !strlen(MIBmetricname)){
    printf("Error: bad params to add LDMSMetric\n");
    return NULL;
  }

  struct MetricInfo* mi = (struct MetricInfo*)malloc(sizeof(struct MetricInfo));
  snprintf(mi->ldmsname,MAXSHORTNAME,"%s",ldmsname);
  snprintf(mi->MIBmetricname,MAXSHORTNAME,"%s",MIBmetricname);

  //update the hw structs
  li->metrics[li->nummetrics] = mi;
  mi->MIBmetricUID = li->nummetrics++;
  if (li->nummetrics >= MAXMETRICSPERCOMPONENT){
    printf("Error: too many metrics when adding <%s>\n", ldmsname);
    exit(-1);
  }
  if (strlen(li->genericOIDString) > 0){
    snprintf(mi->genericOIDString, MAXLONGNAME, "%s.%s.%s", li->genericOIDString,MIBMETRICCATAGORYNAME, mi->MIBmetricname);
    snprintf(mi->genericOIDNum, MAXSHORTNAME, "%s.%d.%d", li->genericOIDNum, MIBMETRICCATAGORYUID, mi->MIBmetricUID);
  } else {
    //dont use the generic oid of the machine
    snprintf(mi->genericOIDString, MAXLONGNAME, "%s.%s", MIBMETRICCATAGORYNAME, mi->MIBmetricname);
    snprintf(mi->genericOIDNum, MAXSHORTNAME, "%d.%d", MIBMETRICCATAGORYUID, mi->MIBmetricUID);
  } 

  g_hash_table_replace(genericOIDToMetricInfo,
		       (gpointer)mi->genericOIDNum,
		       (gpointer)mi);

  mi->instance = li;
  //no values yet

  //update the ldms structs
  si->metrics[si->nummetrics++] = mi;
  if (si->nummetrics >= MAXMETRICSPERSET){
    printf("Error: too many metrics for <%s> (%d)\n", si->setname,si->nummetrics);
    exit (-1);
  }
  mi->ldmsparent = si;

  return mi;
};


struct MetricInfo* addStaticMetric(struct Linfo* li, char* MIBmetricname){
  if ((li == NULL) || !strlen(MIBmetricname)){
    printf("Error: bad params to add LDMSMetric\n");
    return NULL;
  }

  struct MetricInfo* mi = (struct MetricInfo*)malloc(sizeof(struct MetricInfo));
  snprintf(mi->ldmsname,MAXSHORTNAME,"%s","NONE");
  snprintf(mi->MIBmetricname,MAXSHORTNAME,"%s%s",HWLOCSTATICMETRICPREFIX,MIBmetricname); //note this is *not* an LDMS metric
  mi->ldmsparent = NULL;

  //update the hw structs
  li->metrics[li->nummetrics] = mi;
  mi->MIBmetricUID = li->nummetrics++;
  if (li->nummetrics >= MAXMETRICSPERCOMPONENT){
    printf("Error: too many metrics when adding <%s>\n", MIBmetricname);
    exit(-1);
  }

  if (strlen(li->genericOIDString) > 0){
    snprintf(mi->genericOIDString, MAXLONGNAME, "%s.%s.%s", li->genericOIDString,MIBMETRICCATAGORYNAME, mi->MIBmetricname);
    snprintf(mi->genericOIDNum, MAXSHORTNAME, "%s.%d.%d", li->genericOIDNum, MIBMETRICCATAGORYUID, mi->MIBmetricUID);
  } else {
    //dont use the generic oid of the machine
    snprintf(mi->genericOIDString, MAXLONGNAME, "%s.%s", MIBMETRICCATAGORYNAME, mi->MIBmetricname);
    snprintf(mi->genericOIDNum, MAXSHORTNAME, "%d.%d", MIBMETRICCATAGORYUID, mi->MIBmetricUID);
  }

  g_hash_table_replace(genericOIDToMetricInfo,
		       (gpointer)mi->genericOIDNum,
		       (gpointer)mi);

  mi->instance = li;

  //NOTE: do NOT update the ldms structs

  return mi;
};


struct Linfo* addComponent(char* hwlocAssocStr, int Lval, int Pval){
  int found = 0;
  int i;

  struct Linfo* li = (struct Linfo*)malloc(sizeof(struct Linfo));
  strncpy(li->assoc, hwlocAssocStr, MAXSHORTNAME);
  snprintf(li->Lval,5,"%d",Lval);
  snprintf(li->Pval,5,"%d",Pval);
  snprintf(li->localOIDString, MAXSHORTNAME, "%s%d",li->assoc,atoi(li->Lval));
  li->localOIDNum = atoi(li->Lval);
  li->nummetrics = 0;
  li->numchildren = 0;
  li->parent = NULL;

  // tree is really the current branch
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

  //NOTE: when use the LVAL for the naming convention, it is easier to read but then there
  //are some missing components -- for example
  //NUMANode:
  //  Machine0.Socket0.NUMANode0. 0.0.0.
  //	Machine0.Socket0.NUMANode1. 0.0.1.
 //	Machine0.Socket1.NUMANode2. 0.1.2.
  //	Machine0.Socket1.NUMANode3. 0.1.3.
  // there is NO 0.1.0 NOR 0.1.1

  //there is only 1 parent
  struct Linfo *parent = (treesize == 1 ? NULL: tree[treesize-2]);
  if (parent != NULL){
    li->parent = parent;
    li->parent->children[li->parent->numchildren++] = li;
    if (strlen(li->parent->genericOIDString)){
      snprintf(li->genericOIDString,MAXLONGNAME,"%s.%s",li->parent->genericOIDString, li->localOIDString);
      snprintf(li->genericOIDNum,MAXSHORTNAME,"%s.%d",li->parent->genericOIDNum, li->localOIDNum);
    } else {
      //dont add the machine parent OID in the generic oid
      snprintf(li->genericOIDString,MAXLONGNAME,"%s", li->localOIDString);
      snprintf(li->genericOIDNum,MAXSHORTNAME,"%d", li->localOIDNum);
    }
    g_hash_table_replace(genericOIDToLinfo,
			 (gpointer)li->genericOIDNum,
			 (gpointer)li);
  } else {
    //none for the machine
    snprintf(li->genericOIDString,MAXLONGNAME,"%s", "");
    snprintf(li->genericOIDNum,MAXSHORTNAME,"%s", "");
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

  //add the component
  hwloc[found].instances[hwloc[found].numinstances++] = li;

  return li;

};


int parseData(char* machineFile, char *hwlocFile, char LDMSData[MAXHWLOCLEVELS][MAXBUFSIZE], int numLDMSData){
  int i;
  int rc;

  //FIXME: give these a more permanent home...
  hostnameToHostInfo = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
  hostOIDToHostInfo = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, NULL);
  genericOIDToLinfo = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
  genericOIDToMetricInfo = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, NULL);

  rc = parseHwlocData(hwlocFile);
  if (rc != 0){
    printf("Error parsing hwloc data\n");
    cleanup();
    return rc;
  }

  rc = parseMachineData(machineFile);
  if (rc != 0){
    printf("Error parsing machine data\n");
    cleanup();
    return rc;
  }

   
  //FIXME: is there some reason we cant have repeats???
  if (numLDMSData > 0  && (LDMSData != NULL)){
    for (i = 0; i < numLDMSData; i++){
      rc =  parseLDMSData(LDMSData[i]);
      if (rc < 0){
	printf("Error parsing ldms data\n");
	cleanup();
	return rc;
      }
    }
  }

  return rc;
  
}

int parseMachineData(char *file){
  //format will be hostname[space]Lval
  FILE *fd;
  char *s;
  char hostname[MAXLONGNAME];
  char Lval[5];
  char lbuf[MAXBUFSIZE];
  numhosts = 0;


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
    if (lbuf[0] == '#'){
      continue;
    }

    int rc = sscanf(lbuf,"%s %s",hostname, Lval);
    if (rc == 0){
      //blankline
      continue;
    }

    if (rc != 2){
      printf("Error: bad host format <%s>\n", lbuf);
      cleanup();
      return -1;
    }

    if (numhosts > (MAXHOSTS-1)){
      printf("Error: too many hosts\n");
      cleanup();
      return -1;
    }

    //makes the hosts but does not assoc with the existing Linfo
    snprintf(hosts[numhosts].hostname, MAXLONGNAME, "%s", hostname);
    hosts[numhosts].index =  numhosts; 
    snprintf(hosts[numhosts].localOIDString, MAXSHORTNAME, "%s", hostname);
    hosts[numhosts].localOIDNum = atoi(Lval);
    g_hash_table_replace(hostnameToHostInfo,
			 (gpointer)hosts[numhosts].hostname,
			 (gpointer)&(hosts[numhosts]));
    g_hash_table_replace(hostOIDToHostInfo,
			 (gpointer)&(hosts[numhosts].localOIDNum),
			 (gpointer)&(hosts[numhosts]));
    numhosts++;
  } while (s);
  fclose (fd);

  return 0;
}


int parseHwlocData(char* file){
   FILE *fd;
   char *s;
   char lbuf[MAXLONGNAME];
   char hwlocAssocStr[MAXSHORTNAME];
   char keys[MAXATTR][MAXSHORTNAME];
   int attrib[MAXATTR];
   int numAttrib;
   int Lval, Pval;
   int i,j;

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
     if (parse_line(lbuf, hwlocAssocStr, &Lval, &Pval, keys, attrib, &numAttrib) == 0){
       struct Linfo* li = addComponent(hwlocAssocStr, Lval, Pval);
       if (li == NULL){
	 printf("Error: cant add component <%s>\n", lbuf);
	 exit (-1);
       }
       for (i = 0; i < numAttrib; i++){
	 struct MetricInfo* mi = addStaticMetric(li, keys[i]);
	 if (mi == NULL){
	   printf("Error: cant add metric <%s><%d> to component\n", keys[i], i);
	   exit (-1);
	 }
	 //and set its values
	 for (j = 0; j < MAXHOSTS; j++){
	   mi->values[j] = attrib[i];
	 }
       }
     }
   } while (s);
   fclose (fd);

   return 0;
}

