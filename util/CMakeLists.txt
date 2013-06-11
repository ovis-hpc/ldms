if ( NOT ldms_USE_SYSTEM_LIBEVENT )
  set( EVENT_EXPORT_NAME ${ldms_EXPORT_NAME} )
  add_subdirectory( libevent )
  #set( EVENT_LIBRARIES "event" CACHE INTERNAL "Project-local build of libevent" FORCE )
  #set( EVENT_INCLUDE_DIRS
  #  "${ldms_SOURCE_DIR}/util/libevent/include;${ldms_BINARY_DIR}/util/libevent/include;${ldms_SOURCE_DIR}/util/libevent"
  #  CACHE INTERNAL "Project-local libevent includes" FORCE )
  set( EVENT_LIBRARIES "event" PARENT_SCOPE )
  set( EVENT_INCLUDE_DIRS
    "${ldms_SOURCE_DIR}/util/libevent/include;${ldms_BINARY_DIR}/util/libevent/include;${ldms_SOURCE_DIR}/util/libevent"
    PARENT_SCOPE )
endif ( NOT ldms_USE_SYSTEM_LIBEVENT )
