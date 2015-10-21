#ifndef ovis_util_log_h_seen
#define ovis_util_log_h_seen

/**
 * \brief Log levels
 */
#define OL_STR_WRAP(NAME) #NAME,
#define OL_LEVELWRAP(NAME) OL_ ## NAME,

/* a command line alias of user is ALWAYS or QUIET for back compatibility.
 */
#define LOGLEVELS(WRAP) \
        WRAP (DEBUG) \
        WRAP (INFO) \
        WRAP (WARN) \
        WRAP (ERROR) \
        WRAP (CRITICAL) \
        WRAP (USER) \
        WRAP (ENDLEVEL)

/**
 The USER level is for messages that are requested by the user.
 OL_NONE,OL_ENDLEVEL provide termination tests for iterating.
*/
typedef enum ovis_loglevels {
	OL_NONE = -1,
        LOGLEVELS(OL_LEVELWRAP)
} ovis_loglevels_t;

typedef void (*ovis_log_fn_t)(ovis_loglevels_t level, const char *fmt, ...);

/** Connect logger to file with logname given or syslog if name given contains
  exactly "syslog".
  \param progname argv[0] from the application, normally.
  \return 0 or errno detected if it fails.
 */
extern int ovis_log_init(const char *progname, const char *logname, const char *level);

/** Disconnect logger. */
extern void ovis_log_final();

/** Update the threshold for output. */
extern void ovis_log_level_set(ovis_loglevels_t level);

/** Get the current threshold for output. */
extern ovis_loglevels_t ovis_log_level_get();

/** Get level from a string, including various aliases. 
	\return OL_NONE if string not recognized.
*/
extern ovis_loglevels_t ol_to_level(const char *string);

/** Get canonical level name as string. */
extern const char * ol_to_string(ovis_loglevels_t level);

/** Get syslog int value for a level.
	\return LOG_CRIT for invalid inputs, NONE, & ENDLEVEL.
*/
extern int ol_to_syslog(ovis_loglevels_t level);

/** Log a message at given level. Defaults to stdout if 
 * ovis_log_init is not called successfully.
 */
extern void olog(ovis_loglevels_t level, const char *fmt, ...);

/** Log a debug message */
extern void oldebug(const char *fmt, ...);

/** Log an informational message */
extern void olinfo(const char *fmt, ...);

/** Log a condition that someone might worry about */
extern void olwarn(const char *fmt, ...);

/** Log a condition that someone should fix eventually */
extern void olerr(const char *fmt, ...);

/** Log startup/shutdown or a condition that someone should fix soon */
extern void olcrit(const char *fmt, ...);

/** Log data at the request of the user (aka always, supreme) */
extern void oluser(const char *fmt, ...);

/** Flush log file (if not syslog).
 * \return fflush result or 0.
 */
extern int olflush();

/** Close and reopen log after some other agent has renamed it out
of the way.  If log is syslog, does nothing. */
extern void ovis_logrotate();

/** Return the short string (from errno.h macro definitions) name that
 * matches the errno value. In the event of unknown or negative rc,
 * returns 'UNKNOWN(%d)" formatted result that will not change before
 * the next call to rcname.
 */
extern const char *ovis_rcname(int rc);

/**
 * \brief Similar to perror(str), but print stuffs to ovis log instead.
 */
#define oerror(str) olerr("%s:%d, %s(), %s: errno: %d, msg: %m\n", \
                                __FILE__, __LINE__, __func__, \
                                str, errno)

#define oerrorrc(str, rc) olerr("%s:%d, %s(), %s: errno(%d): %s\n", \
                                __FILE__, __LINE__, __func__, \
                                str, (rc), ovis_rcname(rc))

#endif /* ovis_util_log_h_seen */
