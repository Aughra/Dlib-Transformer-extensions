// La température de la puce du Mac (fork pour macOS, 2026-11-22) : voir thermal.h et macos/temperature.
#include "thermal.h"

#import <Foundation/Foundation.h>
#include <IOKit/hidsystem/IOHIDEventSystemClient.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <deque>
#include <mutex>
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
        const size_t history_most = 24 * 3600 / thermal_period;
    }

    void start_thermal_history()
    {
        static std::once_flag once;
        std::call_once(once, [] {
            // Un fil détaché pour la vie du service : une lecture toutes les cinq minutes, la première au démarrage.
            std::thread([] {
                for (;;)
                {
                    const chip_thermal t = read_chip_thermal();
                    if (t.sensors > 0)
                    {
                        thermal_sample s;
                        s.time = static_cast<long long>(std::time(nullptr));
                        s.max_c = t.max_c;
                        s.mean_c = t.mean_c;
                        s.state = t.state;
                        std::lock_guard<std::mutex> lock(history_m);
                        history.push_back(s);
                        while (history.size() > history_most) history.pop_front();
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(thermal_period));
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
