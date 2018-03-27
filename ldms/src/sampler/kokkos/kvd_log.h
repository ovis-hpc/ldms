#ifndef __KVD_LOG__
#define __KVD_LOG__

extern FILE *kvd_log_file;
extern int __kvd_log_level;

extern void __kvd_log(const char *fmt, ...);

enum kvd_log_level {
	KVD_LOG_LV_DEBUG = 0,
	KVD_LOG_LV_INFO,
	KVD_LOG_LV_WARN,
	KVD_LOG_LV_ERR,
	KVD_LOG_LV_QUIET,
	KVD_LOG_LV_LAST,
};

#define kvd_log(lv, fmt, ...) do {\
	if (KVD_LOG_LV_ ## lv >= __kvd_log_level) \
		__kvd_log(#lv ": " fmt "\n", ##__VA_ARGS__);\
} while(0)

#ifndef KVD_LOG_LEVEL
# define KVD_LOG_LEVEL KVD_LOG_LV_DEBUG
#endif

/**
 * \brief Print *DEBUG* message to a default log.
 */
#define kdebug(fmt, ...) kvd_log(DEBUG, fmt, ##__VA_ARGS__)

/**
 * \brief Print *INFO* message to a default log.
 */
#define kinfo(fmt, ...) kvd_log(INFO, fmt, ##__VA_ARGS__)

/**
 * \brief Print *WARN* message to a default log.
 */
#define kwarn(fmt, ...) kvd_log(WARN, fmt, ##__VA_ARGS__)

/**
 * \brief Print *ERR* message to a default log.
 */
#define kerr(fmt, ...) kvd_log(ERR, fmt, ##__VA_ARGS__)

/**
 * \brief Similar to perror(str), but print stuff to the log instead
 */
#define kerror(str) kerr("%s:%d, %s(), %s: errno: %d, msg: %m\n", \
			 __FILE__, __LINE__, __func__,		  \
			 str, errno)

/**
 * \brief Set log level.
 * \param level One of the enumeration ::kvd_log_level.
 */
void kvd_log_set_level(int level);

/**
 * \brief Same as kvd_log_set_level(), but with string option instead.
 *
 * Valid values of \c level include:
 *   - "DEBUG"
 *   - "INFO"
 *   - "WARN"
 *   - "ERROR"
 *   - "QUIET"
 *   - "0"
 *   - "1"
 *   - "2"
 *   - "3"
 *   - "4"
 *   - "D"
 *   - "I"
 *   - "W"
 *   - "E"
 *   - "Q"
 *
 * \retval 0 if OK.
 * \retval EINVAL if the input \c level is invalid.
 */
int kvd_log_set_level_str(const char *level);

/**
 * Get kvd_log level.
 *
 * \retval LVL The current log level.
 */
int kvd_log_get_level();

/**
 * \brief Set \a f as a log file.
 * \param f The log file. \a f should be opened.
 */
void kvd_log_set_file(FILE *f);

/**
 * \brief Open \a path for logging.
 * \param path The path to the file to be used for logging.
 * \return 0 on success.
 * \return -1 on failure. (errno should be set accordingly)
 */
int kvd_log_open(const char *path);

/**
 * \brief Close the log file.
 * This is essentially a wrapper to calling fclose(kvd_log_file).
 * Hence, errno will be set accordingly on failure.
 * \return 0 on success.
 * \return <b>EOF</b> on failure.
 */
int kvd_log_close();

/**
 * \brief Flush the log file.
 * This is essentially a wrapper to calling fflush(kvd_log_file).
 * Hence, errno will be set accordingly on failure.
 * \return 0 on success.
 * \return <b>EOF</b> on failure.
 */
int kvd_log_flush();

#endif
