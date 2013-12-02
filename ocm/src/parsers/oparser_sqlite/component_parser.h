/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
/*
 * component_parser.h
 *
 *  Created on: Oct 14, 2013
 *      Author: nichamon
 */

#ifndef COMPONENT_PARSER_H_
#define COMPONENT_PARSER_H_

#include <sys/queue.h>
#include "oparser_util.h"


struct src_array {
	struct oparser_component **comp_array;
	int level;
	int num_comps;
	LIST_ENTRY(src_array) entry;
};
LIST_HEAD(src_list, src_array);

/**
 * \brief Initialize the Component Parser
 *
 */
void oparser_component_parser_init();

/**
 * \brief Parse the component definitions in \c conf_file
 *
 * \param[in]   conf_file   The configuration file
 *
 */
struct oparser_scaffold *oparser_parse_component_def(FILE *conff);

/**
 * \brief Create a scaffold representing the system
 *
 * \param[in]   type_list   The parsed component definition
 *
 * \return The component type list containing the relationship among the components
 */
struct oparser_scaffold *oparser_create_scaffold();

/**
 * \brief Print the scaffold \c list to the \c output_file
 *
 * \param[in]   list   The component type list obtaining from \c oparser_create_scaffold
 * \param[in]   outputf   the file to print the components
 *
 */
void oparser_print_component_def(struct oparser_component_type_list *list,
								FILE *outputf);

void oparser_print_scaffold(struct oparser_scaffold *scaffold, FILE *outf);

void oparser_scaffold_to_sqlite(struct oparser_scaffold *scaffold, sqlite3 *db);
#endif /* COMPONENT_PARSER_H_ */

