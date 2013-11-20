/**
 * \file butils.h
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 20, 2013
 * \ingroup butils
 *
 * \defgroup butils Baler Utility Functions.
 * \{
 *
 * \brief Baler utility functions.
 */

#ifndef _BUTILS_H
#define _BUTILS_H

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

/* *** LOGGING *** */
extern FILE *blog_file;

extern void __blog(const char *fmt, ...);

#define blog(lv, fmt, ...) __blog(#lv ": " fmt "\n", ##__VA_ARGS__);

#define BLOG_LV_DEBUG	0
#define BLOG_LV_INFO	1
#define BLOG_LV_WARN	2
#define BLOG_LV_ERR	3

#ifndef BLOG_LEVEL
# define BLOG_LEVEL BLOG_LV_DEBUG
#endif

/**
 * \brief Print *DEBUG* message to a default log.
 */
#if BLOG_LEVEL <= BLOG_LV_DEBUG
# define bdebug(fmt, ...) blog(DEBUG, fmt, ##__VA_ARGS__)
#else
# define bdebug(fmt, ...)
#endif

/**
 * \brief Print *INFO* message to a default log.
 */
#if BLOG_LEVEL <= BLOG_LV_INFO
# define binfo(fmt, ...) blog(INFO, fmt, ##__VA_ARGS__)
#else
# define binfo(fmt, ...)
#endif

/**
 * \brief Print *WARN* message to a default log.
 */
#if BLOG_LEVEL <= BLOG_LV_WARN
# define bwarn(fmt, ...) blog(WARN, fmt, ##__VA_ARGS__)
#else
# define bwarn(fmt, ...)
#endif

/**
 * \brief Print *ERR* message to a default log.
 */
#if BLOG_LEVEL <= BLOG_LV_ERR
# define berr(fmt, ...) blog(ERR, fmt, ##__VA_ARGS__)
#else
# define berr(fmt, ...)
#endif

/**
 * \brief Similar to perror(str), but print stuffs to Baler log instead.
 */
#define berror(str) berr("%s:%d, %s, %s: errno: %d, msg: %s\n", \
				__FILE__, __LINE__, __func__, \
				str, errno, sys_errlist[errno])
/**
 * \brief Set \a f as a log file.
 * \param f The log file. \a f should be opened.
 */
void blog_set_file(FILE *f);

/**
 * \brief Open \a path for logging.
 * \param path The path to the file to be used for logging.
 * \return 0 on success.
 * \return -1 on failure. (errno should be set accordingly)
 */
int blog_open_file(const char *path);

/**
 * \brief Close the log file.
 * This is essentially a wrapper to calling fclose(blog_file).
 * Hence, errno will be set accordingly on failure.
 * \return 0 on success.
 * \return <b>EOF</b> on failure.
 */
int blog_close_file();

/**
 * \brief Flush the log file.
 * This is essentially a wrapper to calling fflush(blog_file).
 * Hence, errno will be set accordingly on failure.
 * \return 0 on success.
 * \return <b>EOF</b> on failure.
 */
int blog_flush();

/**
 * \brief A convenient function checking if the given \a path exists.
 * \return 1 if the \a path exists. \a path is not necessary a file though.
 * \return 0 if the \a path does not exist.
 */
int bfile_exists(const char *path);

/**
 * \brief Check if the given path is a directory.
 * \return 1 if the given \a path exists and is directory.
 * \return 0 if the given path does not exist or is not a directory.
 */
int bis_dir(const char *path);

/**
 * This behave like mkdir -p, except that it will report errors even in the case
 * of directory/file exists.
 */
int bmkdir_p(const char *path, __mode_t mode);

/* **** Dynamic String **** */
struct bdstr {
	size_t alloc_len;
	size_t str_len;
	char *str;
};

/**
 * Allocate ::bdstr, with initial allocation length \c len.
 * \returns the pointer to the ::bdstr if success.
 * \returns NULL if failed.
 */
struct bdstr* bdstr_new(size_t len);

/**
 * Expand the \c str inside the ::bdstr \c bs to \c new_size.
 * \returns 0 on success, error code on error.
 */
int bdstr_expand(struct bdstr *bs, size_t new_size);

/**
 * Append \c str into \c bs->str, expand \c bs->str if necessary.
 * \returns 0 on success, error code on failure.
 */
int bdstr_append(struct bdstr *bs, const char *str);

#endif // _BUTILS_H
/**\}*/
