// Copyright (C) 2006  Davis E. King (davis@dlib.net)
// License: Boost Software License   See LICENSE.txt for the full license.

#ifdef DLIB_ALL_SOURCE_END
#include "dlib_basic_cpp_build_tutorial.txt"
#endif

#ifndef DLIB_PLATFORm_
#define DLIB_PLATFORm_


/*!
    This file ensures that DLIB_POSIX and MACOSX are defined.  The macOS fork of dlib builds for
    macOS only (Apple Silicon): any other platform stops the build.
!*/

#if !defined(__APPLE__)
    #error "This fork of dlib builds for macOS only"
#endif

#ifndef DLIB_POSIX
    #define DLIB_POSIX
#endif

#ifndef MACOSX
    #define MACOSX
#endif




#endif // DLIB_PLATFORm_

