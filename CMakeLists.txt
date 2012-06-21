project( ldms )
cmake_minimum_required(VERSION 2.8)
set( CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}" )
include(FindPkgConfig)

set( CMAKE_THREAD_PREFER_PTHREAD 1 )
find_package( Threads REQUIRED )
find_package( LibEvent )
if ( EVENT_FOUND )
  option( ldms_USE_SYSTEM_LIBEVENT ON "Use system's libevent library?" )
else ( EVENT_FOUND )
  option( ldms_USE_SYSTEM_LIBEVENT OFF "Use system's libevent library?" )
endif ( EVENT_FOUND )
pkg_check_modules( glib2  glib-2.0 )

if ( NOT ldms_EXPORT_NAME )
  # Fallback name for the package containing export file (assuming parent package hasn't set it)
  # NB. Must stay above add_subdirectory( util ).
  set( ldms_EXPORT_NAME "ldms" )
endif ( NOT ldms_EXPORT_NAME )

add_subdirectory( util )

option( ldms_MMAP "Support metric transmission via mmap?" OFF )
option( ldms_RDMA "Support metric transmission via RDMA?" OFF )
option( ldms_SOCK "Support metric transmission via sockets?" ON )

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

include( CheckFunctionExists )
set( CMAKE_REQUIRED_INCLUDES "pthread.h" )
set( CMAKE_REQUIRED_LIBRARIES ${CMAKE_THREAD_LIBS_INIT} )
check_function_exists( pthread_spin_lock HAVE_SPINLOCK_T )

include_directories(
  ${LINUX_KERNEL_CONFIG_DIR}
  ${LINUX_KERNEL_CONFIG_DIR}/infiniband
  ${EVENT_INCLUDE_DIRS}
  ${ldms_BINARY_DIR}/include/ldms
  ${glib2_INCLUDE_DIRS}
)

set( ldms_INSTALL_TARGETS "" )
set( ldms_HEADERS
  src/ldms.h
  src/ldms_sock_xprt.h
  src/ldms_xprt.h
  src/ogc_rbt.h
)
file( MAKE_DIRECTORY ${ldms_BINARY_DIR}/include/ldms )
foreach ( file ${ldms_HEADERS} )
  configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/${file} ${ldms_BINARY_DIR}/include/ldms COPY_ONLY )
endforeach ()
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/ldms_config.h.cmake
  ${ldms_BINARY_DIR}/include/ldms/config.h
  @ONLY )

add_library( un
  src/libun.c
)
add_dependencies( un ${CMAKE_CURRENT_SOURCE_DIR}/src/libun.map )
set( ldms_INSTALL_TARGETS "${ldms_INSTALL_TARGETS};un" )

if ( ldms_RDMA )
  add_library( ldmsrdma MODULE
    src/ldms_rdma.c
  )
  target_link_libraries( ldmsrdma
    verbs
    rdmacm
    ${EVENT_LIBRARIES}
    ldms
  )
  add_dependencies( ldmsrdma ${CMAKE_CURRENT_SOURCE_DIR}/src/libldmsrdma.map )
  set( ldms_SUBLIBRARIES "${ldms_SUBLIBRARIES};ldmsrdma" )
else ( ldms_RDMA )
  set( ldms_FLAGS "${ldms_FLAGS};DISABLE_RDMA" )
endif ( ldms_RDMA )

if ( ldms_MMAP )
  set( ldms_FLAGS "${ldms_FLAGS};ENABLE_MMAP" )
endif ( ldms_MMAP )

if ( ldms_SOCK )
  add_library( ldmssock MODULE
    src/ldms_sock.c
  )
  target_link_libraries( ldmssock
    ${EVENT_LIBRARIES}
    ldms
  )
  add_dependencies( ldmssock ${CMAKE_CURRENT_SOURCE_DIR}/src/libldmssock.map )
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
add_dependencies( ldms ${CMAKE_CURRENT_SOURCE_DIR}/src/libldms.map )
target_link_libraries( ldms "${EVENT_LIBRARIES}" dl pthread )
set( ldms_INSTALL_TARGETS "${ldms_INSTALL_TARGETS};ldms;${ldms_SUBLIBRARIES}" )

add_executable( ldms_ls
  src/ldms_ls.c
)
target_link_libraries( ldms_ls ldms )
set( ldms_INSTALL_TARGETS "${ldms_INSTALL_TARGETS};ldms_ls" )

find_package( readline )
if ( readline_FOUND )
  add_executable( ldmsctl
    src/ldmsctl.c
  )
  target_link_libraries( ldmsctl ldms un ${readline_LIBRARIES} )
  set( ldms_INSTALL_TARGETS "${ldms_INSTALL_TARGETS};ldmsctl" )
endif ( readline_FOUND )

add_executable( ldmsd
  src/ldmsd.c
  src/ldmsd_cfg.c
)
target_link_libraries( ldmsd ldms )
set( ldms_INSTALL_TARGETS "${ldms_INSTALL_TARGETS};ldmsd" )

set( ldms_plugins
  meminfo
  geminfo
  procinterrupts
  procstatutil
  vmstat
)

# Only build the perf_event module when papi exists.
find_package( papi )
if ( papi_FOUND )
  set( ldms_plugins ${ldms_plugins} perf_event )
  set( perf_event_LIBRARIES ${papi_LIBRARIES} )
endif()

# Only build the sedc module when glib2 is present.
if ( glib2_FOUND )
  set( ldms_plugins ${ldms_plugins} sedc )
  set( sedc_LIBRARIES ${glib2_LIBRARIES} )
endif()

foreach( plugin ${ldms_plugins} )
  add_library( ${plugin} MODULE src/${plugin}.c )
  add_dependencies( ${plugin} src/${plugin}.map )
  if ( ${${plugin}_LIBRARIES} )
    target_link_libraries( ${plugin} ${${plugin}_LIBRARIES} )
  endif()
  target_link_libraries( ${plugin} ldms )
endforeach()

if ( glib2_FOUND )
  add_library( metricmap src/metricmap.c )
  target_link_libraries( metricmap
    ${glib2_LIBRARIES}
    ${EVENT_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
  )
  add_executable( hwlocldms_ls src/hwlocldms_ls.c )
  target_link_libraries( hwlocldms_ls
    metricmap
    ${glib2_LIBRARIES}
  )
  set( ldms_INSTALL_TARGETS "${ldms_INSTALL_TARGETS};metricmap;hwlocldms_ls" )
endif()

# Doxygen documentation
find_package( Doxygen )
if ( DOXYGEN_FOUND )
  configure_file(
    ${ldms_SOURCE_DIR}/Doxyfile.in
    ${ldms_BINARY_DIR}/Doxyfile
    @ONLY
  )
  add_custom_target( docs-ldms
    COMMAND ${DOXYGEN_EXECUTABLE} ${ldms_BINARY_DIR}/Doxyfile
    DEPENDS ${ldms_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${ldms_BINARY_DIR}
    COMMENT "Generating developer documentation"
  )
endif ( DOXYGEN_FOUND )

# Install rules
# Rules for targets
set( ldms_INSTALL_TARGETS "${ldms_INSTALL_TARGETS};${ldms_plugins}" )
install(
  TARGETS ${ldms_INSTALL_TARGETS}
  EXPORT ldms
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
# Rule to install auto-generated configuration information for projects that wish to link to or run LDMS targets:
install(
  EXPORT ldms
  DESTINATION lib/ldms
)
# Rule for headers
install(
  DIRECTORY ${ldms_BINARY_DIR}/include/ldms
  DESTINATION include
  COMPONENT devel
  FILES_MATCHING PATTERN "*.h"
  PATTERN "CMakeFiles" EXCLUDE )

# Rule for Doxygen docs
# FIXME: No way to make docs-ldms a dependency of install target.
if ( DOXYGEN_FOUND )
  install(
    DIRECTORY ${ldms_BINARY_DIR}/doc/html
    DESTINATION share/doc/ldms/developer
    COMPONENT devel
    PATTERN "*"
  )
endif ( DOXYGEN_FOUND )
