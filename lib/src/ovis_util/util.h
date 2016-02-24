/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-15 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-15 Sandia Corporation. All rights reserved.
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

#ifndef OVIS_UTIL_H_
#define OVIS_UTIL_H_

#include <stdio.h>
#include <unistd.h>

/*
 * This file aggregates simple utilities for key/value list parsing,
 * number scaling by suffix, and spawning a trackable child.
 */

struct attr_value {
	char *name;
	char *value;
};

/** 
 * Fixed upper bound in size key/value lists can be created and used
 * as demonstrated in the following example.
 * #include "ovis_util/util.h"
 * int max_pairs = 10;
 * int max_words = 10;_
 * struct attr_value_list *kvl = av_new(max_words);
 * struct attr_value_list *avl = av_new(max_pairs);
 * char *data = "a=b\nc=d e=f\n#g=h\n	i=j k\n";
 * tokenize(data,kvl,avl);
 * // producing list kvl containing "k" and
 * // avl containing pairs [a,b] [c,d] [e,f] [#g,h] [i,j]
 * char *value = av_value(avl,"w");
 * if (value != NULL) 
 * 	printf("unexpected element in avl\n");
 *
 * Handling an unbounded list size becomes a bounded problem by
 * estimating the maximum possible tokens as follows for string s.
 * int size = 1;
 * char *t = s;
 * while (t[0] != '\0') {
 * 	if (isspace(t[0])) size++;
 * 	t++;
 * }
 * struct attr_value_list *avl = av_new(size);
 * 
 */
struct attr_value_list {
	int size;
	int count;
	struct attr_value list[0];
};

/**
 * \brief Get the value of attribute \c name
 */
char *av_value(struct attr_value_list *av_list, char *name);

/**
 * \brief Get the attribute name in the \c av_list
 * at the index \c idx
 */
char *av_name(struct attr_value_list *av_list, int idx);

/**
 * \brief Get the value at the index \c idx
 */
char *av_value_at_idx(struct attr_value_list *av_list, int idx);

/**
 * \brief Tokenize the string \c cmd into the keyword list \c kwl
 * and the attribute list \c avl
 * \return nonzero if lists give are too small to hold all words or
 * pairs found in cmd, else 0.
 */
int tokenize(char *cmd, struct attr_value_list *kwl,
	     struct attr_value_list *avl);

/**
 * \brief Allocate memory for a new attribute list of size \c size
 */
struct attr_value_list *av_new(size_t size);

/**
 * \brief Parse the memory size
 * \return size of memory in bytes
 */
size_t ovis_get_mem_size(const char *s);

/**
 * \brief Fork and exec the given command with /bin/sh.
 *
 * This function call will fork and execute the given command with bash. It is
 * non-blocking, i.e. the function returns right away after fork (not after the
 * child process finished execution). Caller can handle child process
 * termination by handling SIGCHLD (see signal(7)), or using a variation of
 * wait(2).
 *
 * This utility function also close all inherited file descriptors (including
 * STDIN, STDOUT and STDERR).
 *
 * \retval pid The PID of the forked child.
 */
pid_t ovis_execute(const char *command);

#endif /* OVIS_UTIL_H_ */
