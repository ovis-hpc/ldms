/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file mpi_profiler_func_list.c
 * \brief Routines to manage MPI functions and events
 */
#include <stdio.h>
#include <string.h>

#include "mpi_profiler.h"
#include "mpi_profiler_func_list.h"

/**
 * TODO create this using the generator
 */
void ldms_shm_mpi_init_func_names(
		ldms_shm_mpi_event_base_t *ldms_shm_mpi_base_events)
{
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Send_ID].desc,
			strlen("MPI_Send") + 1, "%s", "MPI_Send");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Recv_ID].desc,
			strlen("MPI_Recv") + 1, "%s", "MPI_Recv");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Get_count_ID].desc,
			strlen("MPI_Get_count") + 1, "%s", "MPI_Get_count");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Bsend_ID].desc,
			strlen("MPI_Bsend") + 1, "%s", "MPI_Bsend");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ssend_ID].desc,
			strlen("MPI_Ssend") + 1, "%s", "MPI_Ssend");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Rsend_ID].desc,
			strlen("MPI_Rsend") + 1, "%s", "MPI_Rsend");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Buffer_attach_ID].desc,
			strlen("MPI_Buffer_attach") + 1, "%s",
			"MPI_Buffer_attach");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Buffer_detach_ID].desc,
			strlen("MPI_Buffer_detach") + 1, "%s",
			"MPI_Buffer_detach");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Isend_ID].desc,
			strlen("MPI_Isend") + 1, "%s", "MPI_Isend");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ibsend_ID].desc,
			strlen("MPI_Ibsend") + 1, "%s", "MPI_Ibsend");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Issend_ID].desc,
			strlen("MPI_Issend") + 1, "%s", "MPI_Issend");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Irsend_ID].desc,
			strlen("MPI_Irsend") + 1, "%s", "MPI_Irsend");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Irecv_ID].desc,
			strlen("MPI_Irecv") + 1, "%s", "MPI_Irecv");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Wait_ID].desc,
			strlen("MPI_Wait") + 1, "%s", "MPI_Wait");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Test_ID].desc,
			strlen("MPI_Test") + 1, "%s", "MPI_Test");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Request_free_ID].desc,
			strlen("MPI_Request_free") + 1, "%s",
			"MPI_Request_free");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Waitany_ID].desc,
			strlen("MPI_Waitany") + 1, "%s", "MPI_Waitany");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Testany_ID].desc,
			strlen("MPI_Testany") + 1, "%s", "MPI_Testany");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Waitall_ID].desc,
			strlen("MPI_Waitall") + 1, "%s", "MPI_Waitall");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Testall_ID].desc,
			strlen("MPI_Testall") + 1, "%s", "MPI_Testall");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Waitsome_ID].desc,
			strlen("MPI_Waitsome") + 1, "%s", "MPI_Waitsome");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Testsome_ID].desc,
			strlen("MPI_Testsome") + 1, "%s", "MPI_Testsome");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Iprobe_ID].desc,
			strlen("MPI_Iprobe") + 1, "%s", "MPI_Iprobe");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Probe_ID].desc,
			strlen("MPI_Probe") + 1, "%s", "MPI_Probe");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Cancel_ID].desc,
			strlen("MPI_Cancel") + 1, "%s", "MPI_Cancel");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Test_cancelled_ID].desc,
			strlen("MPI_Test_cancelled") + 1, "%s",
			"MPI_Test_cancelled");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Send_init_ID].desc,
			strlen("MPI_Send_init") + 1, "%s", "MPI_Send_init");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Bsend_init_ID].desc,
			strlen("MPI_Bsend_init") + 1, "%s", "MPI_Bsend_init");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ssend_init_ID].desc,
			strlen("MPI_Ssend_init") + 1, "%s", "MPI_Ssend_init");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Rsend_init_ID].desc,
			strlen("MPI_Rsend_init") + 1, "%s", "MPI_Rsend_init");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Recv_init_ID].desc,
			strlen("MPI_Recv_init") + 1, "%s", "MPI_Recv_init");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Start_ID].desc,
			strlen("MPI_Start") + 1, "%s", "MPI_Start");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Startall_ID].desc,
			strlen("MPI_Startall") + 1, "%s", "MPI_Startall");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Sendrecv_ID].desc,
			strlen("MPI_Sendrecv") + 1, "%s", "MPI_Sendrecv");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Sendrecv_replace_ID].desc,
			strlen("MPI_Sendrecv_replace") + 1, "%s",
			"MPI_Sendrecv_replace");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_contiguous_ID].desc,
			strlen("MPI_Type_contiguous") + 1, "%s",
			"MPI_Type_contiguous");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_vector_ID].desc,
			strlen("MPI_Type_vector") + 1, "%s", "MPI_Type_vector");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_hvector_ID].desc,
			strlen("MPI_Type_hvector") + 1, "%s",
			"MPI_Type_hvector");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_indexed_ID].desc,
			strlen("MPI_Type_indexed") + 1, "%s",
			"MPI_Type_indexed");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_hindexed_ID].desc,
			strlen("MPI_Type_hindexed") + 1, "%s",
			"MPI_Type_hindexed");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_struct_ID].desc,
			strlen("MPI_Type_struct") + 1, "%s", "MPI_Type_struct");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Address_ID].desc,
			strlen("MPI_Address") + 1, "%s", "MPI_Address");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_extent_ID].desc,
			strlen("MPI_Type_extent") + 1, "%s", "MPI_Type_extent");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_size_ID].desc,
			strlen("MPI_Type_size") + 1, "%s", "MPI_Type_size");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_lb_ID].desc,
			strlen("MPI_Type_lb") + 1, "%s", "MPI_Type_lb");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_ub_ID].desc,
			strlen("MPI_Type_ub") + 1, "%s", "MPI_Type_ub");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_commit_ID].desc,
			strlen("MPI_Type_commit") + 1, "%s", "MPI_Type_commit");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_free_ID].desc,
			strlen("MPI_Type_free") + 1, "%s", "MPI_Type_free");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Get_elements_ID].desc,
			strlen("MPI_Get_elements") + 1, "%s",
			"MPI_Get_elements");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Pack_ID].desc,
			strlen("MPI_Pack") + 1, "%s", "MPI_Pack");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Unpack_ID].desc,
			strlen("MPI_Unpack") + 1, "%s", "MPI_Unpack");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Pack_size_ID].desc,
			strlen("MPI_Pack_size") + 1, "%s", "MPI_Pack_size");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Barrier_ID].desc,
			strlen("MPI_Barrier") + 1, "%s", "MPI_Barrier");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Bcast_ID].desc,
			strlen("MPI_Bcast") + 1, "%s", "MPI_Bcast");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Gather_ID].desc,
			strlen("MPI_Gather") + 1, "%s", "MPI_Gather");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Gatherv_ID].desc,
			strlen("MPI_Gatherv") + 1, "%s", "MPI_Gatherv");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Scatter_ID].desc,
			strlen("MPI_Scatter") + 1, "%s", "MPI_Scatter");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Scatterv_ID].desc,
			strlen("MPI_Scatterv") + 1, "%s", "MPI_Scatterv");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Allgather_ID].desc,
			strlen("MPI_Allgather") + 1, "%s", "MPI_Allgather");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Allgatherv_ID].desc,
			strlen("MPI_Allgatherv") + 1, "%s", "MPI_Allgatherv");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Alltoall_ID].desc,
			strlen("MPI_Alltoall") + 1, "%s", "MPI_Alltoall");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Alltoallv_ID].desc,
			strlen("MPI_Alltoallv") + 1, "%s", "MPI_Alltoallv");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Alltoallw_ID].desc,
			strlen("MPI_Alltoallw") + 1, "%s", "MPI_Alltoallw");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Exscan_ID].desc,
			strlen("MPI_Exscan") + 1, "%s", "MPI_Exscan");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Reduce_ID].desc,
			strlen("MPI_Reduce") + 1, "%s", "MPI_Reduce");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Op_create_ID].desc,
			strlen("MPI_Op_create") + 1, "%s", "MPI_Op_create");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Op_free_ID].desc,
			strlen("MPI_Op_free") + 1, "%s", "MPI_Op_free");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Allreduce_ID].desc,
			strlen("MPI_Allreduce") + 1, "%s", "MPI_Allreduce");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Reduce_scatter_ID].desc,
			strlen("MPI_Reduce_scatter") + 1, "%s",
			"MPI_Reduce_scatter");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Scan_ID].desc,
			strlen("MPI_Scan") + 1, "%s", "MPI_Scan");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Group_size_ID].desc,
			strlen("MPI_Group_size") + 1, "%s", "MPI_Group_size");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Group_rank_ID].desc,
			strlen("MPI_Group_rank") + 1, "%s", "MPI_Group_rank");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Group_translate_ranks_ID].desc,
			strlen("MPI_Group_translate_ranks") + 1, "%s",
			"MPI_Group_translate_ranks");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Group_compare_ID].desc,
			strlen("MPI_Group_compare") + 1, "%s",
			"MPI_Group_compare");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_group_ID].desc,
			strlen("MPI_Comm_group") + 1, "%s", "MPI_Comm_group");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Group_union_ID].desc,
			strlen("MPI_Group_union") + 1, "%s", "MPI_Group_union");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Group_intersection_ID].desc,
			strlen("MPI_Group_intersection") + 1, "%s",
			"MPI_Group_intersection");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Group_difference_ID].desc,
			strlen("MPI_Group_difference") + 1, "%s",
			"MPI_Group_difference");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Group_incl_ID].desc,
			strlen("MPI_Group_incl") + 1, "%s", "MPI_Group_incl");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Group_excl_ID].desc,
			strlen("MPI_Group_excl") + 1, "%s", "MPI_Group_excl");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Group_range_incl_ID].desc,
			strlen("MPI_Group_range_incl") + 1, "%s",
			"MPI_Group_range_incl");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Group_range_excl_ID].desc,
			strlen("MPI_Group_range_excl") + 1, "%s",
			"MPI_Group_range_excl");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Group_free_ID].desc,
			strlen("MPI_Group_free") + 1, "%s", "MPI_Group_free");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_size_ID].desc,
			strlen("MPI_Comm_size") + 1, "%s", "MPI_Comm_size");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_rank_ID].desc,
			strlen("MPI_Comm_rank") + 1, "%s", "MPI_Comm_rank");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_compare_ID].desc,
			strlen("MPI_Comm_compare") + 1, "%s",
			"MPI_Comm_compare");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_dup_ID].desc,
			strlen("MPI_Comm_dup") + 1, "%s", "MPI_Comm_dup");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_dup_with_info_ID].desc,
			strlen("MPI_Comm_dup_with_info") + 1, "%s",
			"MPI_Comm_dup_with_info");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_create_ID].desc,
			strlen("MPI_Comm_create") + 1, "%s", "MPI_Comm_create");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_split_ID].desc,
			strlen("MPI_Comm_split") + 1, "%s", "MPI_Comm_split");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_free_ID].desc,
			strlen("MPI_Comm_free") + 1, "%s", "MPI_Comm_free");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_test_inter_ID].desc,
			strlen("MPI_Comm_test_inter") + 1, "%s",
			"MPI_Comm_test_inter");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_remote_size_ID].desc,
			strlen("MPI_Comm_remote_size") + 1, "%s",
			"MPI_Comm_remote_size");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_remote_group_ID].desc,
			strlen("MPI_Comm_remote_group") + 1, "%s",
			"MPI_Comm_remote_group");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Intercomm_create_ID].desc,
			strlen("MPI_Intercomm_create") + 1, "%s",
			"MPI_Intercomm_create");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Intercomm_merge_ID].desc,
			strlen("MPI_Intercomm_merge") + 1, "%s",
			"MPI_Intercomm_merge");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Keyval_create_ID].desc,
			strlen("MPI_Keyval_create") + 1, "%s",
			"MPI_Keyval_create");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Keyval_free_ID].desc,
			strlen("MPI_Keyval_free") + 1, "%s", "MPI_Keyval_free");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Attr_put_ID].desc,
			strlen("MPI_Attr_put") + 1, "%s", "MPI_Attr_put");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Attr_get_ID].desc,
			strlen("MPI_Attr_get") + 1, "%s", "MPI_Attr_get");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Attr_delete_ID].desc,
			strlen("MPI_Attr_delete") + 1, "%s", "MPI_Attr_delete");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Topo_test_ID].desc,
			strlen("MPI_Topo_test") + 1, "%s", "MPI_Topo_test");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Cart_create_ID].desc,
			strlen("MPI_Cart_create") + 1, "%s", "MPI_Cart_create");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Dims_create_ID].desc,
			strlen("MPI_Dims_create") + 1, "%s", "MPI_Dims_create");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Graph_create_ID].desc,
			strlen("MPI_Graph_create") + 1, "%s",
			"MPI_Graph_create");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Graphdims_get_ID].desc,
			strlen("MPI_Graphdims_get") + 1, "%s",
			"MPI_Graphdims_get");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Graph_get_ID].desc,
			strlen("MPI_Graph_get") + 1, "%s", "MPI_Graph_get");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Cartdim_get_ID].desc,
			strlen("MPI_Cartdim_get") + 1, "%s", "MPI_Cartdim_get");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Cart_get_ID].desc,
			strlen("MPI_Cart_get") + 1, "%s", "MPI_Cart_get");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Cart_rank_ID].desc,
			strlen("MPI_Cart_rank") + 1, "%s", "MPI_Cart_rank");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Cart_coords_ID].desc,
			strlen("MPI_Cart_coords") + 1, "%s", "MPI_Cart_coords");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Graph_neighbors_count_ID].desc,
			strlen("MPI_Graph_neighbors_count") + 1, "%s",
			"MPI_Graph_neighbors_count");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Graph_neighbors_ID].desc,
			strlen("MPI_Graph_neighbors") + 1, "%s",
			"MPI_Graph_neighbors");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Cart_shift_ID].desc,
			strlen("MPI_Cart_shift") + 1, "%s", "MPI_Cart_shift");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Cart_sub_ID].desc,
			strlen("MPI_Cart_sub") + 1, "%s", "MPI_Cart_sub");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Cart_map_ID].desc,
			strlen("MPI_Cart_map") + 1, "%s", "MPI_Cart_map");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Graph_map_ID].desc,
			strlen("MPI_Graph_map") + 1, "%s", "MPI_Graph_map");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Get_processor_name_ID].desc,
			strlen("MPI_Get_processor_name") + 1, "%s",
			"MPI_Get_processor_name");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Get_version_ID].desc,
			strlen("MPI_Get_version") + 1, "%s", "MPI_Get_version");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Get_library_version_ID].desc,
			strlen("MPI_Get_library_version") + 1, "%s",
			"MPI_Get_library_version");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Errhandler_create_ID].desc,
			strlen("MPI_Errhandler_create") + 1, "%s",
			"MPI_Errhandler_create");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Errhandler_set_ID].desc,
			strlen("MPI_Errhandler_set") + 1, "%s",
			"MPI_Errhandler_set");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Errhandler_get_ID].desc,
			strlen("MPI_Errhandler_get") + 1, "%s",
			"MPI_Errhandler_get");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Errhandler_free_ID].desc,
			strlen("MPI_Errhandler_free") + 1, "%s",
			"MPI_Errhandler_free");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Error_string_ID].desc,
			strlen("MPI_Error_string") + 1, "%s",
			"MPI_Error_string");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Error_class_ID].desc,
			strlen("MPI_Error_class") + 1, "%s", "MPI_Error_class");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Wtime_ID].desc,
			strlen("MPI_Wtime") + 1, "%s", "MPI_Wtime");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Wtick_ID].desc,
			strlen("MPI_Wtick") + 1, "%s", "MPI_Wtick");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Init_ID].desc,
			strlen("MPI_Init") + 1, "%s", "MPI_Init");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Finalize_ID].desc,
			strlen("MPI_Finalize") + 1, "%s", "MPI_Finalize");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Initialized_ID].desc,
			strlen("MPI_Initialized") + 1, "%s", "MPI_Initialized");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Abort_ID].desc,
			strlen("MPI_Abort") + 1, "%s", "MPI_Abort");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Pcontrol_ID].desc,
			strlen("MPI_Pcontrol") + 1, "%s", "MPI_Pcontrol");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Close_port_ID].desc,
			strlen("MPI_Close_port") + 1, "%s", "MPI_Close_port");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_accept_ID].desc,
			strlen("MPI_Comm_accept") + 1, "%s", "MPI_Comm_accept");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_connect_ID].desc,
			strlen("MPI_Comm_connect") + 1, "%s",
			"MPI_Comm_connect");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_disconnect_ID].desc,
			strlen("MPI_Comm_disconnect") + 1, "%s",
			"MPI_Comm_disconnect");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_get_parent_ID].desc,
			strlen("MPI_Comm_get_parent") + 1, "%s",
			"MPI_Comm_get_parent");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_join_ID].desc,
			strlen("MPI_Comm_join") + 1, "%s", "MPI_Comm_join");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Lookup_name_ID].desc,
			strlen("MPI_Lookup_name") + 1, "%s", "MPI_Lookup_name");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Open_port_ID].desc,
			strlen("MPI_Open_port") + 1, "%s", "MPI_Open_port");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Publish_name_ID].desc,
			strlen("MPI_Publish_name") + 1, "%s",
			"MPI_Publish_name");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Unpublish_name_ID].desc,
			strlen("MPI_Unpublish_name") + 1, "%s",
			"MPI_Unpublish_name");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_set_info_ID].desc,
			strlen("MPI_Comm_set_info") + 1, "%s",
			"MPI_Comm_set_info");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_get_info_ID].desc,
			strlen("MPI_Comm_get_info") + 1, "%s",
			"MPI_Comm_get_info");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Accumulate_ID].desc,
			strlen("MPI_Accumulate") + 1, "%s", "MPI_Accumulate");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Get_ID].desc,
			strlen("MPI_Get") + 1, "%s", "MPI_Get");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Put_ID].desc,
			strlen("MPI_Put") + 1, "%s", "MPI_Put");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_complete_ID].desc,
			strlen("MPI_Win_complete") + 1, "%s",
			"MPI_Win_complete");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_create_ID].desc,
			strlen("MPI_Win_create") + 1, "%s", "MPI_Win_create");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_fence_ID].desc,
			strlen("MPI_Win_fence") + 1, "%s", "MPI_Win_fence");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_free_ID].desc,
			strlen("MPI_Win_free") + 1, "%s", "MPI_Win_free");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_get_group_ID].desc,
			strlen("MPI_Win_get_group") + 1, "%s",
			"MPI_Win_get_group");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_lock_ID].desc,
			strlen("MPI_Win_lock") + 1, "%s", "MPI_Win_lock");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_post_ID].desc,
			strlen("MPI_Win_post") + 1, "%s", "MPI_Win_post");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_start_ID].desc,
			strlen("MPI_Win_start") + 1, "%s", "MPI_Win_start");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_test_ID].desc,
			strlen("MPI_Win_test") + 1, "%s", "MPI_Win_test");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_unlock_ID].desc,
			strlen("MPI_Win_unlock") + 1, "%s", "MPI_Win_unlock");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_wait_ID].desc,
			strlen("MPI_Win_wait") + 1, "%s", "MPI_Win_wait");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_allocate_ID].desc,
			strlen("MPI_Win_allocate") + 1, "%s",
			"MPI_Win_allocate");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_allocate_shared_ID].desc,
			strlen("MPI_Win_allocate_shared") + 1, "%s",
			"MPI_Win_allocate_shared");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_shared_query_ID].desc,
			strlen("MPI_Win_shared_query") + 1, "%s",
			"MPI_Win_shared_query");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_create_dynamic_ID].desc,
			strlen("MPI_Win_create_dynamic") + 1, "%s",
			"MPI_Win_create_dynamic");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_attach_ID].desc,
			strlen("MPI_Win_attach") + 1, "%s", "MPI_Win_attach");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_detach_ID].desc,
			strlen("MPI_Win_detach") + 1, "%s", "MPI_Win_detach");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_get_info_ID].desc,
			strlen("MPI_Win_get_info") + 1, "%s",
			"MPI_Win_get_info");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_set_info_ID].desc,
			strlen("MPI_Win_set_info") + 1, "%s",
			"MPI_Win_set_info");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Get_accumulate_ID].desc,
			strlen("MPI_Get_accumulate") + 1, "%s",
			"MPI_Get_accumulate");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Fetch_and_op_ID].desc,
			strlen("MPI_Fetch_and_op") + 1, "%s",
			"MPI_Fetch_and_op");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Compare_and_swap_ID].desc,
			strlen("MPI_Compare_and_swap") + 1, "%s",
			"MPI_Compare_and_swap");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Rput_ID].desc,
			strlen("MPI_Rput") + 1, "%s", "MPI_Rput");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Rget_ID].desc,
			strlen("MPI_Rget") + 1, "%s", "MPI_Rget");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Raccumulate_ID].desc,
			strlen("MPI_Raccumulate") + 1, "%s", "MPI_Raccumulate");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Rget_accumulate_ID].desc,
			strlen("MPI_Rget_accumulate") + 1, "%s",
			"MPI_Rget_accumulate");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_lock_all_ID].desc,
			strlen("MPI_Win_lock_all") + 1, "%s",
			"MPI_Win_lock_all");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_unlock_all_ID].desc,
			strlen("MPI_Win_unlock_all") + 1, "%s",
			"MPI_Win_unlock_all");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_flush_ID].desc,
			strlen("MPI_Win_flush") + 1, "%s", "MPI_Win_flush");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_flush_all_ID].desc,
			strlen("MPI_Win_flush_all") + 1, "%s",
			"MPI_Win_flush_all");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_flush_local_ID].desc,
			strlen("MPI_Win_flush_local") + 1, "%s",
			"MPI_Win_flush_local");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_flush_local_all_ID].desc,
			strlen("MPI_Win_flush_local_all") + 1, "%s",
			"MPI_Win_flush_local_all");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_sync_ID].desc,
			strlen("MPI_Win_sync") + 1, "%s", "MPI_Win_sync");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Add_error_class_ID].desc,
			strlen("MPI_Add_error_class") + 1, "%s",
			"MPI_Add_error_class");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Add_error_code_ID].desc,
			strlen("MPI_Add_error_code") + 1, "%s",
			"MPI_Add_error_code");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Add_error_string_ID].desc,
			strlen("MPI_Add_error_string") + 1, "%s",
			"MPI_Add_error_string");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_call_errhandler_ID].desc,
			strlen("MPI_Comm_call_errhandler") + 1, "%s",
			"MPI_Comm_call_errhandler");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_create_keyval_ID].desc,
			strlen("MPI_Comm_create_keyval") + 1, "%s",
			"MPI_Comm_create_keyval");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_delete_attr_ID].desc,
			strlen("MPI_Comm_delete_attr") + 1, "%s",
			"MPI_Comm_delete_attr");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_free_keyval_ID].desc,
			strlen("MPI_Comm_free_keyval") + 1, "%s",
			"MPI_Comm_free_keyval");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_get_attr_ID].desc,
			strlen("MPI_Comm_get_attr") + 1, "%s",
			"MPI_Comm_get_attr");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_get_name_ID].desc,
			strlen("MPI_Comm_get_name") + 1, "%s",
			"MPI_Comm_get_name");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_set_attr_ID].desc,
			strlen("MPI_Comm_set_attr") + 1, "%s",
			"MPI_Comm_set_attr");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_set_name_ID].desc,
			strlen("MPI_Comm_set_name") + 1, "%s",
			"MPI_Comm_set_name");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_call_errhandler_ID].desc,
			strlen("MPI_File_call_errhandler") + 1, "%s",
			"MPI_File_call_errhandler");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Grequest_complete_ID].desc,
			strlen("MPI_Grequest_complete") + 1, "%s",
			"MPI_Grequest_complete");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Grequest_start_ID].desc,
			strlen("MPI_Grequest_start") + 1, "%s",
			"MPI_Grequest_start");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Init_thread_ID].desc,
			strlen("MPI_Init_thread") + 1, "%s", "MPI_Init_thread");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Is_thread_main_ID].desc,
			strlen("MPI_Is_thread_main") + 1, "%s",
			"MPI_Is_thread_main");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Query_thread_ID].desc,
			strlen("MPI_Query_thread") + 1, "%s",
			"MPI_Query_thread");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Status_set_cancelled_ID].desc,
			strlen("MPI_Status_set_cancelled") + 1, "%s",
			"MPI_Status_set_cancelled");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Status_set_elements_ID].desc,
			strlen("MPI_Status_set_elements") + 1, "%s",
			"MPI_Status_set_elements");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_create_keyval_ID].desc,
			strlen("MPI_Type_create_keyval") + 1, "%s",
			"MPI_Type_create_keyval");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_delete_attr_ID].desc,
			strlen("MPI_Type_delete_attr") + 1, "%s",
			"MPI_Type_delete_attr");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_dup_ID].desc,
			strlen("MPI_Type_dup") + 1, "%s", "MPI_Type_dup");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_free_keyval_ID].desc,
			strlen("MPI_Type_free_keyval") + 1, "%s",
			"MPI_Type_free_keyval");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_get_attr_ID].desc,
			strlen("MPI_Type_get_attr") + 1, "%s",
			"MPI_Type_get_attr");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_get_contents_ID].desc,
			strlen("MPI_Type_get_contents") + 1, "%s",
			"MPI_Type_get_contents");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_get_envelope_ID].desc,
			strlen("MPI_Type_get_envelope") + 1, "%s",
			"MPI_Type_get_envelope");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_get_name_ID].desc,
			strlen("MPI_Type_get_name") + 1, "%s",
			"MPI_Type_get_name");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_set_attr_ID].desc,
			strlen("MPI_Type_set_attr") + 1, "%s",
			"MPI_Type_set_attr");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_set_name_ID].desc,
			strlen("MPI_Type_set_name") + 1, "%s",
			"MPI_Type_set_name");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_match_size_ID].desc,
			strlen("MPI_Type_match_size") + 1, "%s",
			"MPI_Type_match_size");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_call_errhandler_ID].desc,
			strlen("MPI_Win_call_errhandler") + 1, "%s",
			"MPI_Win_call_errhandler");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_create_keyval_ID].desc,
			strlen("MPI_Win_create_keyval") + 1, "%s",
			"MPI_Win_create_keyval");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_delete_attr_ID].desc,
			strlen("MPI_Win_delete_attr") + 1, "%s",
			"MPI_Win_delete_attr");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_free_keyval_ID].desc,
			strlen("MPI_Win_free_keyval") + 1, "%s",
			"MPI_Win_free_keyval");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_get_attr_ID].desc,
			strlen("MPI_Win_get_attr") + 1, "%s",
			"MPI_Win_get_attr");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_get_name_ID].desc,
			strlen("MPI_Win_get_name") + 1, "%s",
			"MPI_Win_get_name");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_set_attr_ID].desc,
			strlen("MPI_Win_set_attr") + 1, "%s",
			"MPI_Win_set_attr");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_set_name_ID].desc,
			strlen("MPI_Win_set_name") + 1, "%s",
			"MPI_Win_set_name");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Alloc_mem_ID].desc,
			strlen("MPI_Alloc_mem") + 1, "%s", "MPI_Alloc_mem");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_create_errhandler_ID].desc,
			strlen("MPI_Comm_create_errhandler") + 1, "%s",
			"MPI_Comm_create_errhandler");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_get_errhandler_ID].desc,
			strlen("MPI_Comm_get_errhandler") + 1, "%s",
			"MPI_Comm_get_errhandler");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_set_errhandler_ID].desc,
			strlen("MPI_Comm_set_errhandler") + 1, "%s",
			"MPI_Comm_set_errhandler");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_create_errhandler_ID].desc,
			strlen("MPI_File_create_errhandler") + 1, "%s",
			"MPI_File_create_errhandler");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_get_errhandler_ID].desc,
			strlen("MPI_File_get_errhandler") + 1, "%s",
			"MPI_File_get_errhandler");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_set_errhandler_ID].desc,
			strlen("MPI_File_set_errhandler") + 1, "%s",
			"MPI_File_set_errhandler");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Finalized_ID].desc,
			strlen("MPI_Finalized") + 1, "%s", "MPI_Finalized");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Free_mem_ID].desc,
			strlen("MPI_Free_mem") + 1, "%s", "MPI_Free_mem");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Get_address_ID].desc,
			strlen("MPI_Get_address") + 1, "%s", "MPI_Get_address");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Info_create_ID].desc,
			strlen("MPI_Info_create") + 1, "%s", "MPI_Info_create");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Info_delete_ID].desc,
			strlen("MPI_Info_delete") + 1, "%s", "MPI_Info_delete");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Info_dup_ID].desc,
			strlen("MPI_Info_dup") + 1, "%s", "MPI_Info_dup");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Info_free_ID].desc,
			strlen("MPI_Info_free") + 1, "%s", "MPI_Info_free");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Info_get_ID].desc,
			strlen("MPI_Info_get") + 1, "%s", "MPI_Info_get");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Info_get_nkeys_ID].desc,
			strlen("MPI_Info_get_nkeys") + 1, "%s",
			"MPI_Info_get_nkeys");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Info_get_nthkey_ID].desc,
			strlen("MPI_Info_get_nthkey") + 1, "%s",
			"MPI_Info_get_nthkey");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Info_get_valuelen_ID].desc,
			strlen("MPI_Info_get_valuelen") + 1, "%s",
			"MPI_Info_get_valuelen");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Info_set_ID].desc,
			strlen("MPI_Info_set") + 1, "%s", "MPI_Info_set");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Pack_external_ID].desc,
			strlen("MPI_Pack_external") + 1, "%s",
			"MPI_Pack_external");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Pack_external_size_ID].desc,
			strlen("MPI_Pack_external_size") + 1, "%s",
			"MPI_Pack_external_size");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Request_get_status_ID].desc,
			strlen("MPI_Request_get_status") + 1, "%s",
			"MPI_Request_get_status");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_create_darray_ID].desc,
			strlen("MPI_Type_create_darray") + 1, "%s",
			"MPI_Type_create_darray");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_create_hindexed_ID].desc,
			strlen("MPI_Type_create_hindexed") + 1, "%s",
			"MPI_Type_create_hindexed");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_create_hvector_ID].desc,
			strlen("MPI_Type_create_hvector") + 1, "%s",
			"MPI_Type_create_hvector");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_create_indexed_block_ID].desc,
			strlen("MPI_Type_create_indexed_block") + 1, "%s",
			"MPI_Type_create_indexed_block");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_create_hindexed_block_ID].desc,
			strlen("MPI_Type_create_hindexed_block") + 1, "%s",
			"MPI_Type_create_hindexed_block");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_create_resized_ID].desc,
			strlen("MPI_Type_create_resized") + 1, "%s",
			"MPI_Type_create_resized");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_create_struct_ID].desc,
			strlen("MPI_Type_create_struct") + 1, "%s",
			"MPI_Type_create_struct");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_create_subarray_ID].desc,
			strlen("MPI_Type_create_subarray") + 1, "%s",
			"MPI_Type_create_subarray");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_get_extent_ID].desc,
			strlen("MPI_Type_get_extent") + 1, "%s",
			"MPI_Type_get_extent");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_get_true_extent_ID].desc,
			strlen("MPI_Type_get_true_extent") + 1, "%s",
			"MPI_Type_get_true_extent");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Unpack_external_ID].desc,
			strlen("MPI_Unpack_external") + 1, "%s",
			"MPI_Unpack_external");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_create_errhandler_ID].desc,
			strlen("MPI_Win_create_errhandler") + 1, "%s",
			"MPI_Win_create_errhandler");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_get_errhandler_ID].desc,
			strlen("MPI_Win_get_errhandler") + 1, "%s",
			"MPI_Win_get_errhandler");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Win_set_errhandler_ID].desc,
			strlen("MPI_Win_set_errhandler") + 1, "%s",
			"MPI_Win_set_errhandler");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_create_f90_integer_ID].desc,
			strlen("MPI_Type_create_f90_integer") + 1, "%s",
			"MPI_Type_create_f90_integer");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_create_f90_real_ID].desc,
			strlen("MPI_Type_create_f90_real") + 1, "%s",
			"MPI_Type_create_f90_real");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_create_f90_complex_ID].desc,
			strlen("MPI_Type_create_f90_complex") + 1, "%s",
			"MPI_Type_create_f90_complex");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Reduce_local_ID].desc,
			strlen("MPI_Reduce_local") + 1, "%s",
			"MPI_Reduce_local");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Op_commutative_ID].desc,
			strlen("MPI_Op_commutative") + 1, "%s",
			"MPI_Op_commutative");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Reduce_scatter_block_ID].desc,
			strlen("MPI_Reduce_scatter_block") + 1, "%s",
			"MPI_Reduce_scatter_block");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Dist_graph_create_adjacent_ID].desc,
			strlen("MPI_Dist_graph_create_adjacent") + 1, "%s",
			"MPI_Dist_graph_create_adjacent");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Dist_graph_create_ID].desc,
			strlen("MPI_Dist_graph_create") + 1, "%s",
			"MPI_Dist_graph_create");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Dist_graph_neighbors_count_ID].desc,
			strlen("MPI_Dist_graph_neighbors_count") + 1, "%s",
			"MPI_Dist_graph_neighbors_count");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Dist_graph_neighbors_ID].desc,
			strlen("MPI_Dist_graph_neighbors") + 1, "%s",
			"MPI_Dist_graph_neighbors");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Improbe_ID].desc,
			strlen("MPI_Improbe") + 1, "%s", "MPI_Improbe");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Imrecv_ID].desc,
			strlen("MPI_Imrecv") + 1, "%s", "MPI_Imrecv");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Mprobe_ID].desc,
			strlen("MPI_Mprobe") + 1, "%s", "MPI_Mprobe");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Mrecv_ID].desc,
			strlen("MPI_Mrecv") + 1, "%s", "MPI_Mrecv");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_idup_ID].desc,
			strlen("MPI_Comm_idup") + 1, "%s", "MPI_Comm_idup");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ibarrier_ID].desc,
			strlen("MPI_Ibarrier") + 1, "%s", "MPI_Ibarrier");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ibcast_ID].desc,
			strlen("MPI_Ibcast") + 1, "%s", "MPI_Ibcast");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Igather_ID].desc,
			strlen("MPI_Igather") + 1, "%s", "MPI_Igather");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Igatherv_ID].desc,
			strlen("MPI_Igatherv") + 1, "%s", "MPI_Igatherv");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Iscatter_ID].desc,
			strlen("MPI_Iscatter") + 1, "%s", "MPI_Iscatter");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Iscatterv_ID].desc,
			strlen("MPI_Iscatterv") + 1, "%s", "MPI_Iscatterv");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Iallgather_ID].desc,
			strlen("MPI_Iallgather") + 1, "%s", "MPI_Iallgather");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Iallgatherv_ID].desc,
			strlen("MPI_Iallgatherv") + 1, "%s", "MPI_Iallgatherv");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ialltoall_ID].desc,
			strlen("MPI_Ialltoall") + 1, "%s", "MPI_Ialltoall");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ialltoallv_ID].desc,
			strlen("MPI_Ialltoallv") + 1, "%s", "MPI_Ialltoallv");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ialltoallw_ID].desc,
			strlen("MPI_Ialltoallw") + 1, "%s", "MPI_Ialltoallw");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ireduce_ID].desc,
			strlen("MPI_Ireduce") + 1, "%s", "MPI_Ireduce");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Iallreduce_ID].desc,
			strlen("MPI_Iallreduce") + 1, "%s", "MPI_Iallreduce");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ireduce_scatter_ID].desc,
			strlen("MPI_Ireduce_scatter") + 1, "%s",
			"MPI_Ireduce_scatter");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ireduce_scatter_block_ID].desc,
			strlen("MPI_Ireduce_scatter_block") + 1, "%s",
			"MPI_Ireduce_scatter_block");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Iscan_ID].desc,
			strlen("MPI_Iscan") + 1, "%s", "MPI_Iscan");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Iexscan_ID].desc,
			strlen("MPI_Iexscan") + 1, "%s", "MPI_Iexscan");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ineighbor_allgather_ID].desc,
			strlen("MPI_Ineighbor_allgather") + 1, "%s",
			"MPI_Ineighbor_allgather");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ineighbor_allgatherv_ID].desc,
			strlen("MPI_Ineighbor_allgatherv") + 1, "%s",
			"MPI_Ineighbor_allgatherv");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ineighbor_alltoall_ID].desc,
			strlen("MPI_Ineighbor_alltoall") + 1, "%s",
			"MPI_Ineighbor_alltoall");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ineighbor_alltoallv_ID].desc,
			strlen("MPI_Ineighbor_alltoallv") + 1, "%s",
			"MPI_Ineighbor_alltoallv");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Ineighbor_alltoallw_ID].desc,
			strlen("MPI_Ineighbor_alltoallw") + 1, "%s",
			"MPI_Ineighbor_alltoallw");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Neighbor_allgather_ID].desc,
			strlen("MPI_Neighbor_allgather") + 1, "%s",
			"MPI_Neighbor_allgather");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Neighbor_allgatherv_ID].desc,
			strlen("MPI_Neighbor_allgatherv") + 1, "%s",
			"MPI_Neighbor_allgatherv");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Neighbor_alltoall_ID].desc,
			strlen("MPI_Neighbor_alltoall") + 1, "%s",
			"MPI_Neighbor_alltoall");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Neighbor_alltoallv_ID].desc,
			strlen("MPI_Neighbor_alltoallv") + 1, "%s",
			"MPI_Neighbor_alltoallv");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Neighbor_alltoallw_ID].desc,
			strlen("MPI_Neighbor_alltoallw") + 1, "%s",
			"MPI_Neighbor_alltoallw");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_split_type_ID].desc,
			strlen("MPI_Comm_split_type") + 1, "%s",
			"MPI_Comm_split_type");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Get_elements_x_ID].desc,
			strlen("MPI_Get_elements_x") + 1, "%s",
			"MPI_Get_elements_x");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Status_set_elements_x_ID].desc,
			strlen("MPI_Status_set_elements_x") + 1, "%s",
			"MPI_Status_set_elements_x");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_get_extent_x_ID].desc,
			strlen("MPI_Type_get_extent_x") + 1, "%s",
			"MPI_Type_get_extent_x");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_get_true_extent_x_ID].desc,
			strlen("MPI_Type_get_true_extent_x") + 1, "%s",
			"MPI_Type_get_true_extent_x");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Type_size_x_ID].desc,
			strlen("MPI_Type_size_x") + 1, "%s", "MPI_Type_size_x");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Comm_create_group_ID].desc,
			strlen("MPI_Comm_create_group") + 1, "%s",
			"MPI_Comm_create_group");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_open_ID].desc,
			strlen("MPI_File_open") + 1, "%s", "MPI_File_open");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_close_ID].desc,
			strlen("MPI_File_close") + 1, "%s", "MPI_File_close");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_delete_ID].desc,
			strlen("MPI_File_delete") + 1, "%s", "MPI_File_delete");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_set_size_ID].desc,
			strlen("MPI_File_set_size") + 1, "%s",
			"MPI_File_set_size");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_preallocate_ID].desc,
			strlen("MPI_File_preallocate") + 1, "%s",
			"MPI_File_preallocate");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_get_size_ID].desc,
			strlen("MPI_File_get_size") + 1, "%s",
			"MPI_File_get_size");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_get_group_ID].desc,
			strlen("MPI_File_get_group") + 1, "%s",
			"MPI_File_get_group");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_get_amode_ID].desc,
			strlen("MPI_File_get_amode") + 1, "%s",
			"MPI_File_get_amode");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_set_info_ID].desc,
			strlen("MPI_File_set_info") + 1, "%s",
			"MPI_File_set_info");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_get_info_ID].desc,
			strlen("MPI_File_get_info") + 1, "%s",
			"MPI_File_get_info");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_set_view_ID].desc,
			strlen("MPI_File_set_view") + 1, "%s",
			"MPI_File_set_view");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_get_view_ID].desc,
			strlen("MPI_File_get_view") + 1, "%s",
			"MPI_File_get_view");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_read_at_ID].desc,
			strlen("MPI_File_read_at") + 1, "%s",
			"MPI_File_read_at");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_read_at_all_ID].desc,
			strlen("MPI_File_read_at_all") + 1, "%s",
			"MPI_File_read_at_all");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_write_at_ID].desc,
			strlen("MPI_File_write_at") + 1, "%s",
			"MPI_File_write_at");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_write_at_all_ID].desc,
			strlen("MPI_File_write_at_all") + 1, "%s",
			"MPI_File_write_at_all");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_iread_at_ID].desc,
			strlen("MPI_File_iread_at") + 1, "%s",
			"MPI_File_iread_at");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_iwrite_at_ID].desc,
			strlen("MPI_File_iwrite_at") + 1, "%s",
			"MPI_File_iwrite_at");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_read_ID].desc,
			strlen("MPI_File_read") + 1, "%s", "MPI_File_read");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_read_all_ID].desc,
			strlen("MPI_File_read_all") + 1, "%s",
			"MPI_File_read_all");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_write_ID].desc,
			strlen("MPI_File_write") + 1, "%s", "MPI_File_write");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_write_all_ID].desc,
			strlen("MPI_File_write_all") + 1, "%s",
			"MPI_File_write_all");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_iread_ID].desc,
			strlen("MPI_File_iread") + 1, "%s", "MPI_File_iread");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_iwrite_ID].desc,
			strlen("MPI_File_iwrite") + 1, "%s", "MPI_File_iwrite");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_seek_ID].desc,
			strlen("MPI_File_seek") + 1, "%s", "MPI_File_seek");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_get_position_ID].desc,
			strlen("MPI_File_get_position") + 1, "%s",
			"MPI_File_get_position");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_get_byte_offset_ID].desc,
			strlen("MPI_File_get_byte_offset") + 1, "%s",
			"MPI_File_get_byte_offset");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_read_shared_ID].desc,
			strlen("MPI_File_read_shared") + 1, "%s",
			"MPI_File_read_shared");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_write_shared_ID].desc,
			strlen("MPI_File_write_shared") + 1, "%s",
			"MPI_File_write_shared");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_iread_shared_ID].desc,
			strlen("MPI_File_iread_shared") + 1, "%s",
			"MPI_File_iread_shared");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_iwrite_shared_ID].desc,
			strlen("MPI_File_iwrite_shared") + 1, "%s",
			"MPI_File_iwrite_shared");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_read_ordered_ID].desc,
			strlen("MPI_File_read_ordered") + 1, "%s",
			"MPI_File_read_ordered");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_write_ordered_ID].desc,
			strlen("MPI_File_write_ordered") + 1, "%s",
			"MPI_File_write_ordered");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_seek_shared_ID].desc,
			strlen("MPI_File_seek_shared") + 1, "%s",
			"MPI_File_seek_shared");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_get_position_shared_ID].desc,
			strlen("MPI_File_get_position_shared") + 1, "%s",
			"MPI_File_get_position_shared");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_read_at_all_begin_ID].desc,
			strlen("MPI_File_read_at_all_begin") + 1, "%s",
			"MPI_File_read_at_all_begin");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_read_at_all_end_ID].desc,
			strlen("MPI_File_read_at_all_end") + 1, "%s",
			"MPI_File_read_at_all_end");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_write_at_all_begin_ID].desc,
			strlen("MPI_File_write_at_all_begin") + 1, "%s",
			"MPI_File_write_at_all_begin");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_write_at_all_end_ID].desc,
			strlen("MPI_File_write_at_all_end") + 1, "%s",
			"MPI_File_write_at_all_end");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_read_all_begin_ID].desc,
			strlen("MPI_File_read_all_begin") + 1, "%s",
			"MPI_File_read_all_begin");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_read_all_end_ID].desc,
			strlen("MPI_File_read_all_end") + 1, "%s",
			"MPI_File_read_all_end");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_write_all_begin_ID].desc,
			strlen("MPI_File_write_all_begin") + 1, "%s",
			"MPI_File_write_all_begin");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_write_all_end_ID].desc,
			strlen("MPI_File_write_all_end") + 1, "%s",
			"MPI_File_write_all_end");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_read_ordered_begin_ID].desc,
			strlen("MPI_File_read_ordered_begin") + 1, "%s",
			"MPI_File_read_ordered_begin");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_read_ordered_end_ID].desc,
			strlen("MPI_File_read_ordered_end") + 1, "%s",
			"MPI_File_read_ordered_end");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_write_ordered_begin_ID].desc,
			strlen("MPI_File_write_ordered_begin") + 1, "%s",
			"MPI_File_write_ordered_begin");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_write_ordered_end_ID].desc,
			strlen("MPI_File_write_ordered_end") + 1, "%s",
			"MPI_File_write_ordered_end");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_get_type_extent_ID].desc,
			strlen("MPI_File_get_type_extent") + 1, "%s",
			"MPI_File_get_type_extent");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_Register_datarep_ID].desc,
			strlen("MPI_Register_datarep") + 1, "%s",
			"MPI_Register_datarep");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_set_atomicity_ID].desc,
			strlen("MPI_File_set_atomicity") + 1, "%s",
			"MPI_File_set_atomicity");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_get_atomicity_ID].desc,
			strlen("MPI_File_get_atomicity") + 1, "%s",
			"MPI_File_get_atomicity");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_sync_ID].desc,
			strlen("MPI_File_sync") + 1, "%s", "MPI_File_sync");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Aint_add_ID].desc,
			strlen("MPI_Aint_add") + 1, "%s", "MPI_Aint_add");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_Aint_diff_ID].desc,
			strlen("MPI_Aint_diff") + 1, "%s", "MPI_Aint_diff");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_iread_all_ID].desc,
			strlen("MPI_File_iread_all") + 1, "%s",
			"MPI_File_iread_all");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_iread_at_all_ID].desc,
			strlen("MPI_File_iread_at_all") + 1, "%s",
			"MPI_File_iread_at_all");
	snprintf(ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_iwrite_all_ID].desc,
			strlen("MPI_File_iwrite_all") + 1, "%s",
			"MPI_File_iwrite_all");
	snprintf(
			ldms_shm_mpi_base_events[LDMS_SHM_MPI_File_iwrite_at_all_ID].desc,
			strlen("MPI_File_iwrite_at_all") + 1, "%s",
			"PI_File_iwrite_at_all");
}

/* FIXME improve this */
ldms_shm_MPI_func_id_t find_func_id(char* func_name)
{
	int i;
	for(i = 0; i < LDMS_SHM_MPI_NUM_FUNCTIONS; i++) {
		if(strcmp(profiler->ldms_shm_mpi_base_events[i].desc, func_name)
				== 0)
			return i;
	}
	return -1;
}
