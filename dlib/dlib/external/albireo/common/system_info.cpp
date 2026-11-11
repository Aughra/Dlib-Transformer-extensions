// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "system_info.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include <sys/utsname.h>
#include <unistd.h>
#include <mach/mach.h>
#include <sys/sysctl.h>

namespace albireo
{

    namespace
    {

        std::string trim(const std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\0')) ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\0')) --b;
            return s.substr(a, b - a);
        }
    }

    cpu_info read_cpu_info()
    {
        cpu_info c;
        c.logical_cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
        c.physical_cores = c.logical_cores;


        // The name of the processor, "Apple M2" for instance.
        char brand[128];
        size_t brand_size = sizeof(brand);
        if (sysctlbyname("machdep.cpu.brand_string", brand, &brand_size, nullptr, 0) == 0)
            c.brand = trim(std::string(brand, strnlen(brand, brand_size)));

        // Distinct (package, core) pairs of the online processors.
        std::set<std::pair<int, int>> cores;
        for (int cpu = 0; cpu < c.logical_cores; ++cpu)
        {
            const std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/";
            std::ifstream pkg(base + "physical_package_id"), core(base + "core_id");
            int p = -1, k = -1;
            if (pkg >> p && core >> k) cores.insert(std::make_pair(p, k));
        }
        if (!cores.empty()) c.physical_cores = static_cast<int>(cores.size());
        if (c.physical_cores < 1) c.physical_cores = 1;
        return c;
    }

    memory_info read_memory_info()
    {
        memory_info m;
        // macOS has no /proc: the memory installed, and the pages free, inactive or purgeable.
        uint64_t total = 0;
        size_t size = sizeof(total);
        if (sysctlbyname("hw.memsize", &total, &size, nullptr, 0) == 0) m.total = total;
        vm_statistics64_data_t vm;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count)
            == KERN_SUCCESS)
            m.available = (static_cast<uint64_t>(vm.free_count) + vm.inactive_count + vm.purgeable_count)
                * static_cast<uint64_t>(vm_kernel_page_size);
        // Never below the physical memory, which a system without paging file still commits.
        m.commit_total = std::max(m.commit_total, m.total);
        m.commit_available = std::max(m.commit_available, m.available);
        return m;
    }

    std::string operating_system_name()
    {
        struct utsname u;
        if (uname(&u) == 0) return std::string(u.sysname) + " " + u.release + " " + u.machine;
        return "Unix";
    }

    std::string host_name()
    {
        char buf[256];
        if (gethostname(buf, sizeof(buf)) == 0)
        {
            buf[255] = 0;
            return buf;
        }
        return "localhost";
    }

}
