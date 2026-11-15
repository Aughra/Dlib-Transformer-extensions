
cmake_minimum_required(VERSION 3.10.0)






set(gcc_like_compilers GNU Clang  Intel)
set(intel_archs x86_64 i386 i686 AMD64 amd64 x86)


# Setup some options to allow a user to enable SSE and AVX instruction use.  
if ((";${gcc_like_compilers};" MATCHES ";${CMAKE_CXX_COMPILER_ID};")  AND
   (";${intel_archs};"        MATCHES ";${CMAKE_SYSTEM_PROCESSOR};") AND NOT USE_AUTO_VECTOR)
   option(USE_SSE2_INSTRUCTIONS "Compile your program with SSE2 instructions" OFF)
   option(USE_SSE4_INSTRUCTIONS "Compile your program with SSE4 instructions" OFF)
   option(USE_AVX_INSTRUCTIONS  "Compile your program with AVX instructions"  OFF)
   if(USE_AVX_INSTRUCTIONS)
      list(APPEND active_compile_opts -mavx)
      message(STATUS "Enabling AVX instructions")
   elseif (USE_SSE4_INSTRUCTIONS)
      list(APPEND active_compile_opts -msse4)
      message(STATUS "Enabling SSE4 instructions")
   elseif(USE_SSE2_INSTRUCTIONS)
      list(APPEND active_compile_opts -msse2)
      message(STATUS "Enabling SSE2 instructions")
   endif()
elseif((";${gcc_like_compilers};" MATCHES ";${CMAKE_CXX_COMPILER_ID};")  AND
        ("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "^arm"))
   option(USE_NEON_INSTRUCTIONS "Compile your program with ARM-NEON instructions" OFF)
   if(USE_NEON_INSTRUCTIONS)
      list(APPEND active_compile_opts -mfpu=neon)
      message(STATUS "Enabling ARM-NEON instructions")
   endif()
endif()




if (CMAKE_COMPILER_IS_GNUCXX)
   # By default, g++ won't warn or error if you forget to return a value in a
   # function which requires you to do so.  This option makes it give a warning
   # for doing this.
   list(APPEND active_compile_opts "-Wreturn-type")
endif()

if ("Clang" MATCHES ${CMAKE_CXX_COMPILER_ID} AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0.0)
   # Clang 6 had a default template recursion depth of 256. This was changed to 1024 in Clang 7.
   # It must be increased on Clang 6 and below to ensure that the dnn examples don't error out.
   list(APPEND active_compile_opts "-ftemplate-depth=500")
endif()



