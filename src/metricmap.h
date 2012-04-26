#ifndef __METRICMAP_H__
#define __METRICMAP_H__


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
#define MAXATTR 10
#define MIBMETRICCATAGORYUID 1000
#define MIBMETRICCATAGORYNAME "Metrics1000"
#define HWLOCSTATICMETRICPREFIX "HWLOCSTATIC_"
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

  struct Linfo* parent; //component parent
  char OIDString[MAXLONGNAME]; 
  char OID[MAXLONGNAME]; 
  struct Linfo* children[MAXCOMPONENTSPERLEVEL]; //component children
  int numchildren;

  struct MetricInfo* metrics[MAXMETRICSPERSET];  //the order of these will determine the value of the MIBmetricUID
  int nummetrics;
};

struct CompTypeInfo{
  char assoc[MAXSHORTNAME];
  struct Linfo* instances[MAXCOMPONENTSPERLEVEL];
  int numinstances;
};

struct CompTypeInfo hwloc[MAXHWLOCLEVELS]; //hwloc[0] is also the root of a tree


//ldms
struct MetricInfo{
  struct SetInfo *ldmsparent; //ldmsset parent
  char ldmsname[MAXSHORTNAME];
  char MIBmetricname[MAXSHORTNAME];
  int MIBmetricUID; //There is no way for a user to assign a MIBmetricUID. This is the last component of the OID
  struct Linfo* instance; //in the current setup GUARENTEED that a metric belongs to a single component only

  char OIDString[MAXLONGNAME]; 
  char OID[MAXLONGNAME]; 
};

struct SetInfo{
  char setname[MAXLONGNAME];
  struct MetricInfo* metrics[MAXMETRICSPERSET];
  int nummetrics;
};

struct SetInfo sets[MAXSETS];

struct Linfo* tree[MAXHWLOCLEVELS]; //temporary for parsing hwlocfile

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


int getHwlocAssoc( char *assoc );
int cleanup();
int componentInstanceEquals(struct Linfo* a, struct Linfo* b);
int componentInstanceMatchesOID(struct Linfo* a, char* oid);
int getInstanceLDMSName(char* orig, char* Lval, char* newname);
int getLDMSName(struct MetricInfo *mi);
int parseMetricData(char* inputfile);
int parse_line(char* lbuf, char* comp_name, int* Lval, int* Pval, char keys[MAXATTR][MAXSHORTNAME], int* attr, int* numAttr);
void addComponent(char* hwlocAssocStr, int Lval, int Pval,  char keys[MAXATTR][MAXSHORTNAME], int* attr, int numAttr);
int parseHwlocfile(char* file);

void printComponents(int printMetrics);
void printLDMSMetricsAsOID();
void printTree(struct Linfo*);
void printMetric(struct MetricInfo*m, int fullprint);

//int HwlocToLDMS_walk(char* hwlocname, char* setname, char* metricname, int dottedstring);
int OIDToLDMS(char* hwlocname, char* setname, char* metricname, int dottedstring);
int LDMSToOID(char* setname, char* metricname, char* hwlocname, int dottedstring);
int LDMSToOIDWHost(char* hostname, char* setname, char* metricname, char* hwlocname, int dottedstring);

#endif




















