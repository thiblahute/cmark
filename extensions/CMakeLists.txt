cmake_minimum_required(VERSION 2.8)
set(LIBRARY "cmarkextensions")
set(STATICLIBRARY "cmarkextensions_static")

set(LIBRARY_SOURCES
    core-extensions.c
    ext_scanners.c
    ext_scanners.h
   )

include_directories(
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_BINARY_DIR}/src
)

# We make LIB_INSTALL_DIR configurable rather than
# hard-coding lib, because on some OSes different locations
# are used for different architectures (e.g. /usr/lib64 on
# 64-bit Fedora).
if(NOT LIB_INSTALL_DIR)
  set(LIB_INSTALL_DIR "lib" CACHE STRING
  "Set the installation directory for libraries." FORCE)
endif(NOT LIB_INSTALL_DIR)

include_directories(. ${CMAKE_CURRENT_BINARY_DIR})

set(CMAKE_C_FLAGS_PROFILE "${CMAKE_C_FLAGS_RELEASE} -pg")
set(CMAKE_LINKER_PROFILE "${CMAKE_LINKER_FLAGS_RELEASE} -pg")

add_library(${LIBRARY} STATIC SHARED ${LIBRARY_SOURCES})

if (CMARK_STATIC)
  add_library(${STATICLIBRARY} STATIC ${LIBRARY_SOURCES})
  set_target_properties(${STATICLIBRARY} PROPERTIES
    COMPILE_FLAGS -DCMARK_STATIC_DEFINE
    POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(cmarkextensions_static libcmark_static)
  target_link_libraries(${STATICLIBRARY} ${CMAKE_DL_LIBS})
endif(CMARK_STATIC)

target_link_libraries(cmarkextensions libcmark)
