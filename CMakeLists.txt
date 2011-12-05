if ( NOT LINUX_KERNEL_VERSION )
  execute_process(
    COMMAND uname -r
    OUTPUT_VARIABLE LINUX_KERNEL_VERSION
    RESULT_VARIABLE UNAME_SUCCESS
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if ( NOT LINUX_KERNEL_VERSION )
    set( LINUX_KERNEL_VERSION "foo" )
  endif()
endif()

set( LINUX_KERNEL_CONFIG_DIR
  "/usr/src/kernels/${LINUX_KERNEL_VERSION}/include/config"
  CACHE PATH "Path to kernel configuration header files." )

include_directories(
  ${LINUX_KERNEL_CONFIG_DIR}
  ${LINUX_KERNEL_CONFIG_DIR}/infiniband
)

set( ldms_HEADERS
  src/ldms.h
  src/ldms_sock_xprt.h
  src/ldms_xprt.h
  src/list.h
  src/ogc_rbt.h
)
file( MAKE_DIRECTORY ${ldms_BINARY_DIR}/include/ldms )
foreach ( file ${ldms_HEADERS} )
  configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/${file} ${ldms_BINARY_DIR}/include/ldms COPY_ONLY )
endforeach ()

add_library( un
  src/libun.c
)

if ( ldms_RDMA )
  add_library( ldmsrdma
    src/ldms_rdma.c
  )
  target_link_libraries( ldmsrdma
    verbs
    rdmacm
  )
  set( ldms_SUBLIBRARIES "${ldms_SUBLIBRARIES};ldmsrdma" )
else ( ldms_RDMA )
  set( ldms_FLAGS "${ldms_FLAGS};DISABLE_RDMA" )
endif ( ldms_RDMA )

if ( ldms_MMAP )
  set( ldms_FLAGS "${ldms_FLAGS};ENABLE_MMAP" )
endif ( ldms_MMAP )

if ( ldms_SOCK )
  add_library( ldmssock
    src/ldms_sock.c
  )
  set( ldms_SUBLIBRARIES "${ldms_SUBLIBRARIES};ldmssock" )
else ( ldms_SOCK )
  set( ldms_FLAGS "${ldms_FLAGS};DISABLE_SOCK" )
endif ( ldms_SOCK )

add_library( ldms
  src/ldms.c
  src/ldms_sock.c
  src/ldms_xprt.c
  src/ogc_rbt.c
)
target_link_libraries( ldms dl pthread )

add_executable( ldms_ls
  src/ldms_ls.c
)
target_link_libraries( ldms_ls ldms )

add_executable( ldmsd
  src/ldmsd.c
  src/ldmsd_cfg.c
)
target_link_libraries( ldmsd ldms )

add_executable( vmstat_un
  src/vmstat_un.c
)
target_link_libraries( vmstat_un ldms un )

add_executable( interrupts_un
  src/interrupts_un.c
)
target_link_libraries( interrupts_un ldms un )

add_executable( meminfo_un
  src/meminfo_un.c
)
target_link_libraries( meminfo_un ldms un )

add_executable( schedstat_un
  src/schedstat_un.c
)
target_link_libraries( schedstat_un ldms un )
