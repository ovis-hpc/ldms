#ifndef __METRICMAP_H__
#define __METRICMAP_H__


#include <glib.h>
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
#define MAXMETRICSPERSET 500
#define MAXMETRICSPERCOMPONENT 5000

//hwloc
#define MAXHOSTS 20
#define MAXHWLOCLEVELS 10
#define MAXCOMPONENTSPERLEVEL 20
//NUMHWLOCLEVELS + 2 for the metrics
//#define DOTTEDSTRINGFORMAT "%s.%s.%s.%s.%s.%s.%s.%s.%s.%s.%s.%s";
//#define DOTTEDNUMBERFORMAT "%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d";


/**************************************************
 * see notes in metricmap.c. 
 ****************************************************/

//NOTE: the fixed size arrays are temporary to get something working.  in an actual
//implementation these would be replaced by something else. while some of the searches
//could be more effectively replaced by taking advantage of the L-ordering of the 
//components, it is not clear that the tree structure will be retained anyway (e.g., oid hash)

//hwloc
struct Linfo {
  char assoc[MAXSHORTNAME];
  char Lval[5]; //for a host assign the Lval (this is the val used in the oid for all components) //TODO: should this become an int?
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
  char Lval[5]; //TODO: should this become an int?
  int index;
};

struct HostInfo hosts[MAXHOSTS]; 
GHashTable *hostnameToHostOID;
GHashTable *hostOIDToHostIndex;

struct Linfo* tree[MAXHWLOCLEVELS]; //temporary for parsing hwlocfile

//enum if want specific line parses based upon these names (dont need to list an assoc if it is not default handling)
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

char knownassoc[MAXHWLOCLEVELS][MAXSHORTNAME]; //will build these as the hwloc info is read in. used for extracting info from dottedstrings

int getHwlocAssoc( char *assoc );
int cleanup();

//accessors
int getLDMSName(struct MetricInfo *mi, int hostoid, char* hostname, char* setname, char* metricname);
int getComponentOID(struct Linfo* linfo, unsigned int num, char* str, int dottedstring);
int getMetricOID(struct MetricInfo* minfo, unsigned int num, char* str, int dottedstring);
//int getComponentInfo(char* oid, struct LInfo** linfo, int* idx, int dottedstring); 
int getMetricInfo(char* oid, struct MetricInfo** minfo, int* idx, int dottedstring); 
int setMetricValueFromLDMS(char* hostname, char* setname, char* metricname, unsigned long val);
int setMetricValueFromOID(char* oid, unsigned long val, int dottedstring);
int getMetricValueFromOID(char* oid, unsigned long* val, int dottedstring);

//translations
int OIDToLDMS(char* hwlocname, char* hostname, char* setname, char* metricname, int dottedstring);
int LDMSToOID(char* hostname, char* setname, char* metricname, char* hwlocname, int dottedstring);

//building the structure
int getInstanceMetricNames(char* orig, char* Lval, char* ldmsname, char* hwlocname);
int parse_line(char* lbuf, char* comp_name, int* Lval, int* Pval, char keys[MAXATTR][MAXSHORTNAME], int* attr, int* numAttr);
void addComponent(char* hwlocAssocStr, int Lval, int Pval,  char keys[MAXATTR][MAXSHORTNAME], int* attr, int numAttr);
int parseHwlocData(char* file);
int parseMachineData(char* file);
int parseLDMSData(char* inputfile);
int parseData(char* machineFile, char *hwlocFile, char LDMSData[MAXHWLOCLEVELS][MAXBUFSIZE], int numLDMSData);

//diagnostic prints
void printComponent(struct Linfo*, int printMetrics, char*);
void printComponents(int printMetrics);
//void printLDMSMetricsAsOID();
void printTreeGuts(struct Linfo*, int);
void printTree(int);
void printMetric(struct MetricInfo*m, int, char*);

//these OIDS are the numeric oids
void printHostnameToHostOIDHash();
void printHostOIDToHostIndexHash();

#endif




















