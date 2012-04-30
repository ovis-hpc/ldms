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
#define MAXMETRICSPERCOMPONENT 2000

//hwloc
#define MAXHOSTS 20
#define MAXHWLOCLEVELS 10
#define MAXCOMPONENTSPERLEVEL 20


//hwloc
struct Linfo {
  char assoc[MAXSHORTNAME];
  char Lval[5]; //for a host assign the Lval (this is the val used in the oid for all components)
  char Pval[5]; 

  struct Linfo* parent;

  struct Linfo* children[MAXCOMPONENTSPERLEVEL]; //component children
  int numchildren;

  struct MetricInfo* metrics[MAXMETRICSPERCOMPONENT];  //the order of these will determine the value of the MIBmetricUID
  int nummetrics;
};

struct CompTypeInfo{
  char assoc[MAXSHORTNAME];
  struct Linfo* instances[MAXCOMPONENTSPERLEVEL];
  int numinstances;
};

struct CompTypeInfo hwloc[MAXHWLOCLEVELS]; //can access a level's instances by its assoc. note that the topmost level is Machine0 and not the hosts

//ldms
struct MetricInfo{
  struct SetInfo *ldmsparent; //ldmsset parent
  char ldmsname[MAXSHORTNAME];

  char MIBmetricname[MAXSHORTNAME];
  int MIBmetricUID; //There is no way for a user to assign a MIBmetricUID. This is the last component of the OID
  struct Linfo* instance; //in the current setup GUARENTEED that a metric belongs to a single component only

  unsigned long values[MAXHOSTS];
};

struct SetInfo{
  char setname[MAXLONGNAME];
  struct MetricInfo* metrics[MAXMETRICSPERSET];
  int nummetrics;
};

struct SetInfo sets[MAXSETS]; 

struct HostInfo {
  char hostname[MAXLONGNAME];
  char Lval[5];
  int index;
};

struct HostInfo hosts[MAXHOSTS]; 


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

//int getLDMSName(struct MetricInfo *mi);
int getComponentOID(struct Linfo* linfo, unsigned int num, char* str, int dottedstring);
int getMetricOID(struct MetricInfo* minfo, unsigned int num, char* str, int dottedstring);

int getInstanceMetricNames(char* orig, char* Lval, char* ldmsname, char* hwlocname);
int parse_line(char* lbuf, char* comp_name, int* Lval, int* Pval, char keys[MAXATTR][MAXSHORTNAME], int* attr, int* numAttr);
void addComponent(char* hwlocAssocStr, int Lval, int Pval,  char keys[MAXATTR][MAXSHORTNAME], int* attr, int numAttr);
int parseHwlocData(char* file);
int parseMachineData(char* file);
int parseLDMSData(char* inputfile);
int parseData(char* machineFile, char *hwlocFile, char LDMSData[MAXHWLOCLEVELS][MAXBUFSIZE], int numLDMSData);

void printComponent(struct Linfo*, int printMetrics, char*);
void printComponents(int printMetrics);
//void printLDMSMetricsAsOID();
void printTreeGuts(struct Linfo*, int);
void printTree(int);
void printMetric(struct MetricInfo*m, int, char*);

//int HwlocToLDMS_walk(char* hwlocname, char* setname, char* metricname, int dottedstring);
//int OIDToLDMS(char* hwlocname, char* setname, char* metricname, int dottedstring);
//int LDMSToOID(char* setname, char* metricname, char* hwlocname, int dottedstring);
//int LDMSToOIDWHost(char* hostname, char* setname, char* metricname, char* hwlocname, int dottedstring);

#endif




















