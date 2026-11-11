// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_COMMON_SYSTEM_INFO_H_
#define ALBIREO_COMMON_SYSTEM_INFO_H_

#include <cstdint>
#include <string>
#include <vector>

namespace albireo
{

    struct cpu_info
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                The processor of the Mac as the engine needs to know it: its name ("Apple M2"),
                its cores, those of performance and those of efficiency, and the extensions of
                ARM that macOS declares (hw.optional.arm.FEAT_*), such as the dot products of
                bytes that the kernels use.
        !*/
        std::string brand;
        int physical_cores = 1;
        int logical_cores = 1;
        int performance_cores = 0;   // hw.perflevel0, 0 when the system does not tell
        int efficiency_cores = 0;    // hw.perflevel1
        std::vector<std::string> features;   // "DotProd", "I8MM", "BF16", "FP16"
    };

    cpu_info read_cpu_info();

    struct memory_info
    {
        uint64_t total = 0;       // physical memory, bytes
        uint64_t available = 0;   // physical memory available to a new allocation, bytes
        /*
            The memory the system can commit: on macOS, the physical memory (the swap of the
            system, which grows on demand, is not counted).
        */
        uint64_t commit_total = 0;
        uint64_t commit_available = 0;
    };

    memory_info read_memory_info();

    // "Darwin 27.0.0 arm64", the name, the version and the machine of the system.
    std::string operating_system_name();

    // Name of this computer.
    std::string host_name();

}

#endif // ALBIREO_COMMON_SYSTEM_INFO_H_
