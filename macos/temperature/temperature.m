// La température de la puce du Mac (fork pour macOS, 2026-11-22), sans droits d'administrateur : l'état thermique de
// macOS (NSProcessInfo.thermalState), puis la plus chaude et la moyenne des sondes « PMU tdie » de la puce, lues par
// IOHIDEventSystemClient (interface interne d'Apple, celle qu'emploient des utilitaires comme Stats).  Les sondes
// absentes rendent environ -9 200 °C et sont écartées.
//
// Puis la charge : le processeur (tous ses cœurs, en %, sur une seconde : host_processor_info relu deux fois) et le GPU
// (« Device Utilization % » des statistiques de l'IOAccelerator, que macOS publie).
//
//   clang -fobjc-arc -framework Foundation -framework IOKit -o build/temperature macos/temperature/temperature.m
//   build/temperature
#import <Foundation/Foundation.h>
#include <IOKit/hidsystem/IOHIDEventSystemClient.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <unistd.h>
typedef struct __IOHIDEvent* IOHIDEventRef;
typedef struct __IOHIDServiceClient* IOHIDServiceClientRef;
extern IOHIDEventSystemClientRef IOHIDEventSystemClientCreate(CFAllocatorRef);
extern int IOHIDEventSystemClientSetMatching(IOHIDEventSystemClientRef, CFDictionaryRef);
extern CFArrayRef IOHIDEventSystemClientCopyServices(IOHIDEventSystemClientRef);
extern IOHIDEventRef IOHIDServiceClientCopyEvent(IOHIDServiceClientRef, int64_t, int32_t, int64_t);
extern CFStringRef IOHIDServiceClientCopyProperty(IOHIDServiceClientRef, CFStringRef);
extern double IOHIDEventGetFloatValue(IOHIDEventRef, int32_t);
// Les tics de tous les cœurs : (occupés, total).
static void cpu_ticks(unsigned long long* busy, unsigned long long* total) {
  natural_t n = 0; processor_info_array_t info; mach_msg_type_number_t count = 0;
  *busy = *total = 0;
  if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &n, &info, &count) != KERN_SUCCESS) return;
  for (natural_t i = 0; i < n; ++i) {
    const integer_t* t = info + i * CPU_STATE_MAX;
    const unsigned long long u = t[CPU_STATE_USER], s = t[CPU_STATE_SYSTEM], nice = t[CPU_STATE_NICE], idle = t[CPU_STATE_IDLE];
    *busy += u + s + nice;
    *total += u + s + nice + idle;
  }
  vm_deallocate(mach_task_self(), (vm_address_t)info, count * sizeof(integer_t));
}
// L'utilisation du GPU, en %, ou -1.
static int gpu_use(void) {
  io_iterator_t it;
  if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &it) != KERN_SUCCESS) return -1;
  int use = -1;
  io_object_t o;
  while ((o = IOIteratorNext(it))) {
    CFTypeRef stats = IORegistryEntryCreateCFProperty(o, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0);
    if (stats && CFGetTypeID(stats) == CFDictionaryGetTypeID()) {
      CFNumberRef v = CFDictionaryGetValue(stats, CFSTR("Device Utilization %"));
      int x;
      if (v && CFNumberGetValue(v, kCFNumberIntType, &x) && x > use) use = x;
    }
    if (stats) CFRelease(stats);
    IOObjectRelease(o);
  }
  IOObjectRelease(it);
  return use;
}
int main() {
  @autoreleasepool {
    const char* st[] = {"normal","assez chaud","chaud","critique"};
    printf("état thermique de macOS : %s\n", st[[[NSProcessInfo processInfo] thermalState]]);
    IOHIDEventSystemClientRef c = IOHIDEventSystemClientCreate(kCFAllocatorDefault);
    NSDictionary* m = @{@"PrimaryUsagePage": @0xff00, @"PrimaryUsage": @5};
    IOHIDEventSystemClientSetMatching(c, (__bridge CFDictionaryRef)m);
    NSArray* sv = CFBridgingRelease(IOHIDEventSystemClientCopyServices(c));
    double mx = -1000, sum = 0; int n = 0;
    for (id s in sv) {
      IOHIDServiceClientRef sc = (__bridge IOHIDServiceClientRef)s;
      NSString* name = CFBridgingRelease(IOHIDServiceClientCopyProperty(sc, CFSTR("Product")));
      IOHIDEventRef e = IOHIDServiceClientCopyEvent(sc, 15, 0, 0);
      if (!e) continue;
      double t = IOHIDEventGetFloatValue(e, 15 << 16);
      CFRelease(e);
      if ([name hasPrefix:@"PMU tdie"] && t > -50 && t < 150) { mx = t > mx ? t : mx; sum += t; ++n; }
    }
    printf("puce : %.1f °C au plus, %.1f °C en moyenne (%d sondes)\n", mx, n ? sum / n : 0, n);
    unsigned long long b0, t0, b1, t1;
    cpu_ticks(&b0, &t0);
    usleep(1000000);
    cpu_ticks(&b1, &t1);
    const double cpu = t1 > t0 ? 100.0 * (double)(b1 - b0) / (double)(t1 - t0) : -1;
    printf("charge : processeur %.0f %%, GPU %d %%\n", cpu, gpu_use());
  }
}
