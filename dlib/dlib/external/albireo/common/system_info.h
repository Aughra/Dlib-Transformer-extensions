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
                Le processeur du Mac tel que le moteur a besoin de le connaître : son nom
                ("Apple M2"), ses cœurs, ceux de performance et ceux d'efficacité, et les
                extensions ARM que macOS déclare (hw.optional.arm.FEAT_*), comme les produits
                scalaires d'octets qu'utilisent les noyaux.
        !*/
        std::string brand;
        int physical_cores = 1;
        int logical_cores = 1;
        int performance_cores = 0;   // hw.perflevel0, 0 quand le système ne le dit pas
        int efficiency_cores = 0;    // hw.perflevel1
        std::vector<std::string> features;   // "DotProd", "I8MM", "BF16", "FP16"
    };

    cpu_info read_cpu_info();

    struct memory_info
    {
        uint64_t total = 0;       // mémoire physique, en octets
        uint64_t available = 0;   // mémoire physique disponible pour une nouvelle
                                  // allocation, en octets
        /*
            La mémoire que le système peut engager : sous macOS, la mémoire physique (le
            swap du système, qui grandit à la demande, n'est pas compté).
        */
        uint64_t commit_total = 0;
        uint64_t commit_available = 0;
    };

    memory_info read_memory_info();

    // "Darwin 27.0.0 arm64", le nom, la version et la machine du système.
    std::string operating_system_name();

    // Nom de cet ordinateur.
    std::string host_name();

}

#endif // ALBIREO_COMMON_SYSTEM_INFO_H_
