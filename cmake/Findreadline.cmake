# Find libreadline
#
# Upon return, the following will be defined:
# readline_FOUND
# readline_INCLUDE_DIR
# readline_LIBRARIES

if ( readline_INCLUDE_DIR AND readline_LIBRARY )
  set( readline_FIND_QUIETLY TRUE )
endif()

find_path( readline_INCLUDE_DIR readline.h
  PATHS
    /usr/local/include
    /usr/include
  PATH_SUFFIXES readline
)

find_library( readline_LIBRARY
  NAMES readline
  PATHS
    /usr/local/lib
    /usr/lib
)

set( readline_LIBRARIES "${readline_LIBRARY}" )

include( FindPackageHandleStandardArgs )
find_package_handle_standard_args( readline
  DEFAULT_MSG
  readline_INCLUDE_DIR
  readline_LIBRARIES
)

if ( readline_INCLUDE_DIR AND readline_LIBRARIES )
  set( readline_FOUND 1 )
endif ( readline_INCLUDE_DIR AND readline_LIBRARIES )

mark_as_advanced(
  readline_INCLUDE_DIR
  readline_LIBRARY
  readline_LIBRARIES
)
