# Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Pseudalis : la protection des données personnelles des documents, bâtie sur dlib/privacy, pour
# macOS sur Apple Silicon seulement.
#
#   cmake -S dlib/dlib/external/pseudalis -B build/macos-pseudalis -G Ninja -DCMAKE_BUILD_TYPE=Release
#
# Chaque programme arrive dans <build>/bin : le service, avec son moteur intégré
# (PSEUDALIS_ENGINE_STATIC) et ses lecteurs sous forme de dossiers du dossier de données, et
# l'application web.

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

# dlib : sa cible HTTPS (cpp-httplib et Mbed TLS) pour les programmes ; le moteur compile lui-même
# ses sources.
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

# Les modèles du détecteur : l'en-tête qu'embarque l'exemple slm_document_anonymizer_ex.
set(PSEUDALIS_MODELS_HEADER "${PSEUDALIS_INCLUDE_DIR}/examples/slm_document_anonymizer_ex.h" CACHE FILEPATH
   "Header of the models compiled into the engines (slm_document_anonymizer_ex embed)")
if (NOT EXISTS "${PSEUDALIS_MODELS_HEADER}")
   message(FATAL_ERROR "No models header at ${PSEUDALIS_MODELS_HEADER}: set PSEUDALIS_MODELS_HEADER")
endif()

# Le build des programmes et des moteurs, nouveau à chaque configuration de l'arbre de build : le
# service ne charge qu'un moteur de son propre build, jamais un moteur qu'une autre installation ou
# un autre build aurait laissé à côté (common/engine_loader.cpp).
string(TIMESTAMP PSEUDALIS_BUILD_TIME "%Y%m%d%H%M%S" UTC)
string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" PSEUDALIS_BUILD_RANDOM)
set(PSEUDALIS_BUILD_STAMP "${PSEUDALIS_BUILD_TIME}-${PSEUDALIS_BUILD_RANDOM}")
configure_file(cmake/build_stamp.h.in ${CMAKE_BINARY_DIR}/generated/build_stamp.h @ONLY)

# ---------------------------------------------------------------------------------------------
# Le moteur, compilé dans le service et dans test_engine.

set(PSEUDALIS_ENGINE_SOURCES engine/engine.cpp engine/archives.cpp engine/engine_api.cpp
   ${PSEUDALIS_DLIB_DIR}/all/source.cpp)
# Les sources de dlib gardent les avertissements de leur propre build.
set_source_files_properties(${PSEUDALIS_DLIB_DIR}/all/source.cpp PROPERTIES COMPILE_OPTIONS "-w")
set(PSEUDALIS_ENGINE_DEFINITIONS PSEUDALIS_ENGINE_BUILD DLIB_NO_GUI_SUPPORT DLIB_DISABLE_ASSERTS
   "PSEUDALIS_MODELS_HEADER=\"${PSEUDALIS_MODELS_HEADER}\"")

# ---------------------------------------------------------------------------------------------
# Les programmes.

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

# Code partagé par le service et les outils.
add_library(pseudalis_common STATIC common/log.cpp common/platform.cpp)
target_include_directories(pseudalis_common PUBLIC ${PSEUDALIS_INCLUDE_DIR})
target_compile_options(pseudalis_common PRIVATE ${PSEUDALIS_WARNINGS})
target_link_libraries(pseudalis_common PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# Le service, avec le moteur intégré.
set(PSEUDALIS_SERVICE_SOURCES common/engine_loader.cpp host/main.cpp host/service.cpp host/server.cpp
   host/settings.cpp host/store.cpp host/vault.cpp host/keys.cpp host/albireo_client.cpp
   host/http_fetch.cpp host/crypto.cpp host/certificates.cpp
   # Les lecteurs : deux dossiers du dossier de données, surveillés par le service.
   host/drive_folders.cpp ${PSEUDALIS_ENGINE_SOURCES})
add_executable(pseudalis-service ${PSEUDALIS_SERVICE_SOURCES})
target_include_directories(pseudalis-service PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(pseudalis-service PRIVATE pseudalis_common pseudalis_sqlite dlib::httplib)
target_compile_definitions(pseudalis-service PRIVATE PSEUDALIS_ENGINE_STATIC ${PSEUDALIS_ENGINE_DEFINITIONS})
target_compile_options(pseudalis-service PRIVATE ${PSEUDALIS_WARNINGS})

# L'application web et la documentation de l'interface, à côté du service ; Swagger UI provient
# de la copie d'Albireo présente dans ce dépôt.
set(swagger_dir ${PSEUDALIS_ALBIREO_DIR}/web/docs)
add_custom_target(pseudalis-web ALL
   COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/bin/web
   COMMAND ${CMAKE_COMMAND} -E copy ${swagger_dir}/swagger-ui-bundle.js ${swagger_dir}/swagger-ui.css
      ${swagger_dir}/LICENSE ${swagger_dir}/NOTICE ${CMAKE_BINARY_DIR}/bin/web/docs
   COMMENT "Copying the web application")
add_dependencies(pseudalis-service pseudalis-web)

# ---------------------------------------------------------------------------------------------
# Les tests.

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
