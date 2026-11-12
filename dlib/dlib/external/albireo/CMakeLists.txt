# Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Albireo : le service d'inférence local construit sur dlib, pour macOS sur Apple Silicon seulement.
#
#   cmake -S dlib/dlib/external/albireo -B build/macos-albireo -G Ninja -DCMAKE_BUILD_TYPE=Release
#
# Chaque programme arrive dans <build>/bin et le moteur dans <build>/bin/engines ; l'application web
# est copiée dans <build>/bin/web.

cmake_minimum_required(VERSION 3.16)
project(albireo VERSION 4.11.8 LANGUAGES C CXX OBJCXX)

# La version est écrite à quatre endroits qui doivent concorder : project() ci-dessus, les numéros et
# le texte de common/version.h, que donnent le service et le moteur, et la description de
# l'interface.
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

# Place les exécutables d'une cible dans un dossier de l'arbre de build.
function(albireo_output_dir target dir)
   set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${dir})
endfunction()

# dlib : les programmes n'utilisent que ses en-têtes et sa cible HTTPS.  Le moteur a ses propres
# noyaux, donc dlib n'a besoin d'aucun BLAS.
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
# Les produits des longues passes passent par Accelerate (engine/qkernels_accelerate.inc) ; le
# GPU par Metal (engine/metal_device.mm).
link_libraries("-framework Accelerate" "-framework Metal" "-framework MetalPerformanceShaders" "-framework Foundation")

# La source des noyaux Metal, intégrée au moteur sous forme d'une chaîne que macOS compile quand
# le moteur ouvre le GPU : le build n'a besoin d'aucun compilateur Metal.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/engine/metal_kernels.metal" ALBIREO_METAL_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/engine/metal_kernels.metal")
configure_file(engine/metal_kernels.inc.in ${CMAKE_BINARY_DIR}/generated/metal_kernels.inc @ONLY)
set_source_files_properties(engine/metal_device.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
   INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/generated")

# zlib, venu de dlib/external, pour les programmes et les décodeurs d'images du moteur.
set(zlib_dir ${ALBIREO_DLIB_DIR}/external/zlib)
add_library(albireo_zlib STATIC ${zlib_dir}/adler32.c ${zlib_dir}/compress.c ${zlib_dir}/crc32.c
   ${zlib_dir}/deflate.c ${zlib_dir}/infback.c ${zlib_dir}/inffast.c ${zlib_dir}/inflate.c ${zlib_dir}/inftrees.c
   ${zlib_dir}/trees.c ${zlib_dir}/uncompr.c ${zlib_dir}/zutil.c)
target_include_directories(albireo_zlib PUBLIC ${zlib_dir})

# Les décodeurs PNG et JPEG du moteur, qui lisent les images des requêtes : les copies de libpng
# et de libjpeg que dlib embarque, compilées dans le moteur, quoi que contienne le système.
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
# Compilé dans le moteur partagé, dont seuls les symboles propres sont visibles.
set_target_properties(albireo_zlib albireo_image PROPERTIES POSITION_INDEPENDENT_CODE ON
   C_VISIBILITY_PRESET hidden)

# Le moteur d'inférence.
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

# Les noyaux quantifiés (NEON, Accelerate), partagés par le moteur, les outils et les tests.
add_library(albireo-kernels OBJECT engine/qkernels.cpp)
target_include_directories(albireo-kernels PRIVATE ${ALBIREO_INCLUDE_DIR})
target_compile_options(albireo-kernels PRIVATE ${ALBIREO_WARNINGS})
set_target_properties(albireo-kernels PROPERTIES POSITION_INDEPENDENT_CODE ON
   C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# La bibliothèque du moteur, que le service charge depuis son dossier engines.
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
   # Bibliothèques compilées dans les programmes : SQLite et zlib (ci-dessus), venus de dlib/external.
   add_library(albireo_sqlite STATIC ${ALBIREO_DLIB_DIR}/external/sqlite/sqlite3.c)
   target_include_directories(albireo_sqlite PUBLIC ${ALBIREO_DLIB_DIR}/external/sqlite)
   target_compile_definitions(albireo_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_DQS=0 SQLITE_DEFAULT_MEMSTATUS=0
      SQLITE_DEFAULT_WAL_SYNCHRONOUS=1 SQLITE_LIKE_DOESNT_MATCH_BLOBS SQLITE_TEMP_STORE=2 SQLITE_SECURE_DELETE
      SQLITE_ENABLE_API_ARMOR SQLITE_ENABLE_FTS5 SQLITE_ENABLE_MATH_FUNCTIONS SQLITE_OMIT_LOAD_EXTENSION)
   target_link_libraries(albireo_sqlite PUBLIC ${CMAKE_DL_LIBS} m)

   # Code partagé par le service et les outils.
   add_library(albireo_common STATIC common/log.cpp common/platform.cpp common/system_info.cpp
      common/engine_loader.cpp)
   target_include_directories(albireo_common PUBLIC ${ALBIREO_INCLUDE_DIR})
   target_compile_options(albireo_common PRIVATE ${ALBIREO_WARNINGS})
   target_link_libraries(albireo_common PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

   # Le service.
   add_executable(albireo-service host/main.cpp host/service.cpp host/server.cpp host/settings.cpp host/store.cpp
      host/models.cpp host/engine_host.cpp host/http_fetch.cpp host/crypto.cpp
      host/certificates.cpp host/web_access.cpp host/hub.cpp)
   target_include_directories(albireo-service PRIVATE ${CMAKE_BINARY_DIR}/generated)
   target_link_libraries(albireo-service PRIVATE albireo_common albireo_sqlite albireo_zlib dlib::httplib)
   target_compile_options(albireo-service PRIVATE ${ALBIREO_WARNINGS})

   # La préparation des modèles fournis avec Albireo : vérifiés et convertis avec le code
   # du moteur, puis remis aux programmes de l'arbre.
   add_executable(albireo-builtin tools/albireo_builtin.cpp engine/chat_format.cpp engine/model_config.cpp
      engine/model_file.cpp $<TARGET_OBJECTS:albireo-kernels> host/crypto.cpp)
   target_link_libraries(albireo-builtin PRIVATE albireo_common dlib::httplib)
   target_compile_options(albireo-builtin PRIVATE ${ALBIREO_WARNINGS})

   # Les modèles fournis avec Albireo (resources/builtin_models.json) : téléchargés une fois dans
   # le cache (cmake/builtin_models.cmake), vérifiés et convertis par albireo-builtin, puis
   # remis au dossier « models » des programmes de l'arbre.
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

   # L'application web et la documentation de l'interface, à côté du service.
   add_custom_target(albireo-web ALL
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/bin/web
      COMMENT "Copying the web application")
   add_dependencies(albireo-service albireo-web)

endif()

if (ALBIREO_TESTS AND ALBIREO_PROGRAMS)
   # Les fichiers que le service écrit dans son dossier de modèles et lit dans ses imports, les
   # téléchargements, les réglages et l'accès web, avec
   # un serveur local.
   add_executable(test_host tests/test_host.cpp host/models.cpp host/store.cpp host/http_fetch.cpp
      host/crypto.cpp host/settings.cpp host/web_access.cpp)
   target_include_directories(test_host PRIVATE ${CMAKE_BINARY_DIR}/generated)
   target_link_libraries(test_host PRIVATE albireo_common albireo_sqlite albireo_zlib dlib::httplib)
   target_compile_options(test_host PRIVATE ${ALBIREO_WARNINGS})
   albireo_output_dir(test_host ${CMAKE_BINARY_DIR}/tests)
   # test_model_file : fichiers de modèle abîmés ; test_engine_api : l'interface C d'une bibliothèque de moteur.
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
   # Les noyaux Metal comparés au processeur, sur des données aléatoires (portage macOS).
   add_executable(test_metal tests/test_metal.cpp engine/metal_device.mm $<TARGET_OBJECTS:albireo-kernels>
      common/system_info.cpp)
   target_include_directories(test_metal PRIVATE ${ALBIREO_INCLUDE_DIR})
   target_compile_options(test_metal PRIVATE ${ALBIREO_WARNINGS})
   target_link_libraries(test_metal PRIVATE Threads::Threads)
   albireo_output_dir(test_metal ${CMAKE_BINARY_DIR}/tests)
   # Le format de chat des agents : les prompts avec outils, et les appels d'outils relus.
   add_executable(test_chat_format tests/test_chat_format.cpp engine/chat_format.cpp)
   target_include_directories(test_chat_format PRIVATE ${ALBIREO_INCLUDE_DIR})
   albireo_output_dir(test_chat_format ${CMAKE_BINARY_DIR}/tests)
endif()
