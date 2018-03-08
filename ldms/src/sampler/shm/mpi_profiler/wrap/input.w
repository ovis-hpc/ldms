// The source for mpi wrapper functions. It uses llnl mpi wrapper generator. see https://github.com/LLNL/wrap


//For all functions except "MPI_Init MPI_Finalize MPI_Pcontrol MPI_Finalized" we want to call 'post_function_call' to profile
{{fnall foo MPI_Init MPI_Finalize MPI_Pcontrol MPI_Finalized MPI_Allreduce MPI_Bcast MPI_Bsend MPI_Bsend_init MPI_Exscan MPI_File_iread MPI_File_iread_at MPI_File_iread_shared MPI_File_iwrite MPI_File_iwrite_at MPI_File_iwrite_shared MPI_File_read MPI_File_read_all MPI_File_read_at MPI_File_read_at_all MPI_File_read_ordered MPI_File_read_shared MPI_File_write MPI_File_write_all MPI_File_write_at MPI_File_write_at_all MPI_File_write_ordered MPI_File_write_shared MPI_Iallreduce MPI_Ibcast MPI_Ibsend MPI_Iexscan MPI_Irecv MPI_Ireduce MPI_Irsend MPI_Iscan MPI_Isend MPI_Issend MPI_Recv_init MPI_Reduce MPI_Reduce_local MPI_Rsend MPI_Rsend_init MPI_Scan MPI_Send_init MPI_Sendrecv_replace MPI_Ssend MPI_Ssend_init MPI_Send MPI_Recv}}
  {{callfn}}
  if (profile_log_level != LDMS_SHM_PROFILING_DISABLED)
  	post_function_call(LDMS_SHM_{{foo}}_ID);
{{endfnall}}

//message based functions are different. call post_msg_based_function_call.
{{fn foo MPI_Allreduce MPI_Bcast MPI_Bsend MPI_Bsend_init MPI_Exscan MPI_File_iread MPI_File_iread_at MPI_File_iread_shared MPI_File_iwrite MPI_File_iwrite_at MPI_File_iwrite_shared MPI_File_read MPI_File_read_all MPI_File_read_at MPI_File_read_at_all MPI_File_read_ordered MPI_File_read_shared MPI_File_write MPI_File_write_all MPI_File_write_at MPI_File_write_at_all MPI_File_write_ordered MPI_File_write_shared MPI_Iallreduce MPI_Ibcast MPI_Ibsend MPI_Iexscan MPI_Irecv MPI_Ireduce MPI_Irsend MPI_Iscan MPI_Isend MPI_Issend MPI_Recv_init MPI_Reduce MPI_Reduce_local MPI_Rsend MPI_Rsend_init MPI_Scan MPI_Send_init MPI_Sendrecv_replace MPI_Ssend MPI_Ssend_init MPI_Send MPI_Recv}}
  {{callfn}}
  if (profile_log_level != LDMS_SHM_PROFILING_DISABLED)
  	post_msg_based_function_call(LDMS_SHM_{{foo}}_ID, count, datatype);
{{endfn}}

//MPI_Init is different. First check for index variable, then call post_init_call.
{{fn foo MPI_Init}}
  profile_log_level = LDMS_SHM_LOG_LNONE;
  {{callfn}}
  	ldms_shm_index_name = getenv(LDMS_SHM_INDEX_ENV_VAR_NAME);
	if(!ldms_shm_index_name) {
		printf(
				"The environment variable: \"%s\" is empty. Disabling profiling ...\n\r",
				LDMS_SHM_INDEX_ENV_VAR_NAME);
		MPI_Pcontrol(LDMS_SHM_PROFILING_DISABLED);
		return {{ret_val}};
	}
	ldms_shm_mpi_app_name = malloc(strlen(argv[0][0]));
    sprintf(ldms_shm_mpi_app_name,"%s",argv[0][0]);
	post_init_call();
{{endfn}}

//MPI_Finalize is different. call post_finalize_call.
{{fn foo MPI_Finalize}}
  {{callfn}}
  if (profile_log_level != LDMS_SHM_PROFILING_DISABLED)
  	post_finalize_call();
{{endfn}}

//MPI_Pcontrol is different. call post_Pcontrol_call.
{{fn foo MPI_Pcontrol}}
  {{callfn}}
  if (profile_log_level != LDMS_SHM_PROFILING_DISABLED)
  	post_Pcontrol_call(level);
{{endfn}}

