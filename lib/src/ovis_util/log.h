#ifndef ovis_util_log_h_seen
#define ovis_util_log_h_seen

/**
 * \brief Log levels
 */
#define OVIS_STR_WRAP(NAME) #NAME,
#define OVIS_LWRAP(NAME) OL_ ## NAME,

/* a command line alias of always is QUIET for back compatibility. */
#define LOGLEVELS(WRAP) \
        WRAP (DEBUG) \
        WRAP (INFO) \
        WRAP (ERROR) \
        WRAP (CRITICAL) \
        WRAP (ALWAYS) \
        WRAP (ENDLEVEL)

typedef enum ovis_loglevels {
        LOGLEVELS(OVIS_LWRAP)
} ovis_loglevels_t;

typedef void (*ovis_log_fn_t)(ovis_loglevels_t level, const char *fmt, ...);

#ifdef REFACTOR_SOON
/* array of strings matching the loglevels enum. null terminated. */
extern const char* ovis_loglevels_names[];

/* convert strings to array index value. QUIET is
  accepted as an alias of ALWAYS for documentation back compatibility.
  \returns the log level, or -1 if unrecognized string given.
*/
int ovis_str_to_level(const char *level_s);
int ovis_level_to_syslog(int level);

/* these go to ldms module headers unless we just run sed over
the code base once for all. */
#define ldms_str_to_level ovis_str_to_level
#define ldms_level_to_syslog ovis_level_to_syslog
#define loglevels_names ovis_loglevels_names

#endif

#endif /* ovis_util_log_h_seen */
