# Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Pseudalis: the protection of the personal data of documents, built on dlib/privacy, for
# macOS on Apple Silicon only.
#
#   cmake -S dlib/dlib/external/pseudalis -B build/macos-pseudalis -G Ninja -DCMAKE_BUILD_TYPE=Release
#
# Every program lands in <build>/bin: the service, with its engine inside
# (PSEUDALIS_ENGINE_STATIC) and its drives as folders of the data folder, and the web application.

cmake_minimum_required(VERSION 3.16)
project(pseudalis VERSION 1.0.5 LANGUAGES C CXX)

option(PSEUDALIS_TESTS "Build the test programs" ON)

if (NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|ARM64)$")
   message(FATAL_ERROR "Pseudalis builds for macOS on Apple Silicon only (arm64).")
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
   set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

get_filename_component(PSEUDALIS_DLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(PSEUDALIS_INCLUDE_DIR "${PSEUDALIS_DLIB_DIR}/.." ABSOLUTE)
get_filename_component(PSEUDALIS_ALBIREO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../albireo" ABSOLUTE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(PSEUDALIS_WARNINGS -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

# dlib: its HTTPS target (cpp-httplib and Mbed TLS) for the programs; the engine compiles its
# sources itself.
set(DLIB_NO_GUI_SUPPORT ON CACHE BOOL "" FORCE)
set(DLIB_USE_CUDA OFF CACHE BOOL "" FORCE)
set(DLIB_USE_NPU OFF CACHE BOOL "" FORCE)
set(DLIB_USE_BLAS OFF CACHE BOOL "" FORCE)
set(DLIB_USE_LAPACK OFF CACHE BOOL "" FORCE)
set(DLIB_JPEG_SUPPORT OFF CACHE BOOL "" FORCE)
set(DLIB_PNG_SUPPORT OFF CACHE BOOL "" FORCE)
set(DLIB_GIF_SUPPORT OFF CACHE BOOL "" FORCE)
set(DLIB_WEBP_SUPPORT OFF CACHE BOOL "" FORCE)
set(DLIB_JXL_SUPPORT OFF CACHE BOOL "" FORCE)
set(DLIB_LINK_WITH_SQLITE3 OFF CACHE BOOL "" FORCE)
set(DLIB_USE_FFMPEG OFF CACHE BOOL "" FORCE)
set(DLIB_USE_MKL_FFT OFF CACHE BOOL "" FORCE)
add_subdirectory(${PSEUDALIS_DLIB_DIR} dlib_build EXCLUDE_FROM_ALL)
find_package(Threads REQUIRED)

# The models of the detector: the header the example slm_document_anonymizer_ex embeds.
set(PSEUDALIS_MODELS_HEADER "${PSEUDALIS_INCLUDE_DIR}/examples/slm_document_anonymizer_ex.h" CACHE FILEPATH
   "Header of the models compiled into the engines (slm_document_anonymizer_ex embed)")
if (NOT EXISTS "${PSEUDALIS_MODELS_HEADER}")
   message(FATAL_ERROR "No models header at ${PSEUDALIS_MODELS_HEADER}: set PSEUDALIS_MODELS_HEADER")
endif()

# The build of the programs and of the engines, new at each configuration of the build tree: the
# service loads only an engine of its own build, never one that another installation or another
# build left beside it (common/engine_loader.cpp).
string(TIMESTAMP PSEUDALIS_BUILD_TIME "%Y%m%d%H%M%S" UTC)
string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" PSEUDALIS_BUILD_RANDOM)
set(PSEUDALIS_BUILD_STAMP "${PSEUDALIS_BUILD_TIME}-${PSEUDALIS_BUILD_RANDOM}")
configure_file(cmake/build_stamp.h.in ${CMAKE_BINARY_DIR}/generated/build_stamp.h @ONLY)

# ---------------------------------------------------------------------------------------------
# The engine, compiled into the service and into test_engine.

set(PSEUDALIS_ENGINE_SOURCES engine/engine.cpp engine/archives.cpp engine/engine_api.cpp
   ${PSEUDALIS_DLIB_DIR}/all/source.cpp)
# The sources of dlib keep the warnings of their own build.
set_source_files_properties(${PSEUDALIS_DLIB_DIR}/all/source.cpp PROPERTIES COMPILE_OPTIONS "-w")
set(PSEUDALIS_ENGINE_DEFINITIONS PSEUDALIS_ENGINE_BUILD DLIB_NO_GUI_SUPPORT DLIB_DISABLE_ASSERTS
   "PSEUDALIS_MODELS_HEADER=\"${PSEUDALIS_MODELS_HEADER}\"")

# ---------------------------------------------------------------------------------------------
# The programs.

add_library(pseudalis_sqlite STATIC ${PSEUDALIS_DLIB_DIR}/external/sqlite/sqlite3.c)
target_include_directories(pseudalis_sqlite PUBLIC ${PSEUDALIS_DLIB_DIR}/external/sqlite)
target_compile_definitions(pseudalis_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_DQS=0 SQLITE_DEFAULT_MEMSTATUS=0
   SQLITE_DEFAULT_WAL_SYNCHRONOUS=1 SQLITE_LIKE_DOESNT_MATCH_BLOBS SQLITE_TEMP_STORE=2 SQLITE_SECURE_DELETE
   SQLITE_ENABLE_API_ARMOR SQLITE_OMIT_LOAD_EXTENSION)
target_link_libraries(pseudalis_sqlite PUBLIC ${CMAKE_DL_LIBS} m)

set(zlib_dir ${PSEUDALIS_DLIB_DIR}/external/zlib)
add_library(pseudalis_zlib STATIC ${zlib_dir}/adler32.c ${zlib_dir}/compress.c ${zlib_dir}/crc32.c
   ${zlib_dir}/deflate.c ${zlib_dir}/infback.c ${zlib_dir}/inffast.c ${zlib_dir}/inflate.c ${zlib_dir}/inftrees.c
   ${zlib_dir}/trees.c ${zlib_dir}/uncompr.c ${zlib_dir}/zutil.c)
target_include_directories(pseudalis_zlib PUBLIC ${zlib_dir})
target_compile_options(pseudalis_zlib PRIVATE -w)

# Code shared by the service and the tools.
add_library(pseudalis_common STATIC common/log.cpp common/platform.cpp)
target_include_directories(pseudalis_common PUBLIC ${PSEUDALIS_INCLUDE_DIR})
target_compile_options(pseudalis_common PRIVATE ${PSEUDALIS_WARNINGS})
target_link_libraries(pseudalis_common PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# The public key of the update feed, which pseudalis-sign keygen writes once, beside the private
# key that the publisher keeps outside the repository (README.md, Publishing a version).
set(PSEUDALIS_FEED_PUBLIC_KEY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/feed-public-key.pem" CACHE FILEPATH
   "Public key (PEM) of the update feed")
set(PSEUDALIS_FEED_PUBLIC_KEY_TEXT "")
if (EXISTS "${PSEUDALIS_FEED_PUBLIC_KEY_FILE}")
   set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PSEUDALIS_FEED_PUBLIC_KEY_FILE}")
   file(STRINGS "${PSEUDALIS_FEED_PUBLIC_KEY_FILE}" feed_key_lines)
   foreach (line ${feed_key_lines})
      string(APPEND PSEUDALIS_FEED_PUBLIC_KEY_TEXT "${line}\\n")
   endforeach()
   message(STATUS "Update feed: public key read from ${PSEUDALIS_FEED_PUBLIC_KEY_FILE}")
else()
   message(STATUS "Update feed: no public key at ${PSEUDALIS_FEED_PUBLIC_KEY_FILE}, the programs install no update")
endif()
configure_file(cmake/feed_public_key.h.in ${CMAKE_BINARY_DIR}/generated/feed_public_key.h @ONLY)

# The service, with the engine inside.
set(PSEUDALIS_SERVICE_SOURCES common/engine_loader.cpp host/main.cpp host/service.cpp host/server.cpp
   host/settings.cpp host/store.cpp host/vault.cpp host/keys.cpp host/albireo_client.cpp
   host/http_fetch.cpp host/crypto.cpp host/certificates.cpp
   # The drives: two folders of the data folder, watched by the service.
   host/drive_folders.cpp ${PSEUDALIS_ENGINE_SOURCES})
add_executable(pseudalis-service ${PSEUDALIS_SERVICE_SOURCES})
target_include_directories(pseudalis-service PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(pseudalis-service PRIVATE pseudalis_common pseudalis_sqlite dlib::httplib)
target_compile_definitions(pseudalis-service PRIVATE PSEUDALIS_ENGINE_STATIC ${PSEUDALIS_ENGINE_DEFINITIONS})
target_compile_options(pseudalis-service PRIVATE ${PSEUDALIS_WARNINGS})

# The signatures of the update feed, for its publisher.
add_executable(pseudalis-sign tools/pseudalis_sign.cpp host/crypto.cpp)
target_include_directories(pseudalis-sign PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(pseudalis-sign PRIVATE pseudalis_common dlib::httplib)
target_compile_options(pseudalis-sign PRIVATE ${PSEUDALIS_WARNINGS})

# The web application and the documentation of the interface, next to the service; Swagger UI
# comes from the copy of Albireo in this repository.
set(swagger_dir ${PSEUDALIS_ALBIREO_DIR}/web/docs)
add_custom_target(pseudalis-web ALL
   COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/bin/web
   COMMAND ${CMAKE_COMMAND} -E copy ${swagger_dir}/swagger-ui-bundle.js ${swagger_dir}/swagger-ui.css
      ${swagger_dir}/LICENSE ${swagger_dir}/NOTICE ${CMAKE_BINARY_DIR}/bin/web/docs
   COMMENT "Copying the web application")
add_dependencies(pseudalis-service pseudalis-web)

# ---------------------------------------------------------------------------------------------
# Tests.

if (PSEUDALIS_TESTS)
   add_executable(test_engine tests/test_engine.cpp ${PSEUDALIS_ENGINE_SOURCES})
   target_include_directories(test_engine PRIVATE ${PSEUDALIS_INCLUDE_DIR} ${CMAKE_BINARY_DIR}/generated)
   target_compile_definitions(test_engine PRIVATE ${PSEUDALIS_ENGINE_DEFINITIONS})
   target_link_libraries(test_engine PRIVATE Threads::Threads)
   add_executable(test_host tests/test_host.cpp host/vault.cpp host/keys.cpp host/store.cpp host/crypto.cpp
      host/settings.cpp)
   target_include_directories(test_host PRIVATE ${CMAKE_BINARY_DIR}/generated)
   target_link_libraries(test_host PRIVATE pseudalis_common pseudalis_sqlite dlib::httplib)
   target_compile_options(test_host PRIVATE ${PSEUDALIS_WARNINGS})
endif()
