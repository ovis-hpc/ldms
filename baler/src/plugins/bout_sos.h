/**
 * \file bout_sos.h
 * \author Narate Taerat.
 *
 * \defgroup bout_sos Baler Output to SOS.
 * \{
 * Baler Output to SOS implements Baler Output Plugin Interface (::boutplugin).
 *
 * This is similar to a virtual class in C++, having many basic functions
 * implemented, but leaving 'process_output' to be implemented by the inherited
 * classes. This is because SOS can support many kind of data. According to
 * current Baler requirement, we will have two kind of storage: messages and
 * images. Please see ::bout_sos_msg and ::bout_sos_img for more information.
 *
 * Please note that baler can have multiple SOS plugin instance loaded. Hence,
 * messages and several images (different resolutions) can be stored
 * simultaneously in different SOSs (taken care of by different plugin
 * instances.)
 *
 * \note bout_sos does not provide \a create_plugin_instance function because it
 * is virtual and should not have an instance.
 */
#ifndef __BOUT_SOS_H
#define __BOUT_SOS_H

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include "baler/bplugin.h"
#include "baler/boutput.h"

#include "sos/sos.h"

/**
 * Output plugin instance for sos, extending ::boutplugin.
 */
struct bout_sos_plugin {
	struct boutplugin base; /**< Base structure. */
	/* Additional data to the base. */
	sos_t sos; /**< SOS object. */
	char *sos_path; /**< SOS path, for reference. */
	pthread_mutex_t sos_mutex; /**< Mutex for sos. */
	struct sos_class_s *sos_class; /**< class describing sos objects */
};

/**
 * Baler Output SOS plugin configuration.
 *
 * In the configuration phase, baler core should give the path to the storage to
 * bout_sos_plugin. The plugin will then memorize the path, but does nothing
 * further.  Currently, bout_sos_plugin knows only ``path'' argument.
 *
 * \param this The plugin instance.
 * \param arg_head The head of argument list (an argument is a pair of strings,
 * 	::bpair_str).
 * \return 0 on success.
 * \return Error code on error.
 */
int bout_sos_config(struct bplugin *this, struct bpair_str_head *arg_head);

/**
 * Start function for ::bout_sos_plugin.
 * This will open sos, of path \a this->sos_path, and return.
 * \return 0 on sucess.
 * \return Error code on error.
 */
int bout_sos_start(struct bplugin *this);

/**
 * This is a destructor of the ::bout_sos_plugin instance.
 * \param this The plugin instance.
 * \return 0 on success.
 * \return Error code on error.
 */
int bout_sos_free(struct bplugin *this);

/**
 * Calling this function will stop \a this plugin instance.
 * Currently, this function only does sos_close.
 * \param this The plugin instance.
 * \return 0 on success.
 * \return Error code on error.
 */
int bout_sos_stop(struct bplugin *this);

/**
 * Plugin initialization.
 *
 * \param this The plugin instance pointer.
 * \param name The name of the plugin.
 * \returns 0 on success.
 * \returns Error code on error.
 */
int bout_sos_init(struct bout_sos_plugin *this, const char *name);

#endif
/** \} */
