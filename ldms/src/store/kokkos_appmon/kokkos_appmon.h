#ifndef _KOKKOS_H_
#define _KOKKOS_H_

#include <sos/sos.h>
#include "flxs.h"

enum kokkos_request_attr {
	KATTR_REQUEST_ID = 1,
	KATTR_job_id,
	KATTR_app_id,
	KATTR_inst_data,
	KATTR_mpi_rank,
	KATTR_start_time,
	KATTR_component_id,
	KATTR_total_app_time,
	KATTR_total_kernel_times,
	KATTR_total_non_kernel_times,
	KATTR_percent_in_kernels,
	KATTR_unique_kernel_calls,
	KATTR_kernel_name,
	KATTR_kernel_type,
	KATTR_region,
	KATTR_call_count,
	KATTR_total_time,
	KATTR_time_per_call
};

/* Kokkos App object attribute ids */
enum kapp_obj_attr_ids {
	KAPP_job_id,
	KAPP_app_id,
	KAPP_inst_data,
	KAPP_mpi_rank,
	KAPP_start_time,
	KAPP_component_id,
	KAPP_total_app_time,
	KAPP_total_kernel_times,
	KAPP_total_non_kernel_times,
	KAPP_percent_in_kernels,
	KAPP_unique_kernel_calls,
	KAPP_job_app_inst_rank_time,
};

/* Kokkos Kernel object attribute ids */
enum kernel_obj_attr_ids {
	KERNEL_job_id,
	KERNEL_app_id,
	KERNEL_inst_data,
	KERNEL_mpi_rank,
	KERNEL_start_time,
	KERNEL_kernel_name,
	KERNEL_kernel_type,
	KERNEL_region,
	KERNEL_call_count,
	KERNEL_total_time,
	KERNEL_time_per_call,
	KERNEL_job_app_inst_rank_time,
};

/*
 * Map of Kokkos App request attributes to object attributes. These
 * attributes must match the order in kapp_obj_attr_ids
 */
#define KAPP_MAP_ENTRY(_name_) [KATTR_ ## _name_] = FLXS_MAP_ATTR(_name_, KATTR_, KAPP_)
static struct flxs_attr_map kokkos_app_map[] = {
	KAPP_MAP_ENTRY( job_id ),
	KAPP_MAP_ENTRY( app_id ),
	KAPP_MAP_ENTRY( inst_data ),
	KAPP_MAP_ENTRY( start_time ),
	KAPP_MAP_ENTRY( mpi_rank ),
	KAPP_MAP_ENTRY( component_id ),
	KAPP_MAP_ENTRY( total_app_time ),
	KAPP_MAP_ENTRY( total_kernel_times ),
	KAPP_MAP_ENTRY( total_non_kernel_times ),
	KAPP_MAP_ENTRY( percent_in_kernels ),
	KAPP_MAP_ENTRY( unique_kernel_calls )
};

/*
 * Map of Kokkos Kernel request attributes to object attributes. These
 * attributes must match the order in kernel_obj_attr_ids
 */
#define KERNEL_MAP_ENTRY(_name_) [KATTR_ ## _name_] = FLXS_MAP_ATTR(_name_, KATTR_, KERNEL_)
static struct flxs_attr_map kokkos_kernel_map[] = {
	KERNEL_MAP_ENTRY( job_id ),
	KERNEL_MAP_ENTRY( app_id ),
	KERNEL_MAP_ENTRY( inst_data ),
	KERNEL_MAP_ENTRY( start_time ),
	KERNEL_MAP_ENTRY( mpi_rank ),
	KERNEL_MAP_ENTRY( kernel_name ),
	KERNEL_MAP_ENTRY( kernel_type ),
	KERNEL_MAP_ENTRY( region ),
	KERNEL_MAP_ENTRY( call_count ),
	KERNEL_MAP_ENTRY( total_time ),
	KERNEL_MAP_ENTRY( time_per_call ),
};


static const char *app_join_list[] = { "job_id", "app_id", "inst_data", "start_time" };
static struct sos_schema_template kokkos_app_schema = {
	.name = "kokkos_app",
	.attrs = {
		FLXS_SCHEMA_ENTRY( KAPP_, job_id, UINT64 ),
		FLXS_SCHEMA_ENTRY( KAPP_, app_id, UINT64 ),
		FLXS_SCHEMA_STRUCT_ENTRY( KAPP_, inst_data, 32 ),
		FLXS_SCHEMA_ENTRY( KAPP_, start_time, TIMESTAMP ),
		FLXS_SCHEMA_ENTRY( KAPP_, mpi_rank, UINT64 ),
		FLXS_SCHEMA_ENTRY( KAPP_, component_id, UINT64 ),
		FLXS_SCHEMA_ENTRY( KAPP_, total_app_time, DOUBLE ),
		FLXS_SCHEMA_ENTRY( KAPP_, total_kernel_times, DOUBLE ),
		FLXS_SCHEMA_ENTRY( KAPP_, total_non_kernel_times, DOUBLE ),
		FLXS_SCHEMA_ENTRY( KAPP_, percent_in_kernels, DOUBLE ),
		FLXS_SCHEMA_ENTRY( KAPP_, unique_kernel_calls, DOUBLE ),
		FLXS_SCHEMA_JOIN_ENTRY( KAPP_, job_app_inst_rank_time, app_join_list ),
		FLXS_SCHEMA_END()
	}
};

static const char *kernel_join_list[] = { "job_id", "app_id", "inst_data", "start_time" };
static struct sos_schema_template kokkos_kernel_schema = {
	"kokkos_kernel",
	.attrs = {
		FLXS_SCHEMA_ENTRY( KERNEL_, job_id, UINT64 ),
		FLXS_SCHEMA_ENTRY( KERNEL_, app_id, UINT64 ),
		FLXS_SCHEMA_STRUCT_ENTRY( KERNEL_, inst_data, 32 ),
		FLXS_SCHEMA_ENTRY( KERNEL_, start_time, TIMESTAMP ),
		FLXS_SCHEMA_ENTRY( KERNEL_, mpi_rank, UINT64 ),
		FLXS_SCHEMA_STRUCT_ENTRY( KERNEL_, kernel_name, 32 ),
		FLXS_SCHEMA_ENTRY( KERNEL_, kernel_type, CHAR_ARRAY ),
		FLXS_SCHEMA_ENTRY( KERNEL_, region, CHAR_ARRAY ),
		FLXS_SCHEMA_ENTRY( KERNEL_, call_count, DOUBLE ),
		FLXS_SCHEMA_ENTRY( KERNEL_, total_time, DOUBLE ),
		FLXS_SCHEMA_ENTRY( KERNEL_, time_per_call, DOUBLE ),
		FLXS_SCHEMA_JOIN_ENTRY( KERNEL_, job_app_inst_rank_time, kernel_join_list ),
		FLXS_SCHEMA_END()
	}
};

#endif
