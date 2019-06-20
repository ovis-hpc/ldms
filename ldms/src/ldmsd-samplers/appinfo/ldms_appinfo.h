/* -*- c-basic-offset: 8 -*-
  * Copyright (c) 2018 National Technology & Engineering Solutions
  * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
  * NTESS, the U.S. Government retains certain rights in this software.
  *
  * This software is available to you under a choice of one of two
  * licenses.  You may choose to be licensed under the terms of the GNU
  * General Public License (GPL) Version 2, available from the file
  * COPYING in the main directory of this source tree, or the BSD-type
  * license below:
  *
  * Redistribution and use in source and binary forms, with or without
  * modification, are permitted provided that the following conditions
  * are met:
  *
  *      Redistributions of source code must retain the above copyright
  *      notice, this list of conditions and the following disclaimer.
  *
  *      Redistributions in binary form must reproduce the above
  *      copyright notice, this list of conditions and the following
  *      disclaimer in the documentation and/or other materials provided
  *      with the distribution.
  *
  *      Neither the name of Sandia nor the names of any contributors may
  *      be used to endorse or promote products derived from this software
  *      without specific prior written permission.
  *
  *      Modified source versions must be plainly marked as such, and
  *      must not be misrepresented as being the original software.
  *
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  */
#ifndef __ldmsapp_H__
#define __ldmsapp_H__

#include <inttypes.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LDMSAPP_DISABLE 0
#define LDMSAPP_ENABLE 1

/**
 * \mainpage LDMSA LIBRARY
 *
 * A \b Metric is a named and typed value. The contents of a metric set are
 * updated as a single entity.
 *
 * \section metrics_assign Assign metrics
 * \li The user have to ask for metrics in the job submission script before
 * running the application.
 * \li More detail will come soon
 *
 * \section use_library How to use the library
 * \li \b ldmsapp_initialize() initialize the communication connection between
 * the user application and LDMS. The user need to make a one time call to this
 *  function at the beginning of the application (main function).
 * \li The user can call the below functions in the application to update a
 * metric:
 * - ldmsapp_report_metric_u8()
 * - ldmsapp_report_metric_u16()
 * - ldmsapp_report_metric_u32()
 * - ldmsapp_report_metric_u64()
 * - ldmsapp_report_metric_s8()
 * - ldmsapp_report_metric_s16()
 * - ldmsapp_report_metric_s32()
 * - ldmsapp_report_metric_s64()
 * - ldmsapp_report_metric_float()
 * - ldmsapp_report_metric_double()
 * - ldmsapp_report_metric_str()
 *
 * \li \b ldmsapp_get_metric_names() called in the application to print
 * or retrieve the metric names
 *
 * \li \b ldmsapp_finialize() close the communication connection.
 */

/**
 * \brief Initialize ldmsa
 *
 * The function performs the following tasks.
 * - Set the mutex lock to synchronize the communication between the application
 * and LDMS.
 * - Establish the shared memory link.
 *
 * \param ldmsapp_enable	enable data sampling 0-disable 1-enable.
 * \param appid is a unique identifying number for this application
 * \param jobid is the job id # (0 means look at PBS_JOBID env var)
 * \param rank is the MPI (or other) process rank designation
 * \param silent is nonzero if want no stderr
 * \return 0 on success. negative value is returned on failure.
 */
extern int ldmsapp_initialize(int ldmsapp_enable, int appid, int jobid,
				int rank, int silent);

/**
 * \brief Finalize ldmsa
 *
 *
 * \return 0 on success. negative value is returned on failure.
 */
extern int ldmsapp_finalize();

/**
 * \brief Get metric names in an array.
 *
 *
 * \return array of strings on success. NULL is returned on failure.
 */
extern char** ldmsapp_get_metric_names();

/**
 * \brief Get metric name by id.
 *
 *
 * \return string on success. NULL is returned on failure.
 */
extern char* ldmsapp_get_metric_name_by_id(int metric_id);

/**
 * \brief Get metrics count.
 *
 *
 * \return number of metrics on success. negative value is returned on failure.
 */
extern int ldmsapp_get_metrics_count();

/**
 * \brief Set the value of a metric of type 8, 16, 32, or 64 unsigned integer.
 *
 * Set the specified metric \c metric_id to the value specified
 * by \c value.
 *
 * Example: write to the fifth metric value 10 of type uint64_t:
 * - ldmsapp_report_metric_u64(5, 10)
 *
 * \param metric_id	The metric index
 * \param value		The value.
 *
 * \return 0 on success. negative value is returned on failure.
 */
extern int ldmsapp_report_metric_u8(int metric_id, uint8_t value);
extern int ldmsapp_report_metric_u16(int metric_id, uint16_t value);
extern int ldmsapp_report_metric_u32(int metric_id, uint32_t value);
extern int ldmsapp_report_metric_u64(int metric_id, uint64_t value);

/**
 * \brief Set the value of a metric of type 8, 16, 32, or 64 signed integer.
 *
 * Set the specified metric \c metric_id to the value specified
 * by \c value.
 *
 * Example: write to the fifth metric value 10.4 of type int64_t:
 * - ldmsapp_report_metric_s64(5, 10.4)
 *
 * \param metric_id	The metric index
 * \param value		The value.
 *
 * \return 0 on success. negative value is returned on failure.
 */
extern int ldmsapp_report_metric_s8(int metric_id, int8_t value);
extern int ldmsapp_report_metric_s16(int metric_id, int16_t value);
extern int ldmsapp_report_metric_s32(int metric_id, int32_t value);
extern int ldmsapp_report_metric_s64(int metric_id, int64_t value);

/**
 * \brief Increment the value of a metric of type 8, 16, 32, or 64 unsigned
 * integer.
 *
 * Increment the specified metric \c metric_id with a value specified
 * by \c value.
 *
 * Example: increment the fifth metric value by 10 of type uint64_t:
 * - ldmsapp_report_metric_s64(5, 10)
 *
 * \param metric_id	The metric index
 * \param value		The value.
 *
 * \return 0 on success. negative value is returned on failure.
 */
extern int ldmsapp_inc_metric_u8(int metric_id, uint8_t value);
extern int ldmsapp_inc_metric_u16(int metric_id, uint16_t value);
extern int ldmsapp_inc_metric_u32(int metric_id, uint32_t value);
extern int ldmsapp_inc_metric_u64(int metric_id, uint64_t value);

/**
 * \brief Set the value of a metric of type float.
 *
 * Set the specified metric \c metric_id to the value specified
 * by \c value.
 *
 * Example: write to the fifth metric value 10.4 of type numerical:
 * - ldmsapp_report_metric_s64(5, 10.4)
 *
 * \param metric_id	The metric index
 * \param value		The value.
 *
 * \return 0 on success. negative value is returned on failure.
 */
extern int ldmsapp_report_metric_float(int metric_id, float value);

/**
 * \brief Set the value of a metric of type double.
 *
 * Set the specified metric \c metric_id to the value specified
 * by \c value.
 *
 * Example: write to the fifth metric value 10.4 of type numerical:
 * - ldmsapp_report_metric_s64(5, 10.4)
 *
 * \param metric_id	The metric index
 * \param value		The value.
 *
 * \return 0 on success. negative value is returned on failure.
 */
extern int ldmsapp_report_metric_double(int metric_id, double value);


/**
 * \brief Set the value of a metric of type string.
 *
 * Set the specified metric \c metric_id to the value specified
 * by \c value.
 *
 * Example: write to the fifth metric value "main start" of type numerical:
 * - ldmsapp_report_metric_s64(5, "main start")
 *
 * Note: the string maximum size is 255 characters, if larger size string exist
 *	it will be truncated to 255 characters.
 *
 * \param metric_id	The metric index
 * \param value		The value.
 *
 * \return 0 on success. negative value is returned on failure.
 */
extern int ldmsapp_report_metric_str(int metric_id, char *value);

#ifdef __cplusplus
}
#endif

#endif
