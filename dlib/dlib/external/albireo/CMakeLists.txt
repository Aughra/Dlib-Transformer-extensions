# Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Albireo: the local inference service built on dlib, for macOS on Apple Silicon only.
#
#   cmake -S dlib/dlib/external/albireo -B build/macos-albireo -G Ninja -DCMAKE_BUILD_TYPE=Release
#
# Every program lands in <build>/bin and the engine in <build>/bin/engines; the web application
# is copied to <build>/bin/web and the voices of the speech synthesis to <build>/bin/speech.

cmake_minimum_required(VERSION 3.16)
project(albireo VERSION 1.0.12 LANGUAGES C CXX OBJCXX)

# The version is written in four places that must agree: project() above, the numbers and the
# text of common/version.h, which the service and the engine give, and the description of the
# interface.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/common/version.h" version_lines REGEX "^#define ALBIREO_VERSION")
set(version_numbers)
foreach (part MAJOR MINOR PATCH)
   string(REGEX MATCH "#define ALBIREO_VERSION_${part} +([0-9]+)" found "${version_lines}")
   list(APPEND version_numbers "${CMAKE_MATCH_1}")
endforeach()
string(REPLACE ";" "." version_numbers "${version_numbers}")
string(REGEX MATCH "#define ALBIREO_VERSION +\"([0-9.]+)\"" found "${version_lines}")
set(version_text "${CMAKE_MATCH_1}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/openapi.json" openapi_text)
string(REGEX MATCH "\"version\" *: *\"([0-9.]+)\"" found "${openapi_text}")
set(openapi_version "${CMAKE_MATCH_1}")
if (NOT version_numbers STREQUAL PROJECT_VERSION OR NOT version_text STREQUAL PROJECT_VERSION OR
    NOT openapi_version STREQUAL PROJECT_VERSION)
   message(FATAL_ERROR "the version differs between project() (${PROJECT_VERSION}), the numbers of "
      "common/version.h (${version_numbers}), its text (${version_text}) and web/openapi.json "
      "(${openapi_version}): the four must agree")
endif()

option(ALBIREO_PROGRAMS "Build the service, the engine and the tools" ON)
option(ALBIREO_TESTS "Build the test programs" ON)

if (NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|ARM64)$")
   message(FATAL_ERROR "Albireo builds for macOS on Apple Silicon only (arm64).")
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
   set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

get_filename_component(ALBIREO_DLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(ALBIREO_INCLUDE_DIR "${ALBIREO_DLIB_DIR}/.." ABSOLUTE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(ALBIREO_ENGINE_DIR ${CMAKE_BINARY_DIR}/bin/engines)

# Puts the executables of a target in a folder of the build tree.
function(albireo_output_dir target dir)
   set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${dir})
endfunction()

# dlib: the programs use its headers and its HTTPS target only.  The engine has its own
# kernels, so dlib needs no BLAS.
set(DLIB_NO_GUI_SUPPORT ON CACHE BOOL "" FORCE)
set(DLIB_USE_CUDA OFF CACHE BOOL "" FORCE)
set(DLIB_USE_NPU OFF CACHE BOOL "" FORCE)
set(DLIB_BACKENDS "CPU" CACHE STRING "" FORCE)
set(DLIB_USE_BLAS OFF CACHE BOOL "" FORCE)
set(DLIB_USE_LAPACK OFF CACHE BOOL "" FORCE)
set(DLIB_JPEG_SUPPORT OFF CACHE BOOL "" FORCE)
set(DLIB_PNG_SUPPORT OFF CACHE BOOL "" FORCE)
set(DLIB_LINK_WITH_SQLITE3 OFF CACHE BOOL "" FORCE)
set(DLIB_USE_FFMPEG OFF CACHE BOOL "" FORCE)
set(DLIB_JXL_SUPPORT OFF CACHE BOOL "" FORCE)
set(DLIB_WEBP_SUPPORT OFF CACHE BOOL "" FORCE)
set(DLIB_GIF_SUPPORT OFF CACHE BOOL "" FORCE)
set(DLIB_USE_MKL_FFT OFF CACHE BOOL "" FORCE)
add_subdirectory(${ALBIREO_DLIB_DIR} dlib_build EXCLUDE_FROM_ALL)

set(ALBIREO_WARNINGS -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
find_package(Threads REQUIRED)
# The products of the long passes go through Accelerate (engine/qkernels_accelerate.inc); the
# GPU through Metal (engine/metal_device.mm).
link_libraries("-framework Accelerate" "-framework Metal" "-framework MetalPerformanceShaders" "-framework Foundation")

# The source of the Metal kernels, embedded in the engine as a string that macOS compiles when
# the engine opens the GPU: the build needs no Metal compiler.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/engine/metal_kernels.metal" ALBIREO_METAL_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/engine/metal_kernels.metal")
configure_file(engine/metal_kernels.inc.in ${CMAKE_BINARY_DIR}/generated/metal_kernels.inc @ONLY)
set_source_files_properties(engine/metal_device.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
   INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/generated")

# zlib from dlib/external, for the programs and the image decoders of the engine.
set(zlib_dir ${ALBIREO_DLIB_DIR}/external/zlib)
add_library(albireo_zlib STATIC ${zlib_dir}/adler32.c ${zlib_dir}/compress.c ${zlib_dir}/crc32.c
   ${zlib_dir}/deflate.c ${zlib_dir}/infback.c ${zlib_dir}/inffast.c ${zlib_dir}/inflate.c ${zlib_dir}/inftrees.c
   ${zlib_dir}/trees.c ${zlib_dir}/uncompr.c ${zlib_dir}/zutil.c)
target_include_directories(albireo_zlib PUBLIC ${zlib_dir})

# The PNG and JPEG decoders of the engine, which read the images of the requests: the copies
# of libpng and libjpeg that dlib bundles, compiled into the engine, whatever the system holds.
set(png_dir ${ALBIREO_DLIB_DIR}/external/libpng)
set(jpeg_dir ${ALBIREO_DLIB_DIR}/external/libjpeg)
add_library(albireo_image STATIC
   ${png_dir}/png.c ${png_dir}/pngerror.c ${png_dir}/pngget.c ${png_dir}/pngmem.c ${png_dir}/pngpread.c
   ${png_dir}/pngread.c ${png_dir}/pngrio.c ${png_dir}/pngrtran.c ${png_dir}/pngrutil.c ${png_dir}/pngset.c
   ${png_dir}/pngtrans.c ${png_dir}/pngwio.c ${png_dir}/pngwrite.c ${png_dir}/pngwtran.c ${png_dir}/pngwutil.c
   ${png_dir}/arm/arm_init.c ${png_dir}/arm/filter_neon_intrinsics.c ${png_dir}/arm/palette_neon_intrinsics.c
   ${jpeg_dir}/jaricom.c ${jpeg_dir}/jcomapi.c ${jpeg_dir}/jdapimin.c ${jpeg_dir}/jdapistd.c ${jpeg_dir}/jdarith.c
   ${jpeg_dir}/jdatasrc.c ${jpeg_dir}/jdcoefct.c ${jpeg_dir}/jdcolor.c ${jpeg_dir}/jddctmgr.c ${jpeg_dir}/jdhuff.c
   ${jpeg_dir}/jdinput.c ${jpeg_dir}/jdmainct.c ${jpeg_dir}/jdmarker.c ${jpeg_dir}/jdmaster.c ${jpeg_dir}/jdmerge.c
   ${jpeg_dir}/jdpostct.c ${jpeg_dir}/jdsample.c ${jpeg_dir}/jerror.c ${jpeg_dir}/jidctflt.c ${jpeg_dir}/jidctfst.c
   ${jpeg_dir}/jidctint.c ${jpeg_dir}/jmemmgr.c ${jpeg_dir}/jmemnobs.c ${jpeg_dir}/jquant1.c ${jpeg_dir}/jquant2.c
   ${jpeg_dir}/jutils.c)
target_include_directories(albireo_image PRIVATE ${png_dir} ${jpeg_dir})
target_link_libraries(albireo_image PUBLIC albireo_zlib)
# Compiled into the shared engine, whose own symbols alone are visible.
set_target_properties(albireo_zlib albireo_image PROPERTIES POSITION_INDEPENDENT_CODE ON
   C_VISIBILITY_PRESET hidden)

# The inference engine.
set(ALBIREO_ENGINE_SOURCES
   engine/chat_format.cpp
   engine/cpu_runtime.cpp
   engine/cpu_runtime_metal.cpp
   engine/engine.cpp
   engine/engine_api.cpp
   engine/image_input.cpp
   engine/metal_device.mm
   engine/model_config.cpp
   engine/model_file.cpp
   engine/sampler.cpp
   engine/vision_encoder.cpp
   common/system_info.cpp)

# The quantized kernels (NEON, Accelerate), shared by the engine, the tools and the tests.
add_library(albireo-kernels OBJECT engine/qkernels.cpp)
target_include_directories(albireo-kernels PRIVATE ${ALBIREO_INCLUDE_DIR})
target_compile_options(albireo-kernels PRIVATE ${ALBIREO_WARNINGS})
set_target_properties(albireo-kernels PROPERTIES POSITION_INDEPENDENT_CODE ON
   C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# The engine library, which the service loads from its engines folder.
function(albireo_engine variant)
   set(target albireo-engine-${variant})
   add_library(${target} SHARED ${ALBIREO_ENGINE_SOURCES} $<TARGET_OBJECTS:albireo-kernels> ${ARGN})
   target_include_directories(${target} PRIVATE ${ALBIREO_INCLUDE_DIR})
   target_compile_definitions(${target} PRIVATE ALBIREO_ENGINE_BUILD ALBIREO_ENGINE_VARIANT="${variant}")
   target_compile_options(${target} PRIVATE ${ALBIREO_WARNINGS})
   target_link_libraries(${target} PRIVATE albireo_image Threads::Threads ${CMAKE_DL_LIBS})
   set_target_properties(${target} PROPERTIES
      C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
      LIBRARY_OUTPUT_DIRECTORY ${ALBIREO_ENGINE_DIR})
endfunction()

if (ALBIREO_PROGRAMS)
   albireo_engine(cpu)
endif()

if (ALBIREO_PROGRAMS)
   # Libraries compiled into the programs: SQLite and zlib (above) from dlib/external.
   add_library(albireo_sqlite STATIC ${ALBIREO_DLIB_DIR}/external/sqlite/sqlite3.c)
   target_include_directories(albireo_sqlite PUBLIC ${ALBIREO_DLIB_DIR}/external/sqlite)
   target_compile_definitions(albireo_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_DQS=0 SQLITE_DEFAULT_MEMSTATUS=0
      SQLITE_DEFAULT_WAL_SYNCHRONOUS=1 SQLITE_LIKE_DOESNT_MATCH_BLOBS SQLITE_TEMP_STORE=2 SQLITE_SECURE_DELETE
      SQLITE_ENABLE_API_ARMOR SQLITE_ENABLE_FTS5 SQLITE_ENABLE_MATH_FUNCTIONS SQLITE_OMIT_LOAD_EXTENSION)
   target_link_libraries(albireo_sqlite PUBLIC ${CMAKE_DL_LIBS} m)

   # Code shared by the service and the tools.
   add_library(albireo_common STATIC common/log.cpp common/platform.cpp common/system_info.cpp
      common/engine_loader.cpp)
   target_include_directories(albireo_common PUBLIC ${ALBIREO_INCLUDE_DIR})
   target_compile_options(albireo_common PRIVATE ${ALBIREO_WARNINGS})
   target_link_libraries(albireo_common PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

   # The speech synthesis that reads the answers aloud: SVOX Pico in its own program,
   # albireo-speech, which the service starts for each reading, and its voices of six languages.
   set(pico_dir ${ALBIREO_DLIB_DIR}/external/svox_pico)
   file(GLOB pico_sources ${pico_dir}/lib/*.c)
   add_library(albireo_pico STATIC ${pico_sources})
   target_include_directories(albireo_pico PUBLIC ${pico_dir}/lib)
   # Third-party code of 2009, compiled as it is published: its warnings are not ours to fix.
   target_compile_options(albireo_pico PRIVATE -w)

   # The program of the speech synthesis: the synthesizer and the runtime of the compiler, no
   # other library (host/speech.h).
   add_executable(albireo-speech host/speech_worker.cpp host/speech_text.cpp)
   target_link_libraries(albireo-speech PRIVATE albireo_pico)
   target_include_directories(albireo-speech PRIVATE ${ALBIREO_INCLUDE_DIR})
   target_compile_options(albireo-speech PRIVATE ${ALBIREO_WARNINGS})

   # The password of the public link of the published files (host/public_link.h), read from a
   # file outside the repository at each configuration and built into the service, masked: the
   # header generated lies in the build tree alone.  Without the file, the service reaches the
   # link without password.
   set(public_link_default "$ENV{HOME}/keys/albireo-public-link.txt")
   set(ALBIREO_PUBLIC_LINK_PASSWORD_FILE "${public_link_default}" CACHE FILEPATH
      "File holding the password of the public link of the published files")
   set(public_link_bytes "0")
   set(public_link_size 0)
   if (EXISTS "${ALBIREO_PUBLIC_LINK_PASSWORD_FILE}")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ALBIREO_PUBLIC_LINK_PASSWORD_FILE}")
      file(READ "${ALBIREO_PUBLIC_LINK_PASSWORD_FILE}" public_link_hex HEX)
      # A byte order mark before the password and the ends of line after it are not part of it.
      string(REGEX REPLACE "^efbbbf" "" public_link_hex "${public_link_hex}")
      string(REGEX REPLACE "(0d|0a)+$" "" public_link_hex "${public_link_hex}")
      string(LENGTH "${public_link_hex}" public_link_digits)
      math(EXPR public_link_size "${public_link_digits} / 2")
      if (public_link_size GREATER 0)
         # Each byte masked by the generator that host/public_link.cpp runs again to unmask it.
         set(public_link_seed 20261101)
         set(public_link_list "")
         math(EXPR public_link_last "${public_link_size} - 1")
         foreach (i RANGE 0 ${public_link_last})
            math(EXPR public_link_at "${i} * 2")
            string(SUBSTRING "${public_link_hex}" ${public_link_at} 2 public_link_byte)
            math(EXPR public_link_seed "(${public_link_seed} * 1103515245 + 12345) % 2147483648")
            math(EXPR public_link_byte "0x${public_link_byte} ^ ((${public_link_seed} >> 16) & 255)")
            list(APPEND public_link_list ${public_link_byte})
         endforeach()
         string(REPLACE ";" ", " public_link_bytes "${public_link_list}")
      endif()
      message(STATUS "Public link: password read from ${ALBIREO_PUBLIC_LINK_PASSWORD_FILE}")
   else()
      message(STATUS "Public link: no password, ${ALBIREO_PUBLIC_LINK_PASSWORD_FILE} does not exist")
   endif()
   configure_file(cmake/public_link_secret.h.in ${CMAKE_BINARY_DIR}/generated/public_link_secret.h @ONLY)

   # The service.
   add_executable(albireo-service host/main.cpp host/service.cpp host/server.cpp host/settings.cpp host/store.cpp
      host/models.cpp host/engine_host.cpp host/http_fetch.cpp host/public_link.cpp host/crypto.cpp
      host/certificates.cpp host/speech.cpp host/speech_text.cpp host/web_access.cpp)
   target_include_directories(albireo-service PRIVATE ${CMAKE_BINARY_DIR}/generated)
   target_link_libraries(albireo-service PRIVATE albireo_common albireo_sqlite albireo_zlib dlib::httplib)
   target_compile_options(albireo-service PRIVATE ${ALBIREO_WARNINGS})

   # The signatures of the update feed and of the model catalog, for their publisher.
   add_executable(albireo-sign tools/albireo_sign.cpp host/crypto.cpp)
   target_link_libraries(albireo-sign PRIVATE albireo_common dlib::httplib)
   target_compile_options(albireo-sign PRIVATE ${ALBIREO_WARNINGS})

   # The preparation of the models that come with Albireo: checked and converted with the
   # code of the engine, then given to the programs of the tree.
   add_executable(albireo-builtin tools/albireo_builtin.cpp engine/chat_format.cpp engine/model_config.cpp
      engine/model_file.cpp $<TARGET_OBJECTS:albireo-kernels> host/crypto.cpp)
   target_link_libraries(albireo-builtin PRIVATE albireo_common dlib::httplib)
   target_compile_options(albireo-builtin PRIVATE ${ALBIREO_WARNINGS})

   # The models that come with Albireo (resources/builtin_models.json): downloaded once into
   # the cache (cmake/builtin_models.cmake), checked and converted by albireo-builtin, then
   # given to the "models" folder of the programs of the tree.
   option(ALBIREO_BUILTIN_MODELS "Prepare the models that come with Albireo" OFF)
   set(cache_default "$ENV{HOME}/Library/Caches/albireo")
   set(ALBIREO_MODEL_CACHE "${cache_default}" CACHE PATH
      "Folder of the downloaded and converted models that come with Albireo")
   set(ALBIREO_BUILTIN_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/resources/builtin_models.json" CACHE FILEPATH
      "Manifest of the models that come with Albireo")
   if (ALBIREO_BUILTIN_MODELS)
      if (CMAKE_VERSION VERSION_LESS 3.19)
         message(FATAL_ERROR "the models that come with Albireo need CMake 3.19 or later "
            "(-DALBIREO_BUILTIN_MODELS=OFF builds without them)")
      endif()
      set(ALBIREO_BUILTIN_COMMAND $<TARGET_FILE:albireo-builtin>)
      set(builtin_tool_target albireo-builtin)
      set(ALBIREO_BUILTIN_STAMP ${CMAKE_BINARY_DIR}/bin/models/builtin.json)
      add_custom_command(OUTPUT ${ALBIREO_BUILTIN_STAMP}
         COMMAND ${CMAKE_COMMAND} -DMANIFEST=${ALBIREO_BUILTIN_MANIFEST} -DCACHE=${ALBIREO_MODEL_CACHE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/builtin_models.cmake
         COMMAND ${ALBIREO_BUILTIN_COMMAND} --manifest ${ALBIREO_BUILTIN_MANIFEST} --cache ${ALBIREO_MODEL_CACHE}
            --models-out ${CMAKE_BINARY_DIR}/bin/models
         DEPENDS ${ALBIREO_BUILTIN_MANIFEST} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/builtin_models.cmake
            ${builtin_tool_target}
         COMMENT "Preparing the models that come with Albireo"
         VERBATIM)
      add_custom_target(albireo-builtin-models ALL DEPENDS ${ALBIREO_BUILTIN_STAMP})
   endif()

   # The web application and the documentation of the interface, next to the service.
   add_custom_target(albireo-web ALL
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/bin/web
      COMMENT "Copying the web application")
   add_dependencies(albireo-service albireo-web)
   # The program and the voices of the speech synthesis, next to the service.
   add_custom_target(albireo-speech-voices ALL
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${pico_dir}/lang ${CMAKE_BINARY_DIR}/bin/speech
      COMMENT "Copying the voices of the speech synthesis")
   add_dependencies(albireo-service albireo-speech albireo-speech-voices)

endif()

if (ALBIREO_TESTS AND ALBIREO_PROGRAMS)
   # The files the service writes in its models folder and reads in its imports, the downloads and the
   # updates from the public link, the settings and the web access, with a local server.
   add_executable(test_host tests/test_host.cpp host/models.cpp host/store.cpp host/http_fetch.cpp
      host/public_link.cpp host/crypto.cpp host/settings.cpp host/web_access.cpp)
   target_include_directories(test_host PRIVATE ${CMAKE_BINARY_DIR}/generated)
   target_link_libraries(test_host PRIVATE albireo_common albireo_sqlite albireo_zlib dlib::httplib)
   target_compile_options(test_host PRIVATE ${ALBIREO_WARNINGS})
   albireo_output_dir(test_host ${CMAKE_BINARY_DIR}/tests)
   # test_model_file: damaged model files; test_engine_api: the C interface of an engine library.
   foreach (t test_qkernels test_runtime test_tokenizer engine_chat bench_runtime test_model_file test_engine_api)
      if (t STREQUAL "engine_chat" OR t STREQUAL "test_engine_api")
         add_executable(${t} tests/${t}.cpp)
         target_link_libraries(${t} PRIVATE albireo_common)
      else()
         add_executable(${t} tests/${t}.cpp engine/cpu_runtime.cpp engine/cpu_runtime_metal.cpp engine/metal_device.mm
            engine/model_config.cpp engine/model_file.cpp $<TARGET_OBJECTS:albireo-kernels> common/system_info.cpp)
         target_include_directories(${t} PRIVATE ${ALBIREO_INCLUDE_DIR})
         target_link_libraries(${t} PRIVATE Threads::Threads)
      endif()
      albireo_output_dir(${t} ${CMAKE_BINARY_DIR}/tests)
   endforeach()
   # The Metal kernels against the processor, on random data (macOS port).
   add_executable(test_metal tests/test_metal.cpp engine/metal_device.mm $<TARGET_OBJECTS:albireo-kernels>
      common/system_info.cpp)
   target_include_directories(test_metal PRIVATE ${ALBIREO_INCLUDE_DIR})
   target_compile_options(test_metal PRIVATE ${ALBIREO_WARNINGS})
   target_link_libraries(test_metal PRIVATE Threads::Threads)
   albireo_output_dir(test_metal ${CMAKE_BINARY_DIR}/tests)
   # The chat format of the agents: the prompts with tools, and the tool calls read back.
   add_executable(test_chat_format tests/test_chat_format.cpp engine/chat_format.cpp)
   target_include_directories(test_chat_format PRIVATE ${ALBIREO_INCLUDE_DIR})
   albireo_output_dir(test_chat_format ${CMAKE_BINARY_DIR}/tests)
endif()
