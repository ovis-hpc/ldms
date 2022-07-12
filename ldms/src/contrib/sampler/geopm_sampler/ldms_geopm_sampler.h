/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022 Intel Corporation
 * Copyright (c) 2011-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2011-2018 Open Grid Computing, Inc. All rights reserved.
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
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
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


/**
 * \file ldms_geopm_sampler.h
 * \brief Interface header for GEOPM LDMS sampler
 */

#include <stdio.h>
#include <geopm_topo.h>

/**
 *  @brief logical map from geopm::string_format_e to LDMS value type.
 *
 *  The geopm_format_e enum is designed to be passed to
 *  geopm::string_format_type_to_function() to enable conversion of
 *  the PlatformIO "signal" double precision value into a string.  In
 *  this case the enum is being used to determine how to convert from
 *  a PlatformIO signal to an LDMS value type enum.  The
 *  ldms_geopm_sampler_metric_type_e enum provides this logical
 *  mapping from the geopm::string_format_e enum.
 */
enum ldms_geopm_sampler_metric_type_e {
	LDMS_GEOPM_SAMPLER_METRIC_TYPE_INVALID = -1,
	LDMS_GEOPM_SAMPLER_METRIC_TYPE_D64, /*!< geopm::STRING_FORMAT_DOUBLE */
	LDMS_GEOPM_SAMPLER_METRIC_TYPE_S32, /*!< geopm::STRING_FORMAT_INTEGER */
	LDMS_GEOPM_SAMPLER_METRIC_TYPE_S64, /*!< geopm::STRING_FORMAT_HEX */
	LDMS_GEOPM_SAMPLER_METRIC_TYPE_U64, /*!< geopm::STRING_FORMAT_RAW64 */
};

union ldms_value;

/**
 *  @brief Convert geopm_format_e into an LDMS value type
 *
 *  @param [in] metric_type: One of the
 *         ldms_geopm_sampler_metric_type_e enum values describing the
 *         type.
 *
 *  @return One of the ldms_value_type enums defined in ldms_core.h,
 *          LDMS_V_NONE if metric_type is not in
 *          ldms_geopm_sampler_metric_type_e.
 */
enum ldms_value_type ldms_geopm_sampler_value_type(enum ldms_geopm_sampler_metric_type_e metric_type);

/**
 *  @brief Convert geopm::PlatformIO sample into an LDMS value
 *
 *  @param [in] metric_type: One of the
 *         ldms_geopm_sampler_metric_type_e enum values describing the
 *         type.
 *
 *  @param [in] sample: Value sampled from geopm::PlatformIO.
 *
 *  @param [out] value: Output converted to ldms_value.
 *
 *  @return Zero on success, EINVAL if metric_type is not in
 *          ldms_geopm_sampler_metric_type_e.
 */
int ldms_geopm_sampler_convert_sample(enum ldms_geopm_sampler_metric_type_e metric_type,
				      double sample,
				      union ldms_value *value);

/**
 *  @brief Set logging function for GEOPM sampler plugin
 *
 *  @param [in] pf: Function pointer to logger used by GEOPM sampler
 *         plugin.
 */
void ldms_geopm_sampler_set_log(void (*pf)(int, const char *fms, ...));

/**
 *  @brief Open GEOPM Sampler configuration file with logging
 *
 *  @param [in] config_path: Path to configuration file to open.
 *
 *  @param [out] result: Pointer to opened file on success.
 *
 *  @return Zero on success, ENOENT on error.
 */
int ldms_geopm_sampler_open_config(const char *config_path, FILE **result);

/**
 *  @brief Parse next non-empty line of GEOPM sampler configuration
 *         file.
 *
 *  @param [in] fid: File descriptor to open GEOPM sampler config.
 *
 *  @param [out] signal_name: Parsed name of signal.  String must be
 *         of size NAME_MAX.
 *
 *  @param [out] domain_type: Parsed domain geopm_topo_domain_e as
 *         defined in geopm_topo.h.
 *
 *  @param [out] metric_name: String that combines the three request
 *         parameters with dashes.  String must be of size NAME_MAX.
 *
 *  @param [out] metric_type: Enum passed to
 *         ldms_geopm_sampler_convert_sample() to enable proper
 *         conversion to LDMS value.
 *
 *  @return Zero on success, EOF when end of file is reached.  Any
 *          other return value indicates a failure and the errno
 *          associated with the failure and errors are logged.
 */
int ldms_geopm_sampler_parse_line(FILE *fid,
				  char *signal_name,
				  enum geopm_domain_e *domain_type,
				  int *domain_idx,
				  char *metric_name,
				  enum ldms_geopm_sampler_metric_type_e *metric_type);

/**
 *  @brief Check that file parsing completed correctly
 *
 *  @details Log an error if entire file as not properly parsed.
 *
 *  @param [in] parse_rc: Non-zero return code from
 *         ldms_geopm_sampler_parse_line() which is expected to have
 *         value EOF when parsing completes.
 *
 *  @return Zero on success, parse_rc if not equal to EOF.
 */
int ldms_geopm_sampler_parse_check(int parse_rc);

/**
 *  @brief Register request from one line of configuration file
 *
 *  @param [in] signal_name: geopm::PlatformIO signal name.
 *
 *  @param [in] domain_type: geopm_domain_type_e enum value
 *
 *  @param [in] metric_type: One of the
 *         ldms_geopm_sampler_metric_type_e values for conversion.
 *
 *  @return Zero on success, EINVAL if parameters are invalid.
 */
int ldms_geopm_sampler_push_signal(const char *signal_name,
				   enum geopm_domain_e domain_type,
				   int domain_idx,
				   enum ldms_geopm_sampler_metric_type_e metric_type);

/**
 *  @brief Read all values into memory
 *
 *  @returns Zero on success, GEOPM error code on failure.
 */
int ldms_geopm_sampler_read_batch(void);

/**
 *  @brief Sample and log all values
 *
 *  @returns Zero on success, GEOPM error code on failure.
 */
int ldms_geopm_sampler_sample_batch(void);

/**
 *
 *  @brief Reset the geopm sampler state
 *
 */
void ldms_geopm_sampler_reset(void);
