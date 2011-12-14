# Find libpapi
#
# Upon return, the following will be defined:
# papi_FOUND
# papi_INCLUDE_DIR
# papi_LIBRARIES

if ( papi_INCLUDE_DIR AND papi_LIBRARY )
  set( papi_FIND_QUIETLY TRUE )
endif()

find_path( papi_INCLUDE_DIR papi.h
  PATHS
    /usr/local/include
    /usr/include
)

find_library( papi_LIBRARY
  NAMES papi
  PATHS
    /usr/local/lib
    /usr/lib
)

set( papi_LIBRARIES "${papi_LIBRARY}" )

include( FindPackageHandleStandardArgs )
find_package_handle_standard_args( papi
  DEFAULT_MSG
  papi_INCLUDE_DIR
  papi_LIBRARIES
)

mark_as_advanced(
  papi_INCLUDE_DIR
  papi_LIBRARY
  papi_LIBRARIES
)

