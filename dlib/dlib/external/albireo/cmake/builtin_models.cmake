# Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Télécharge les fichiers des modèles fournis avec Albireo dans le cache du build, chacun
# nommé d'après son SHA-256 et vérifié avec lui (cmake -P, CMake 3.19 ou plus récent) :
#
#   cmake -DMANIFEST=<resources/builtin_models.json> -DCACHE=<folder> -P builtin_models.cmake
#
# Un modèle indique le fichier dont il est converti (« from ») et, pour un modèle qui lit des
# images, son projecteur (« projector »), qu'Albireo livre tel qu'il est publié.  Un fichier déjà
# dans le cache est gardé quand son SHA-256 correspond, et téléchargé à nouveau sinon ;
# albireo-builtin revérifie l'empreinte avant de l'utiliser.  Les téléchargements passent par le
# proxy de la variable https_proxy, quand elle est définie, et s'arrêtent après deux minutes sans
# données.

if (CMAKE_VERSION VERSION_LESS 3.19)
   message(FATAL_ERROR "the models that come with Albireo need CMake 3.19 or later")
endif()

# Télécharge url dans le cache sous <sha>.gguf, sauf si un fichier de cette taille et de cette
# empreinte y est déjà.
function(albireo_cache_file label url sha size)
   set(target "${CACHE}/${sha}.gguf")
   if (EXISTS "${target}")
      file(SIZE "${target}" have)
      if (have EQUAL size)
         file(SHA256 "${target}" have_sha)
         string(TOLOWER "${sha}" want_sha)
         if (have_sha STREQUAL want_sha)
            return()
         endif()
         message(STATUS "${target} does not match its SHA-256: downloaded again")
      endif()
      file(REMOVE "${target}")
   endif()
   message(STATUS "Downloading ${label}: ${url}")
   file(DOWNLOAD "${url}" "${target}.partial" EXPECTED_HASH SHA256=${sha} SHOW_PROGRESS TLS_VERIFY ON
      INACTIVITY_TIMEOUT 120 STATUS status)
   list(GET status 0 code)
   if (NOT code EQUAL 0)
      list(GET status 1 text)
      file(REMOVE "${target}.partial")
      message(FATAL_ERROR "${label} could not be downloaded from ${url}: ${text}")
   endif()
   file(RENAME "${target}.partial" "${target}")
endfunction()

file(READ "${MANIFEST}" manifest)
string(JSON count LENGTH "${manifest}" models)
file(MAKE_DIRECTORY "${CACHE}")
math(EXPR last "${count} - 1")
foreach (i RANGE ${last})
   string(JSON id GET "${manifest}" models ${i} id)
   string(JSON url GET "${manifest}" models ${i} from url)
   string(JSON sha GET "${manifest}" models ${i} from sha256)
   string(JSON size GET "${manifest}" models ${i} from size)
   albireo_cache_file("${id}" "${url}" "${sha}" "${size}")
   string(JSON projector ERROR_VARIABLE none GET "${manifest}" models ${i} projector)
   if (NOT none)
      string(JSON url GET "${projector}" url)
      string(JSON sha GET "${projector}" sha256)
      string(JSON size GET "${projector}" size)
      albireo_cache_file("the projector of ${id}" "${url}" "${sha}" "${size}")
   endif()
endforeach()
