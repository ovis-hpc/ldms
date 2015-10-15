#ifndef ovis_util_spool_h_seen
#define ovis_util_spool_h_seen

#include "ovis_util/log.h"

/**
 Spawn spoolexec with arguments outfile (which must be closed) and
 spooldir (which must exist).
 Spoolexec is detached. After this call, caller should never
 refer to outfile again for any purpose, including recreating it.

 The least effort call is ovis_file_spool("/bin/mv","datafile","/tmp");
 Generally, it's a good idea to use a script wrapper instead of mv to
 catch and log filesystem errors and manage permissions. 
 The function ignores NULL inputs, logging an error of receiving any NULL.
*/
extern void ovis_file_spool(const char *spoolexec, const char *outfile,
	const char *spooldir, ovis_log_fn_t log);

#endif /* ovis_util_spool_h_seen */
