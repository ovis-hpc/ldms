#ifndef __METRICMAP_H__
#define __METRICMAP_H__


#include <stdio.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>


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

//temporary for parsing
struct Linfo tree[MAXHWLOCLEVELS];


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
int getInstanceLDMSName(char* orig, char* Lval, char* newname);
int parseMetricData(char* inputfile);
int parse_line(char* lbuf, char* comp_name, int* Lval, int* Pval, char keys[MAXATTR][MAXSHORTNAME], int* attr, int* numAttr);
void  addComponent(char* hwlocAssocStr, int Lval, int Pval);
int parseLDMSOutput(char* cmd);
void printComponents();
void printMetrics();
int setHwlocfile(char* file);

#endif
