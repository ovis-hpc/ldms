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
int numhosts = 0;
int treesize = 0;


//NOTE: that Machine0 will be everyhosts base host
//FIXME: assumes hostname/setname/metricname
//NOTE:using that the order the assocs are reached (and therefore assigned in the sets) is the depth.


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
  return 0;
}

/*
//keeping this because it walks the tree
int OIDToLDMS(char* hwlocname, char* setname, char* metricname, int dottedstring){
  setname[0] = '\0';
  metricname[0] = '\0';

  char buf[MAXBUFSIZE];
  snprintf(buf,MAXBUFSIZE,"%s",hwlocname);

  struct Linfo* tr = NULL;

  char* pch;
  int i;
  pch = strtok(buf, ".");

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
	snprintf(assoc, MAXSHORTNAME, "%s%s", tr->children[i]->assoc, tr->children[i]->Lval);
	if (!strcmp(assoc, pch)){
	  tr = tr->children[i];
	  found = 1;
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
	    printf("Error: bad hwloc string <%s> - bad metric uid\n", hwlocname);
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
	  printf("Error: bad hwloc string <%s> - metric does not exist \n", hwlocname);
	  return -1;
	}
	if (mi->ldmsparent == NULL){
	  //	  printf("WARNING: <%s> Not an ldms metric\n", hwlocname);
	  return -1;
	}
	snprintf(setname, strlen(mi->ldmsparent->setname)+1, "%s", mi->ldmsparent->setname);
	snprintf(metricname, strlen(mi->ldmsname)+1, "%s", mi->ldmsname);
	return 0;
      } //if(!val)

      //dont know what this is
      printf("Error: bad hwloc string <%s> -- <%s> not a metric\n", hwlocname, pch);
      return -1;
    }
  }
  
  printf("Error: bad hwloc string <%s> -- default \n", hwlocname);
  return -1;
}
*/

/*
//FIXME: how to handle the hostname???
//dont walk the string, but use the hwloc prefix depth to compare. 
int OIDToLDMS(char* oid, char* setname, char* metricname, int dottedstring){
  //oid needs to have the form ComponentOID/ComponentOIDString.METRICCATAGORYUID/METRICCATAGORYNAME.MIBmetricUID/MIBmetricname
  setname[0] = '\0';
  metricname[0] = '\0';

  printf("considering <%s>\n",oid);

  int i;
  int count = 0;
  char* seg[MAXHWLOCLEVELS+5];
  char *p = index(oid, '.');
  while (p!= NULL){
    seg[count] = p;
    for (i = 0; i < count; i++){
      printf("seg %d <%s>\n",i, seg[i]); //FIXME: start here
    }
    p = index(seg[count++], '.');
    if (count ==2 ) exit(-1);
  }
  if (count < 2){
    printf("Error: bad oid <%s>\n", oid);
    return -1;  
  }

  int levelassoc = count-2;
  struct Linfo* li = NULL;
  printf("level is %d\n", (count-2));
  if (dottedstring){
    for (i = 0; i < hwloc[levelassoc].numinstances; i++){
      if (!strncmp(hwloc[levelassoc].instances[i]->OID, oid, strlen(oid)-strlen(seg[count-2]))){
	li =  hwloc[levelassoc].instances[i];
	break;
      }
    }
  } else {
    for (i = 0; i < hwloc[levelassoc].numinstances; i++){
      if (!strncmp(hwloc[levelassoc].instances[i]->OIDString, oid, strlen(oid)-strlen(seg[count-2]))){
	li =  hwloc[levelassoc].instances[i];
	break;
      }
    }
  }

  if (li == NULL){
    printf("Error: bad oid <%s>\n", oid);
    return -1;
  }

  struct MetricInfo* mi = NULL;
  if (dottedstring){
    for (i = 0; i < li->nummetrics; i++){
      if (!strcmp(li->metrics[i]->OID, oid)){
	mi = li->metrics[i];
	break;
      }
    }
  } else {
    for (i = 0; i < li->nummetrics; i++){
      if (!strcmp(li->metrics[i]->OIDString, oid)){
	mi = li->metrics[i];
	break;
      }
    }
  }
  
  if (mi == NULL){
    printf("Error: bad oid <%s> (no metric) \n", oid);
    return -1;
  }

  if ((mi->ldmsparent == NULL) || strlen(mi->ldmsname) == 0){
    printf("Warning: not an LDMS metric\n");
    return -1;
  }

  snprintf(setname, strlen(mi->ldmsparent->setname), "%s", mi->ldmsparent->setname);
  snprintf(metricname, strlen(mi->ldmsname), "%s", mi->ldmsname);
  return 0;

}
*/

/*
//FIXME: make one where you dont have to parse thru the sets each time?
int LDMSToOID(char* setname, char* metricname, char* hwlocname, int dottedstring){

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
  for (i = 0; i < sets[setnum].nummetrics; i++){
    if (!(strcmp(sets[setnum].metrics[i]->ldmsname,metricname))){
      snprintf(hwlocname,MAXBUFSIZE,"%s", (dottedstring ? sets[setnum].metrics[i]->OID: sets[setnum].metrics[i]->OIDString));
      return 0;
    }
  }

  //  printf("Error: dont have metric <%s>\n",metricname);
  return -1;
};

int LDMSToOIDWHost(char* hostname, char* setname, char* metricname, char* hwlocname, int dottedstring){
  //replace the hwloc Machine0 with the actual hostname
  char buf[MAXBUFSIZE];
  hwlocname[0] = '\0';

  int rc = LDMSToOID(setname, metricname, buf, dottedstring);
  if (rc < 0){
    return rc;
  }

  char* p = strstr(buf,".");
  if (p == NULL)
    return -1;

  snprintf(hwlocname,MAXBUFSIZE,"%s.%s",hostname,p+1);
  return 1;
};
*/


void printMetric(struct MetricInfo* mi, int hostoid, char* prefix){
  int i;
  if (mi == NULL){
    return;
  }

  char oid[MAXLONGNAME], oidString[MAXLONGNAME];
  oid[0] = '\0';
  oidString[0] = '\0';
  int rc = getMetricOID(mi, hostoid, oid, 1);
  if (rc < 0){
    printf("Error: bad oid!\n");
    exit (-1);
  }
  rc = getMetricOID(mi, hostoid, oidString, 0);
  if (rc < 0){
    printf("Error: bad oid!\n");
    exit (-1);
  }

  for (i = 0; i < numhosts; i++){
    if (atoi(hosts[i].Lval) == hostoid){
      printf("%s %-120s %-30s (ldmsname: %s) (value %lu) \n", prefix, oidString, oid, mi->ldmsname, mi->values[i]);
      return;
    }
  }

  printf("Error: cant get value for this metric\n");

}


void printComponent(struct Linfo* tr, int hostoid, char* prefix){
  int i;

  if (tr == NULL){
    return;
  }

  char oid[MAXLONGNAME], oidString[MAXLONGNAME];
  oid[0] = '\0';
  oidString[0] = '\0';

  int rc = getComponentOID(tr, hostoid, oid, 1);
  if (rc < 0){
    printf("Error: bad oid!\n");
    exit (-1);
  }
  rc = getComponentOID(tr, hostoid, oidString, 0);
  if (rc < 0){
    printf("Error: bad oid!\n");
    exit (-1);
  }

  if (!strcmp(tr->assoc,"Machine")){
    for (i = 0; i < numhosts; i++){
      if (atoi(hosts[i].Lval) == hostoid){
	printf("%s (%-30s) %-120s %-30s\n", prefix, hosts[i].hostname, oidString, oid);
	return;
      }
    }
  } else {
    printf("%s %-120s %-30s\n", prefix, oidString, oid);
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
      printComponent(hwloc[0].instances[0], atoi(hosts[i].Lval),"\t");
      if (printMetrics){
	printf("\t\tMetrics:\n");
	for (k = 0; k < hwloc[0].instances[0]->nummetrics; k++){
	  printMetric(hwloc[0].instances[0]->metrics[k], atoi(hosts[i].Lval),"\t\t"); 	  //there may be some that arent LDMS metrics
	}
      }
    }
  }
  printf("Generic subcomponents:\n");
  for (i = 1; i < numlevels; i++){
    printf("%s:\n", hwloc[i].assoc);
    for (j = 0; j < hwloc[i].numinstances; j++){
      //use the first legitmate host Lval
      printComponent(hwloc[i].instances[j], 
		     (numhosts > 0 ? atoi(hosts[0].Lval): atoi(hwloc[0].instances[0]->Lval)),
		      "\t");
      if (printMetrics){
	printf("\t\tMetrics:\n");
	for (k = 0; k < hwloc[i].instances[j]->nummetrics; k++){
	  printMetric(hwloc[i].instances[j]->metrics[k],
		      (numhosts >0 ? atoi(hosts[0].Lval): atoi(hwloc[0].instances[0]->Lval)),
		       "\t\t"); 	  //there may be some that arent LDMS metrics
	}
      }
    }
  }
  printf("\n");
}

/*
//change this to print one or many trees. will have to figure out how to handle the ldmssets per hostname
void printLDMSMetricsAsOID(){
  int i, j;
  printf("LDMS Metrics:\n");
  for (i = 0; i < numsets; i++){
    printf("%s:\n", sets[i].setname);
    for (j = 0; j < sets[i].nummetrics; j++){
      char setname[MAXLONGNAME], metricname[MAXLONGNAME];
      int rc = OIDToLDMS(sets[i].metrics[j]->OIDString, setname, metricname, 0);
      printf("\t%-120s %-30s (LDMS<%s/%s>)\n", sets[i].metrics[j]->OIDString, sets[i].metrics[j]->OID,
	     (rc == 0? setname: "NONE"), (rc == 0? metricname: "NONE"));
    }
  }
  printf("\n");
}
*/

void printTreeGuts(struct Linfo* tr, int hostoid){
  int i;

  if (tr == NULL){
    return;
  }

  char oid[MAXLONGNAME], oidString[MAXLONGNAME];
  oid[0] = '\0';
  oidString[0] = '\0';
  int rc = getComponentOID(tr, hostoid, oid, 1);
  if (rc < 0){
    printf("Error: bad oid!\n");
    exit (-1);
  }
  rc = getComponentOID(tr, hostoid, oidString, 0);
  if (rc < 0){
    printf("Error: bad oid!\n");
    exit (-1);
  }
  printf("\t%-120s %-30s (%d direct children) (%d metrics)\n", oidString, oid, tr->numchildren, tr->nummetrics);
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
    printf("WARNING: no hosts!\n");
    printf("%s (%s):\n", hwloc[0].instances[0]->assoc, "NONAME");
    printTreeGuts(hwloc[0].instances[0], atoi(hwloc[0].instances[0]->Lval));
    return;
  }

  if (hostoid < 0){
    for (i = 0; i < numhosts; i++){
      printf("%s (%s):\n", hwloc[0].instances[0]->assoc, hosts[i].hostname);
      printTreeGuts(hwloc[0].instances[0], atoi(hosts[i].Lval));
    }
    return;
  } else {
    for (i = 0; i < numhosts; i++){
      if (atoi(hosts[i].Lval) == hostoid){
	printf("%s (%s):\n", hwloc[0].instances[0]->assoc, hosts[i].hostname);
	printTreeGuts(hwloc[0].instances[0], hostoid);
	return;
      }
    }
    printf("WARNING: no host <%d>\n", hostoid);
  }

  return;
}

int getMetricOID(struct MetricInfo* mi, unsigned int hostoid, char* str, int dottedstring){
  char temp[MAXLONGNAME];
  str[0] = '\0';
  temp[0] = '\0';

  if (mi == NULL){
    printf("Error: passed in a null metric\n");
    return -1;
  }

  if (mi->instance == NULL){
    printf("Error: no component for metric <%s>\n",mi->MIBmetricname);
    return -1;
  }

  int rc = getComponentOID(mi->instance, hostoid, temp, dottedstring);
  if (rc < 0){
    printf("Error: cant get component oid for metric <%s>\n", mi->MIBmetricname);
    return -1;
  }
  if (strlen(temp) == 0){
    printf("Error: cant get component oid for metric <%s>\n", mi->MIBmetricname);
    return -1;
  }
  
  if (dottedstring){
    snprintf(str, MAXLONGNAME, "%s.%d.%d",temp,MIBMETRICCATAGORYUID,mi->MIBmetricUID);
  } else {
    snprintf(str, MAXLONGNAME, "%s.%s.%s",temp,MIBMETRICCATAGORYNAME,mi->MIBmetricname);
  }
  return 0;
}

int getComponentOID(struct Linfo* linfo, unsigned int hostoid, char* str, int dottedstring){
  //WARNING: be sure str[0] = '\0' before you call this
  //FIXME: test to be sure str[0] was cleared before this call
  int i;

  if (linfo == NULL){
    printf("Error: passed in a null component\n");
    return -1;
  }

  //check if top of the tree
  if (linfo->parent == NULL){
    if (!strcmp(linfo->assoc,"Machine")){
      //print the actual component of interest

      //make sure this is a valid hostid
      for (i = 0; i < numhosts; i++){
	if (atoi(hosts[i].Lval) == hostoid){
	  char temp[MAXLONGNAME];
	  if (dottedstring){
	    if (strlen(str) > 0){
	      snprintf(temp, MAXLONGNAME, "%d.%s", hostoid, str);
	    } else {
	      snprintf(temp, MAXLONGNAME, "%d", hostoid);
	    }
	  } else {
	    if (strlen(str) > 0){
	      snprintf(temp, MAXLONGNAME, "%s%d.%s", linfo->assoc, hostoid, str);
	    } else {
	      snprintf(temp, MAXLONGNAME, "%s%d", linfo->assoc, hostoid);
	    }
	  }
	  snprintf(str, MAXLONGNAME, temp);
	  return 1;
	}
      }
      printf("Error: no host <%d>\n", hostoid);
      return -1;
    } else {
      printf("Error: bad tree (top component <%s>)!\n", linfo->assoc);
      return -1;
    }
  }

  //otherwise add myself
  char temp[MAXLONGNAME];
  if (dottedstring){
    if (strlen(str) > 0){
      snprintf(temp, MAXLONGNAME, "%s.%s", linfo->Lval, str);
    } else {
      snprintf(temp, MAXLONGNAME, "%s", linfo->Lval);
    }
  } else {
    if (strlen(str) > 0){
      snprintf(temp, MAXLONGNAME, "%s%s.%s", linfo->assoc, linfo->Lval, str);
    } else {
      snprintf(temp, MAXLONGNAME, "%s%s", linfo->assoc, linfo->Lval);
    }
  }
  snprintf(str, MAXLONGNAME, temp);

  return getComponentOID(linfo->parent, hostoid, str, dottedstring);      

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

  printf("Parsing ldmsdata file <%s>\n", inputfile);

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
	struct MetricInfo* mi = (struct MetricInfo*)malloc(sizeof(struct MetricInfo));
	snprintf(mi->ldmsname,MAXSHORTNAME,"%s",ldmsname);
	snprintf(mi->MIBmetricname,MAXSHORTNAME,"%s",hwlocname);

	//update the hw structs
	struct Linfo* li = hwloc[comptypenum].instances[i]; 
	li->metrics[li->nummetrics] = mi;
	mi->MIBmetricUID = li->nummetrics++;
	mi->instance = li;
	//no values yet

	//update the ldms structs
	sets[setnum].metrics[sets[setnum].nummetrics++] = mi;
	mi->ldmsparent = &sets[setnum];

	//	printf("adding LDMS metric\n");
	//	printMetric(mi,1);

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

void  addComponent(char* hwlocAssocStr, int Lval, int Pval, char keys[MAXATTR][MAXSHORTNAME], int* attr, int numAttr){
  int found = 0;
  int i,j;

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
    for (j = 0; j < MAXHOSTS; j++){
      mi->values[j] = attr[i];
    }
    mi->ldmsparent = NULL;

    //update the hw structs
    li->metrics[li->nummetrics] = mi;
    mi->MIBmetricUID = li->nummetrics++;
    mi->instance = li;

    //    printf("adding metric\n");
    //    printMetric(mi,1);

    //NOTE: do NOT update the ldms structs
  }
  
  //add the component
  hwloc[found].instances[hwloc[found].numinstances++] = li;

}


int parseData(char* machineFile, char *hwlocFile, char LDMSData[MAXHWLOCLEVELS][MAXBUFSIZE], int numLDMSData){
  int i;
  int rc;
  
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
      printf("should be parsing LDMS data <%s>\n", LDMSData[i]);
      rc =  parseLDMSData(LDMSData[i]);
      if (rc < 0){
	printf("Error parsing ldms data\n");
	cleanup();
	return rc;
      }
    }
  }

  printComponents(1);
  
  printTree(-1);

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
    snprintf(hosts[numhosts].Lval,5,"%s", Lval);
    hosts[numhosts].index =  numhosts; 
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
       //ignore the attributes for now
       addComponent(hwlocAssocStr, Lval, Pval, keys, attrib, numAttrib);
     }
   } while (s);
   fclose (fd);
   
   return 0;
}

