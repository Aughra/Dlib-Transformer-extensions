// La température de la puce du Mac (fork pour macOS, 2026-11-22) : voir thermal.h et macos/temperature.
#include "thermal.h"

#import <Foundation/Foundation.h>
#include <IOKit/hidsystem/IOHIDEventSystemClient.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

// L'interface des événements HID que macOS ne déclare pas dans ses en-têtes publics.
typedef struct __IOHIDEvent* IOHIDEventRef;
typedef struct __IOHIDServiceClient* IOHIDServiceClientRef;
extern "C" IOHIDEventSystemClientRef IOHIDEventSystemClientCreate(CFAllocatorRef);
extern "C" int IOHIDEventSystemClientSetMatching(IOHIDEventSystemClientRef, CFDictionaryRef);
extern "C" CFArrayRef IOHIDEventSystemClientCopyServices(IOHIDEventSystemClientRef);
extern "C" IOHIDEventRef IOHIDServiceClientCopyEvent(IOHIDServiceClientRef, int64_t, int32_t, int64_t);
extern "C" CFTypeRef IOHIDServiceClientCopyProperty(IOHIDServiceClientRef, CFStringRef);
extern "C" double IOHIDEventGetFloatValue(IOHIDEventRef, int32_t);

namespace gulliver
{
    namespace
    {
        const int64_t temperature_event = 15;

        chip_thermal sample()
        {
            chip_thermal t;
            @autoreleasepool
            {
                static const char* const states[] = { "nominal", "fair", "serious", "critical" };
                const NSInteger s = [[NSProcessInfo processInfo] thermalState];
                if (s >= 0 && s < 4) t.state = states[s];
                // Un client gardé pour la vie du service : le créer coûte bien plus qu'une lecture.
                static IOHIDEventSystemClientRef client = [] {
                    IOHIDEventSystemClientRef c = IOHIDEventSystemClientCreate(kCFAllocatorDefault);
                    if (c)
                    {
                        NSDictionary* match = @{ @"PrimaryUsagePage": @0xff00, @"PrimaryUsage": @5 };
                        IOHIDEventSystemClientSetMatching(c, (__bridge CFDictionaryRef)match);
                    }
                    return c;
                }();
                if (!client) return t;
                NSArray* services = CFBridgingRelease(IOHIDEventSystemClientCopyServices(client));
                double sum = 0;
                for (id service in services)
                {
                    IOHIDServiceClientRef sc = (__bridge IOHIDServiceClientRef)service;
                    id product = CFBridgingRelease(IOHIDServiceClientCopyProperty(sc, CFSTR("Product")));
                    if (![product isKindOfClass:[NSString class]] || ![(NSString*)product hasPrefix:@"PMU tdie"]) continue;
                    IOHIDEventRef e = IOHIDServiceClientCopyEvent(sc, temperature_event, 0, 0);
                    if (!e) continue;
                    const double c = IOHIDEventGetFloatValue(e, static_cast<int32_t>(temperature_event << 16));
                    CFRelease(e);
                    // Les sondes absentes rendent environ -9 200 degrés.
                    if (c < -50 || c > 150) continue;
                    t.max_c = t.sensors == 0 ? c : std::max(t.max_c, c);
                    sum += c;
                    ++t.sensors;
                }
                if (t.sensors > 0) t.mean_c = sum / t.sensors;
            }
            return t;
        }
    }

    chip_thermal read_chip_thermal()
    {
        static std::mutex m;
        static chip_thermal last;
        static std::chrono::steady_clock::time_point when;
        std::lock_guard<std::mutex> lock(m);
        const auto now = std::chrono::steady_clock::now();
        if (when.time_since_epoch().count() == 0 || now - when >= std::chrono::seconds(2))
        {
            last = sample();
            when = now;
        }
        return last;
    }

    namespace
    {
        std::mutex history_m;
        std::deque<thermal_sample> history;
        const size_t history_most = static_cast<size_t>(thermal_keep_minutes) * 60 / thermal_period;
        std::string history_file;   // vide : la mémoire seule
        size_t file_lines = 0;

        // Une ligne du fichier : « secondes max moyenne état processeur gpu », séparés par des tabulations.
        std::string sample_line(const thermal_sample& s)
        {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%lld\t%.2f\t%.2f\t%s\t%.1f\t%.1f\n", s.time, s.max_c, s.mean_c,
                s.state.empty() ? "-" : s.state.c_str(), s.cpu, s.gpu);
            return buf;
        }

        // Les relevés du fichier encore dans la fenêtre gardée ; le fichier est réécrit avec eux seuls.
        void load_history()
        {
            std::ifstream in(history_file);
            const long long since = static_cast<long long>(std::time(nullptr)) - 60LL * thermal_keep_minutes;
            std::string line;
            while (std::getline(in, line))
            {
                std::istringstream f(line);
                thermal_sample s;
                if (!(f >> s.time >> s.max_c >> s.mean_c >> s.state >> s.cpu >> s.gpu) || s.time < since) continue;
                if (s.state == "-") s.state.clear();
                history.push_back(s);
            }
            while (history.size() > history_most) history.pop_front();
            const std::string tmp = history_file + ".tmp";
            {
                std::ofstream out(tmp, std::ios::trunc);
                for (const auto& s : history) out << sample_line(s);
            }
            std::rename(tmp.c_str(), history_file.c_str());
            file_lines = history.size();
        }

        // Ajoute un relevé au fichier ; quand il a deux fois plus de lignes que la fenêtre, il est réécrit.
        void save_sample(const thermal_sample& s)
        {
            if (history_file.empty()) return;
            if (++file_lines > 2 * history_most)
            {
                const std::string tmp = history_file + ".tmp";
                {
                    std::ofstream out(tmp, std::ios::trunc);
                    for (const auto& h : history) out << sample_line(h);
                }
                std::rename(tmp.c_str(), history_file.c_str());
                file_lines = history.size();
                return;
            }
            std::ofstream(history_file, std::ios::app) << sample_line(s);
        }
    }

    namespace
    {
        // Les tics de tous les cœurs : occupés et total.
        void cpu_ticks(unsigned long long& busy, unsigned long long& total)
        {
            natural_t n = 0;
            processor_info_array_t info = nullptr;
            mach_msg_type_number_t count = 0;
            busy = total = 0;
            if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &n, &info, &count) != KERN_SUCCESS) return;
            for (natural_t i = 0; i < n; ++i)
            {
                const integer_t* t = info + i * CPU_STATE_MAX;
                const unsigned long long used = static_cast<unsigned long long>(t[CPU_STATE_USER])
                    + static_cast<unsigned long long>(t[CPU_STATE_SYSTEM]) + static_cast<unsigned long long>(t[CPU_STATE_NICE]);
                busy += used;
                total += used + static_cast<unsigned long long>(t[CPU_STATE_IDLE]);
            }
            vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(info), count * sizeof(integer_t));
        }

        // L'utilisation du GPU en %, que macOS publie dans les statistiques de l'IOAccelerator ; -1 sans elle.
        int gpu_use()
        {
            io_iterator_t it;
            if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &it) != KERN_SUCCESS)
                return -1;
            int use = -1;
            io_object_t o;
            while ((o = IOIteratorNext(it)))
            {
                CFTypeRef stats = IORegistryEntryCreateCFProperty(o, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0);
                if (stats && CFGetTypeID(stats) == CFDictionaryGetTypeID())
                {
                    const auto v = static_cast<CFNumberRef>(CFDictionaryGetValue(static_cast<CFDictionaryRef>(stats),
                        CFSTR("Device Utilization %")));
                    int x = 0;
                    if (v && CFNumberGetValue(v, kCFNumberIntType, &x) && x > use) use = x;
                }
                if (stats) CFRelease(stats);
                IOObjectRelease(o);
            }
            IOObjectRelease(it);
            return use;
        }
    }

    void start_thermal_history(const std::string& file)
    {
        static std::once_flag once;
        std::call_once(once, [file] {
            if (!file.empty())
            {
                std::lock_guard<std::mutex> lock(history_m);
                history_file = file;
                load_history();
            }
            /*
                Un fil détaché pour la vie du service : toutes les dix secondes, l'utilisation du GPU ; toutes les
                thermal_period secondes (et dix secondes après le démarrage, pour un premier point), un relevé : la
                température du moment, la charge moyenne du processeur (ses compteurs de temps depuis le relevé
                précédent) et celle du GPU (la moyenne des lectures de la période).
            */
            std::thread([] {
                unsigned long long busy0 = 0, total0 = 0;
                cpu_ticks(busy0, total0);
                double gpu_sum = 0;
                int gpu_count = 0;
                auto last = std::chrono::steady_clock::now();
                bool first = true;
                for (;;)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(10));
                    const int g = gpu_use();
                    if (g >= 0)
                    {
                        gpu_sum += g;
                        ++gpu_count;
                    }
                    const auto now = std::chrono::steady_clock::now();
                    if (!first && now - last < std::chrono::seconds(thermal_period)) continue;
                    first = false;
                    last = now;
                    unsigned long long busy = 0, total = 0;
                    cpu_ticks(busy, total);
                    const chip_thermal t = read_chip_thermal();
                    thermal_sample s;
                    s.time = static_cast<long long>(std::time(nullptr));
                    s.max_c = t.max_c;
                    s.mean_c = t.mean_c;
                    s.state = t.state;
                    if (total > total0) s.cpu = 100.0 * static_cast<double>(busy - busy0) / static_cast<double>(total - total0);
                    if (gpu_count > 0) s.gpu = gpu_sum / gpu_count;
                    busy0 = busy;
                    total0 = total;
                    gpu_sum = 0;
                    gpu_count = 0;
                    if (t.sensors == 0 && s.cpu < 0 && s.gpu < 0) continue;
                    std::lock_guard<std::mutex> lock(history_m);
                    history.push_back(s);
                    while (history.size() > history_most) history.pop_front();
                    save_sample(s);
                }
            }).detach();
        });
    }

    std::vector<thermal_sample> thermal_history(int minutes)
    {
        const long long since = static_cast<long long>(std::time(nullptr)) - 60LL * minutes;
        std::lock_guard<std::mutex> lock(history_m);
        std::vector<thermal_sample> out;
        for (const auto& s : history)
            if (s.time >= since) out.push_back(s);
        return out;
    }
}
