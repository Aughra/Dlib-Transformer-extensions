# Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Gulliver : le service d'inférence local construit sur dlib, pour macOS sur Apple Silicon seulement.
#
#   cmake -S dlib/dlib/external/gulliver -B build/macos-gulliver -G Ninja -DCMAKE_BUILD_TYPE=Release
#
# Chaque programme arrive dans <build>/bin et le moteur dans <build>/bin/engines ; l'application web
# est copiée dans <build>/bin/web.

cmake_minimum_required(VERSION 3.16)
project(gulliver VERSION 7.14.0 LANGUAGES C CXX OBJCXX)

# La version est écrite à quatre endroits qui doivent concorder : project() ci-dessus, les numéros et
# le texte de common/version.h, que donnent le service et le moteur, et la description de
# l'interface.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/common/version.h" version_lines REGEX "^#define GULLIVER_VERSION")
set(version_numbers)
foreach (part MAJOR MINOR PATCH)
   string(REGEX MATCH "#define GULLIVER_VERSION_${part} +([0-9]+)" found "${version_lines}")
   list(APPEND version_numbers "${CMAKE_MATCH_1}")
endforeach()
string(REPLACE ";" "." version_numbers "${version_numbers}")
string(REGEX MATCH "#define GULLIVER_VERSION +\"([0-9.]+)\"" found "${version_lines}")
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

option(GULLIVER_PROGRAMS "Build the service, the engine and the tools" ON)
option(GULLIVER_TESTS "Build the test programs" ON)

if (NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|ARM64)$")
   message(FATAL_ERROR "Gulliver builds for macOS on Apple Silicon only (arm64).")
endif()

# C++23 (décision de l'humain, 2026-11-28 : « transforme en C++ 23 ») ; l'Objective-C++ suit le même standard.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
   set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Gulliver ne lit plus rien de dlib (fil rouge, morceau 3, 2026-11-20) : le code qu'il en a repris est
# dans common/ (lecture des GGUF), engine/ (tokenizer) et host/ (SQLite, ZIP), et la licence de dlib,
# sous laquelle ce code reste, dans resources/dlib-license.txt.

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(GULLIVER_ENGINE_DIR ${CMAKE_BINARY_DIR}/bin/engines)

# Place les exécutables d'une cible dans un dossier de l'arbre de build.
function(gulliver_output_dir target dir)
   set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${dir})
endfunction()

# Les bibliothèques d'autres projets vivent dans third_party/, copiées telles quelles de
# dlib/external (fil rouge, morceau 2) : le build de Gulliver ne construit plus dlib.
set(GULLIVER_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
find_package(Threads REQUIRED)

# cpp-httplib (un seul en-tête), en HTTP seulement : le TLS du serveur et des connexions sortantes est fait
# par Network.framework (host/tls_gate.mm, host/tls_client.mm).
add_library(gulliver_httplib INTERFACE)
target_include_directories(gulliver_httplib INTERFACE ${GULLIVER_THIRD_PARTY}/httplib)
target_link_libraries(gulliver_httplib INTERFACE Threads::Threads
   "-framework CoreFoundation" "-framework Security")

set(GULLIVER_WARNINGS -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
# Les produits des longues passes passent par Accelerate (engine/qkernels_accelerate.inc) ; le
# GPU par Metal (engine/metal_device.mm).
link_libraries("-framework Accelerate" "-framework Metal" "-framework MetalPerformanceShaders" "-framework Foundation"
   "-framework ImageIO" "-framework CoreGraphics" "-framework CoreFoundation" "-framework Security" "-framework Network")

# La source des noyaux Metal, intégrée au moteur sous forme d'une chaîne que macOS compile quand
# le moteur ouvre le GPU : le build n'a besoin d'aucun compilateur Metal.
# Les formats des poids (metal_formats.metal) en tête des deux sources Metal, qui les partagent.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/engine/metal_formats.metal" GULLIVER_METAL_FORMATS)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/engine/metal_kernels.metal" GULLIVER_METAL_MAIN)
set(GULLIVER_METAL_SOURCE "${GULLIVER_METAL_FORMATS}${GULLIVER_METAL_MAIN}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/engine/metal_formats.metal")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/engine/metal_kernels.metal")
configure_file(engine/metal_kernels.inc.in ${CMAKE_BINARY_DIR}/generated/metal_kernels.inc @ONLY)
# Les noyaux des accélérateurs neuronaux du M5 (Metal 4), compilés à part (metal_device::open()).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/engine/metal_kernels_nax.metal" GULLIVER_METAL_NAX_MAIN)
set(GULLIVER_METAL_NAX_SOURCE "${GULLIVER_METAL_FORMATS}${GULLIVER_METAL_NAX_MAIN}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/engine/metal_kernels_nax.metal")
configure_file(engine/metal_kernels_nax.inc.in ${CMAKE_BINARY_DIR}/generated/metal_kernels_nax.inc @ONLY)
set_source_files_properties(engine/metal_device.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
   INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/generated")

# zlib, celle de macOS (SDK), pour les programmes et les décodeurs d'images du moteur.
find_package(ZLIB REQUIRED)

# Les images des requêtes sont décodées par ImageIO (engine/image_input.cpp) : plus de copies de
# libpng ni de libjpeg (fork pour macOS, 2026-11-20).

# Le moteur d'inférence.
set(GULLIVER_ENGINE_SOURCES
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
add_library(gulliver-kernels OBJECT engine/qkernels.cpp)
target_compile_options(gulliver-kernels PRIVATE ${GULLIVER_WARNINGS})
set_target_properties(gulliver-kernels PROPERTIES POSITION_INDEPENDENT_CODE ON
   C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# La bibliothèque du moteur, que le service charge depuis son dossier engines.
function(gulliver_engine variant)
   set(target gulliver-engine-${variant})
   add_library(${target} SHARED ${GULLIVER_ENGINE_SOURCES} $<TARGET_OBJECTS:gulliver-kernels> ${ARGN})
   target_compile_definitions(${target} PRIVATE GULLIVER_ENGINE_BUILD GULLIVER_ENGINE_VARIANT="${variant}")
   target_compile_options(${target} PRIVATE ${GULLIVER_WARNINGS})
   target_link_libraries(${target} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
   set_target_properties(${target} PROPERTIES
      C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
      LIBRARY_OUTPUT_DIRECTORY ${GULLIVER_ENGINE_DIR})
endfunction()

if (GULLIVER_PROGRAMS)
   gulliver_engine(cpu)
endif()

if (GULLIVER_PROGRAMS)
   # SQLite et zlib sont ceux de macOS (fork, 2026-11-20 : plus de copie de SQLite).  L'enveloppe de
   # Gulliver (host/sqlite_db.h) ouvre la base en mode « serialized » et pose les réglages que la copie
   # avait à la compilation (host/store.cpp).
   add_library(gulliver_sqlite INTERFACE)
   target_link_libraries(gulliver_sqlite INTERFACE sqlite3)

   # Code partagé par le service et les outils.
   add_library(gulliver_common STATIC common/log.cpp common/platform.cpp common/system_info.cpp
      common/engine_loader.cpp)
   target_compile_options(gulliver_common PRIVATE ${GULLIVER_WARNINGS})
   target_link_libraries(gulliver_common PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

   # Le service.
   add_executable(gulliver-service host/main.cpp host/service.cpp host/server.cpp host/settings.cpp host/store.cpp
      host/models.cpp host/engine_host.cpp host/http_fetch.cpp host/crypto.cpp host/accounts.cpp host/mailer.cpp
      host/certificates.cpp host/web_access.cpp host/hub.cpp host/carnet.cpp host/relay.cpp host/pseudonyms.cpp
      host/rag_engine.cpp host/doc_extract.cpp host/doc_extract_pdf.mm host/pdf_render.cpp host/highlight.cpp host/markdown.cpp host/content_guard.cpp host/tls_gate.mm
      host/tls_client.mm host/thermal.mm)
   set_source_files_properties(host/tls_gate.mm host/tls_client.mm host/thermal.mm host/doc_extract_pdf.mm
      PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
   target_include_directories(gulliver-service PRIVATE ${CMAKE_BINARY_DIR}/generated)
   # Le carnet du portage lit les documents du dépôt où le service est construit (host/carnet.h).
   get_filename_component(GULLIVER_FORK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
   target_compile_definitions(gulliver-service PRIVATE GULLIVER_FORK_ROOT_DEFAULT="${GULLIVER_FORK_ROOT}")
   # L'extraction du texte des PDF passe par PDFKit (host/doc_extract_pdf.mm) et le dessin des
   # pages par CoreGraphics (host/pdf_render.cpp) : deux cadriciels natifs de macOS, rien à
   # embarquer (PDFium abandonnée en 7.3.0).
   target_link_libraries(gulliver-service PRIVATE gulliver_common gulliver_sqlite ZLIB::ZLIB gulliver_httplib
      "-framework CoreFoundation" "-framework IOKit" "-framework PDFKit" "-framework Quartz")
   target_compile_options(gulliver-service PRIVATE ${GULLIVER_WARNINGS})

   # La préparation des modèles fournis avec Gulliver : vérifiés et convertis avec le code
   # du moteur, puis remis aux programmes de l'arbre.
   add_executable(gulliver-builtin tools/gulliver_builtin.cpp engine/chat_format.cpp engine/model_config.cpp
      engine/model_file.cpp $<TARGET_OBJECTS:gulliver-kernels> host/crypto.cpp)
   target_link_libraries(gulliver-builtin PRIVATE gulliver_common gulliver_httplib)
   target_compile_options(gulliver-builtin PRIVATE ${GULLIVER_WARNINGS})

   # Les modèles fournis avec Gulliver (resources/builtin_models.json) : téléchargés une fois dans
   # le cache (cmake/builtin_models.cmake), vérifiés et convertis par gulliver-builtin, puis
   # remis au dossier « models » des programmes de l'arbre.
   option(GULLIVER_BUILTIN_MODELS "Prepare the models that come with Gulliver" OFF)
   set(cache_default "$ENV{HOME}/Library/Caches/gulliver")
   set(GULLIVER_MODEL_CACHE "${cache_default}" CACHE PATH
      "Folder of the downloaded and converted models that come with Gulliver")
   set(GULLIVER_BUILTIN_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/resources/builtin_models.json" CACHE FILEPATH
      "Manifest of the models that come with Gulliver")
   if (GULLIVER_BUILTIN_MODELS)
      if (CMAKE_VERSION VERSION_LESS 3.19)
         message(FATAL_ERROR "the models that come with Gulliver need CMake 3.19 or later "
            "(-DGULLIVER_BUILTIN_MODELS=OFF builds without them)")
      endif()
      set(GULLIVER_BUILTIN_COMMAND $<TARGET_FILE:gulliver-builtin>)
      set(builtin_tool_target gulliver-builtin)
      set(GULLIVER_BUILTIN_STAMP ${CMAKE_BINARY_DIR}/bin/models/builtin.json)
      add_custom_command(OUTPUT ${GULLIVER_BUILTIN_STAMP}
         COMMAND ${CMAKE_COMMAND} -DMANIFEST=${GULLIVER_BUILTIN_MANIFEST} -DCACHE=${GULLIVER_MODEL_CACHE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/builtin_models.cmake
         COMMAND ${GULLIVER_BUILTIN_COMMAND} --manifest ${GULLIVER_BUILTIN_MANIFEST} --cache ${GULLIVER_MODEL_CACHE}
            --models-out ${CMAKE_BINARY_DIR}/bin/models
         DEPENDS ${GULLIVER_BUILTIN_MANIFEST} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/builtin_models.cmake
            ${builtin_tool_target}
         COMMENT "Preparing the models that come with Gulliver"
         VERBATIM)
      add_custom_target(gulliver-builtin-models ALL DEPENDS ${GULLIVER_BUILTIN_STAMP})
   endif()

   # L'application web et la documentation de l'interface, à côté du service.
   add_custom_target(gulliver-web ALL
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/bin/web
      COMMENT "Copying the web application")
   add_dependencies(gulliver-service gulliver-web)

   # Les textes des licences des composants du service, que montre la page « Composants et licences »
   # (fork pour macOS : l'installateur Windows de l'original les assemblait, cmake/payload.cmake).
   set(gulliver_licenses
      gulliver.txt     ${CMAKE_CURRENT_SOURCE_DIR}/resources/license.txt
      dlib.txt        ${CMAKE_CURRENT_SOURCE_DIR}/resources/dlib-license.txt
      cpp-httplib.txt ${GULLIVER_THIRD_PARTY}/httplib/LICENSE
      sqlite.txt      ${CMAKE_CURRENT_SOURCE_DIR}/resources/sqlite.txt)
   set(gulliver_license_commands)
   set(gulliver_license_sources)
   list(LENGTH gulliver_licenses gulliver_license_count)
   math(EXPR gulliver_license_last "${gulliver_license_count} - 1")
   foreach(i RANGE 0 ${gulliver_license_last} 2)
      math(EXPR j "${i} + 1")
      list(GET gulliver_licenses ${i} name)
      list(GET gulliver_licenses ${j} source)
      list(APPEND gulliver_license_sources ${source})
      list(APPEND gulliver_license_commands
         COMMAND ${CMAKE_COMMAND} -E copy_if_different ${source} ${CMAKE_BINARY_DIR}/bin/licenses/${name})
   endforeach()
   add_custom_target(gulliver-licenses ALL
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/licenses
      ${gulliver_license_commands}
      DEPENDS ${gulliver_license_sources}
      COMMENT "Copying the license texts")
   add_dependencies(gulliver-service gulliver-licenses)

endif()

if (GULLIVER_TESTS AND GULLIVER_PROGRAMS)
   # Les fichiers que le service écrit dans son dossier de modèles et lit dans ses imports, les
   # téléchargements, les réglages, l'accès web et le relais, avec des serveurs locaux.
   add_executable(test_host tests/test_host.cpp host/models.cpp host/store.cpp host/http_fetch.cpp
      host/crypto.cpp host/settings.cpp host/web_access.cpp host/certificates.cpp host/relay.cpp host/rag_engine.cpp
      host/pseudonyms.cpp host/pdf_render.cpp host/highlight.cpp host/markdown.cpp host/content_guard.cpp host/accounts.cpp host/tls_client.mm host/tls_gate.mm)
   target_include_directories(test_host PRIVATE ${CMAKE_BINARY_DIR}/generated)
   target_link_libraries(test_host PRIVATE gulliver_common gulliver_sqlite ZLIB::ZLIB gulliver_httplib)
   target_compile_options(test_host PRIVATE ${GULLIVER_WARNINGS})
   gulliver_output_dir(test_host ${CMAKE_BINARY_DIR}/tests)
   # test_model_file : fichiers de modèle abîmés ; test_engine_api : l'interface C d'une bibliothèque de moteur.
   # perplexity_runtime : la justesse comparée à llama-perplexity (fork pour macOS).
   foreach (t test_qkernels test_runtime test_tokenizer engine_chat bench_runtime perplexity_runtime test_model_file
         test_engine_api)
      if (t STREQUAL "engine_chat" OR t STREQUAL "test_engine_api")
         add_executable(${t} tests/${t}.cpp)
         target_link_libraries(${t} PRIVATE gulliver_common)
      else()
         add_executable(${t} tests/${t}.cpp engine/cpu_runtime.cpp engine/cpu_runtime_metal.cpp engine/metal_device.mm
            engine/model_config.cpp engine/model_file.cpp $<TARGET_OBJECTS:gulliver-kernels> common/system_info.cpp)
         target_link_libraries(${t} PRIVATE Threads::Threads)
      endif()
      gulliver_output_dir(${t} ${CMAKE_BINARY_DIR}/tests)
   endforeach()
   # Les noyaux Metal comparés au processeur, sur des données aléatoires (portage macOS).
   add_executable(test_metal tests/test_metal.cpp engine/metal_device.mm $<TARGET_OBJECTS:gulliver-kernels>
      common/system_info.cpp)
   target_compile_options(test_metal PRIVATE ${GULLIVER_WARNINGS})
   target_link_libraries(test_metal PRIVATE Threads::Threads)
   gulliver_output_dir(test_metal ${CMAKE_BINARY_DIR}/tests)
   # L'empreinte SHA-256 du moteur (CommonCrypto) et celle des images (fork pour macOS).
   add_executable(test_image_input tests/test_image_input.cpp engine/image_input.cpp)
   target_compile_options(test_image_input PRIVATE ${GULLIVER_WARNINGS})
   gulliver_output_dir(test_image_input ${CMAKE_BINARY_DIR}/tests)
   # Le format de chat des agents : les prompts avec outils, et les appels d'outils relus.
   add_executable(test_chat_format tests/test_chat_format.cpp engine/chat_format.cpp)
   gulliver_output_dir(test_chat_format ${CMAKE_BINARY_DIR}/tests)
endif()
