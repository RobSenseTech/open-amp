
set_property (GLOBAL PROPERTY "PROJECT_LIB_EXTRA_CFLAGS")

collector_create (PROJECT_LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}")
collect (PROJECT_LIB_DIRS "${CMAKE_CURRENT_BINARY_DIR}")
collect (PROJECT_INC_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include")


add_subdirectory (common)
add_subdirectory (virtio)
add_subdirectory (rpmsg)
add_subdirectory (remoteproc)

if (WITH_PROXY)
  add_subdirectory (proxy)
endif (WITH_PROXY)

set (OPENAMP_LIB open_amp)

collector_list (_include PROJECT_INC_DIRS)
include_directories (${_include})

collector_list (_deps PROJECT_LIB_DEPS)

get_property (_ecflags GLOBAL PROPERTY "PROJECT_LIB_EXTRA_CFLAGS")

collector_list (_sources PROJECT_LIB_SOURCES)
set_property (SOURCE ${_sources}
  APPEND_STRING PROPERTY COMPILE_FLAGS " ${_ecflags}")

# Build a shared library if so configured.
if (WITH_SHARED_LIB)
  set (_lib ${OPENAMP_LIB}-shared)
  add_library (${_lib} SHARED ${_sources})
  target_link_libraries (${_lib} ${_deps})
  install (TARGETS ${_lib} LIBRARY DESTINATION lib)
  set_target_properties (${_lib} PROPERTIES
    OUTPUT_NAME       "${OPENAMP_LIB}"
    VERSION           "${PROJECT_VER}"
    SOVERSION         "${PROJECT_VER_MAJOR}"
  )
endif (WITH_SHARED_LIB)

if (WITH_STATIC_LIB)
  set (_lib ${OPENAMP_LIB}-static)
  add_library (${_lib} STATIC ${_sources})
  install (TARGETS ${_lib} ARCHIVE DESTINATION lib)
  set_target_properties (${_lib} PROPERTIES
    OUTPUT_NAME       "${OPENAMP_LIB}"
  )
endif (WITH_STATIC_LIB)

install (DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/openamp" DESTINATION include)

# vim: expandtab:ts=2:sw=2:smartindent
