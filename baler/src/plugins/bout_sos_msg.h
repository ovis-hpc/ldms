/**
 * \file bout_sos_msg.h
 * \defgroup bout_sos_msg {
 * \ingroup bout_sos
 *
 * \brief An extension of ::bout_sos for Image data storage.
 *
 * bout_sos_msg requires only information stored in ::bout_sos, so this plugin
 * will not extend the SOS plugin instance structure. It does only override
 * \a start and \a process_output to correctly create SOS and process data to
 * store in the created SOS.
 */

#ifndef __BOUT_SOS_IMG_H
#define __BOUT_SOS_IMG_H

#include "bout_sos.h"

struct bout_sos_msg_plugin {
	struct bout_sos_plugin base; /** base structure. */
	sos_iter_t sos_iter; /** Iterator for seeking objects. */
	uint32_t delta_ts; /** ts granularity */
	uint32_t delta_node; /** node granularity */
};

/**
 * Process \a odata from SOS plugin instance, and put it into the storage.
 */
int bout_sos_msg_process_output(struct boutplugin *this,
				struct boutq_data *odata);

#endif
/** \} */
