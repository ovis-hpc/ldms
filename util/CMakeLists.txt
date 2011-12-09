if ( NOT ldms_USE_SYSTEM_LIBEVENT )
  add_subdirectory( libevent )
  set( EVENT_LIBRARIES "event" CACHE INTERNAL "Project-local build of libevent" FORCE PARENT_SCOPE )
  set( EVENT_INCLUDE_DIRS
    "${ldms_SOURCE_DIR}/util/libevent/include;${ldms_BINARY_DIR}/util/libevent/include;${ldms_SOURCE_DIR}/util/libevent"
    CACHE INTERNAL "Project-local libevent includes" FORCE PARENT_SCOPE )
endif ( NOT ldms_USE_SYSTEM_LIBEVENT )
