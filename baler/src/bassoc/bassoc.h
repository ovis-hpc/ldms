/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-2016 Sandia Corporation. All rights reserved.
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
/**
 * \file bassoc.h
 * \brief Header file for Baler Associatione Rule Miner
 */
#ifndef __BASSOC_H
#define __BASSOC_H

#include <sys/queue.h>
#include <math.h>

#include "baler/btypes.h"
#include "baler/bset.h"
#include "baler/bhash.h"
#include "baler/bmvec.h"
#include "baler/bmqueue.h"
#include "baler/butils.h"

#include "bassocimg.h"
#include "baler/barray.h"

#define BASSOC_MAX_RULE_DEPTH 16

struct bassoc_conf {
	/* Image granularity */
	uint32_t spp;
	uint32_t npp;
};

struct bassoc_conf_handle {
	int fd;
	struct bassoc_conf *conf;
};

struct bassoc_rule {
	struct bmqueue_elm qelm;
	double conf;
	double sig;
	int formula_len;
	int formula[1+BASSOC_MAX_RULE_DEPTH];
};

struct bassoc_rule_index_entry {
	struct bassoc_rule *rule;
	LIST_ENTRY(bassoc_rule_index_entry) entry;
};

LIST_HEAD(bassoc_rule_index_list, bassoc_rule_index_entry);

/**
 * bassoc_rule_index is just a wrapper of ::bhash, with some utility functions
 * associated with it.
 *
 * hash['tgt,ev'] is a list of ::bassoc_rule_index_entry, referring to rules
 * that has target 'tgt' and has antecedent containing 'ev'.
 */
struct bassoc_rule_index {
	pthread_mutex_t mutex;
	struct bhash *hash;
};

struct bassocimgbin {
	char metric_name[256];
	int alloc_bin_len;
	int bin_len;
	struct {
		double lower_bound;
		struct bassocimg *img;
		struct barray *count_buff;
	} bin[0];
	/*
	 * NOTE: for bin[x].img is an image for [bin[x].lower_bound,
	 * bin[x+1].upper_bound). For x == 0, bin[0].lower_bound is -inf.
	 * The last bin, bin[bin_len - 1], will have lower_bound == inf.
	 */
};

#endif
