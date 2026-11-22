// La température de la puce du Mac (fork pour macOS, 2026-11-22), sans droits d'administrateur : l'état thermique de
// macOS (NSProcessInfo.thermalState), puis la plus chaude et la moyenne des sondes « PMU tdie » de la puce, lues par
// IOHIDEventSystemClient (interface interne d'Apple, celle qu'emploient des utilitaires comme Stats).  Les sondes
// absentes rendent environ -9 200 °C et sont écartées.
//
//   clang -fobjc-arc -framework Foundation -framework IOKit -o build/temperature macos/temperature/temperature.m
//   build/temperature
#import <Foundation/Foundation.h>
#include <IOKit/hidsystem/IOHIDEventSystemClient.h>
typedef struct __IOHIDEvent* IOHIDEventRef;
typedef struct __IOHIDServiceClient* IOHIDServiceClientRef;
extern IOHIDEventSystemClientRef IOHIDEventSystemClientCreate(CFAllocatorRef);
extern int IOHIDEventSystemClientSetMatching(IOHIDEventSystemClientRef, CFDictionaryRef);
extern CFArrayRef IOHIDEventSystemClientCopyServices(IOHIDEventSystemClientRef);
extern IOHIDEventRef IOHIDServiceClientCopyEvent(IOHIDServiceClientRef, int64_t, int32_t, int64_t);
extern CFStringRef IOHIDServiceClientCopyProperty(IOHIDServiceClientRef, CFStringRef);
extern double IOHIDEventGetFloatValue(IOHIDEventRef, int32_t);
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
  }
}
