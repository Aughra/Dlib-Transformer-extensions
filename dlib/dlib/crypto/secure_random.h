// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef DLIB_SECURE_RANDOM_H_
#define DLIB_SECURE_RANDOM_H_

#include "secure_random_abstract.h"

#include <cstddef>
#include <string>
#include "crypto_utils.h"

// macOS fork: the bytes of arc4random_buf, which the system draws from its own generator.
#include <stdlib.h>

namespace dlib
{

// ----------------------------------------------------------------------------------------

    inline void generate_secure_random_bytes(
        void* buffer,
        std::size_t size
    )
    {
        if (size == 0)
            return;
        arc4random_buf(buffer, size);
    }

    inline std::string generate_secure_random_bytes(
        std::size_t size
    )
    {
        std::string out(size, '\0');
        if (size > 0)
            generate_secure_random_bytes(&out[0], size);
        return out;
    }

// ----------------------------------------------------------------------------------------

}

#endif // DLIB_SECURE_RANDOM_H_
