CMAKE_MINIMUM_REQUIRED(VERSION 3.5)
PROJECT(header-parser)

# Force Release (not MinSizeRel): always built for speed, not size.
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Release" "Debug")

SET(SOURCES
  "main.cc"
  "log.cc"
  "log.h"
  "options.h"
  "token.h"
  "tokenizer.cc"
  "tokenizer.h"
  "parser.cc"
  "parser.h"
  "type_node.h"
  "dependency_template.h"
  "postpone_macro.h"
  "bodygeneration_macro.h"
  )

INCLUDE_DIRECTORIES(
	"${PROJECT_SOURCE_DIR}/external/rapidjson/include"
	"${PROJECT_SOURCE_DIR}/external/tclap/include"
	)

macro(use_cxx20)
  if (CMAKE_VERSION VERSION_LESS "3.1")
    if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU" OR
      ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
      add_definitions(-std=c++20)
    endif()
  else ()
    set (CMAKE_CXX_STANDARD 20)
  endif ()
endmacro(use_cxx20)

use_cxx20()

ADD_EXECUTABLE(header-parser ${SOURCES} parser.cc parser.h main.h)
