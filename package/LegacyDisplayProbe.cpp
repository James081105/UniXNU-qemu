// SPDX-License-Identifier: BSD-2-Clause-Patent
// Bounded offline original lifecycle instrument; no desktop claim.
// LegacyDisplayProbe.cpp of UniXNU-sheng; the late probe starts sheng's key store and HID probes and this board's
// VirtIO input, but no longer sheng's CPU frequency, GPU power and touch panel probes, which drive sheng hardware and
// are not part of this board's archive.
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOUserClient.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSSerialize.h>
#include <libkern/c++/OSData.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <sys/proc.h>
#include <pexpert/pexpert.h>
#include <mach/kmod.h>
#include <machine/machine_routines.h>
extern "C" kmod_info_t g_kernel_kmod_info;
extern "C" void native_legacy_display_static_init(void) __asm__("__GLOBAL__sub_I_NativeLegacyDisplay.cpp");
extern "C" IOService *native_legacy_display_allocate(void);
extern "C" void native_gop_presenter_static_init(void) __asm__("__GLOBAL__sub_I_NativeGOPPresenter.cpp");
extern "C" IOService *native_gop_presenter_create(void);
static IOService *pendingLegacyDisplay;
static IOService *activeGOPPresenter,*activeGOPNub;
// GPU hand-off presenter (sheng-fb-gpu=1, phase 4): lives beside the display driver, which offers it frames instead of copying them.
static IOService *gpuHandoffPresenter;
extern "C" bool native_gop_gpu_offer(IOService *,void *display,IOMemoryDescriptor *,uint64_t stride,uint32_t width,uint32_t height);
extern "C" void native_gop_presenter_set_source(IOService *presenter,IOService *nub);
volatile uint32_t native_legacy_gpu_fallbacks,native_legacy_gpu_last_status;
static IOService *activeLegacyDisplay;
// Keep one diagnostic mapping alive until reboot, even after backend stop.
// The pulse is permanently closed before any system-frame copy.
static IOMemoryMap *diagnosticAnchorMapping;
extern "C" int boot_progress_pulse_anchor(uintptr_t,uint64_t,unsigned,unsigned,unsigned) __attribute__((weak_import));
static IORegistryEntry *unitScaleChosen;
static bool unitScaleAdded,unitRotationAdded;
static void finishUnitScalePolicy(bool keep) {
    if(unitScaleChosen && !keep) {
        if(unitScaleAdded)unitScaleChosen->removeProperty("display-scale");
        if(unitRotationAdded)unitScaleChosen->removeProperty("display-rotation");
    }
    OSSafeReleaseNULL(unitScaleChosen);unitScaleAdded=false;unitRotationAdded=false;
}
static bool publishOrMatchWord(IORegistryEntry *entry,const char *key,uint32_t value,bool &added) {
    OSObject *previous=entry->getProperty(key);
    if(previous) {
        OSData *data=OSDynamicCast(OSData,previous);uint32_t word=0;
        if(!data || data->getLength()!=sizeof(word))return false;
        memcpy(&word,data->getBytesNoCopy(),sizeof(word));return word==value;
    }
    OSData *data=OSData::withBytes(&value,sizeof(value));
    bool result=data && entry->setProperty(key,data);OSSafeReleaseNULL(data);added=result;return result;
}
//: The agent channel, declared STRONG, with C linkage, at file scope.
//: A weak_import reference does not oblige the linker to keep the
//: definition, and MEASURED on build 41330622788 one resolved to null and
//: dropped records behind a green build.  A strong reference makes a
//: missing archive fail the link naming the symbol instead.  Same rule
//: HidInputProbe.cpp:51 records for its own emitter.
//:
//: extern "C" is load-bearing and not decoration: the definition is a C
//: symbol in the XNU console patch, and this translation unit's functions
//: all sit OUTSIDE the extern "C" block that ends at line 129, so a bare
//: `extern int` would mangle the reference into C++ and the kext would
//: carry an undefined symbol that resolves to nothing at load.  That is
//: the same failure the weak_import produced -- a green build that emits
//: nothing -- reached by a different route.  HidInputProbe.cpp gets C
//: linkage by declaring inside its extern "C" block; this file cannot,
//: because its emitter is not in one.
extern "C" int native_console_report(const void *, size_t);

extern "C" {
volatile uint32_t native_legacy_stage;
volatile int native_legacy_meta_status=-1;
volatile uint32_t native_legacy_init_result,native_legacy_start_result,native_legacy_probe_match;
volatile uint32_t native_legacy_parent_match,native_legacy_object_size,native_legacy_released;
volatile uint64_t native_legacy_object,native_legacy_nub,native_legacy_workloop;
volatile uint32_t native_legacy_guard_slot;
volatile uint64_t native_legacy_guard_object,native_legacy_guard_caller;
volatile int native_legacy_surface_status=-1;
volatile uint32_t native_legacy_surface_found;
//: How long the off-thread wait is allowed to run, in milliseconds.  Read
//: back from the guest so a run can say what window it actually used.
volatile uint32_t native_legacy_surface_wait_ms;
volatile uint64_t native_legacy_video_base,native_legacy_video_bytes;
volatile uint64_t native_legacy_gop_map,native_legacy_gop_virtual,native_legacy_gop_physical,native_legacy_gop_bytes;
volatile uint32_t native_legacy_gop_width,native_legacy_gop_height;
volatile uint32_t native_legacy_registration_calls,native_legacy_properties_length;
volatile uint32_t native_legacy_dt_attached;
volatile int native_legacy_unit_scale_status=-1;
volatile uint32_t native_legacy_unit_scale_calls,native_legacy_chosen_scale,native_legacy_chosen_rotation;
volatile uint32_t native_legacy_pm_stage,native_legacy_pm_calls,native_legacy_pm_callbacks,native_legacy_pm_ordinal;
volatile int native_legacy_pm_status=-1;
volatile uint32_t native_legacy_swap_validation_calls,native_legacy_swap_validation_result,native_legacy_swap_validation_reason;
volatile uint64_t native_legacy_swap_request,native_legacy_swap_surface;
volatile uint32_t native_legacy_transfer_calls;
volatile int native_legacy_transfer_status=-1;
volatile uint64_t native_legacy_transfer_bytes,native_legacy_transfer_request;
volatile uint32_t native_legacy_retirement_calls,native_legacy_retired_generation,native_legacy_presented_generation,native_legacy_retired_frames;
volatile uint64_t native_legacy_retired_current,native_legacy_retired_pending;
char native_legacy_properties[4096];
volatile uint32_t native_legacy_client_calls,native_legacy_client_type;
volatile int native_legacy_client_pid,native_legacy_client_status=-1;
volatile uint64_t native_legacy_client_task,native_legacy_client_object;
char native_legacy_client_class[64];
extern kern_return_t OSKextLoadKextWithIdentifier(const char *);
extern int boot_progress_prepare_scanout(void) __attribute__((weak_import));
extern void boot_progress_cancel_scanout(void) __attribute__((weak_import));
extern void boot_progress_stage(const char *) __attribute__((weak_import));
volatile uint32_t native_legacy_prepare_calls,native_legacy_prepare_reason,native_legacy_prepare_attempts;
volatile uint32_t native_legacy_prepare_failures,native_legacy_prepare_retried,native_legacy_prepare_max_attempts;
volatile uint64_t native_legacy_prepare_wait_us;
// Task #36: say WHERE the kick path stops.  native_legacy_kick_calls counts
// kickSwapQueue entries; native_legacy_kick_gate records the last gate taken
// (0 = the loop ran, 1 = re-entered while kicking).  native_legacy_kick_once_*
// do the same for kickOnce's two gates, with native_legacy_kick_once_reason
// naming which sub-condition of gate 1 fired, so "no swap was submitted" can be
// attributed instead of guessed.
volatile uint32_t native_legacy_kick_calls,native_legacy_kick_gate,native_legacy_kick_rekick;
volatile uint32_t native_legacy_kick_once_calls,native_legacy_kick_once_gate,native_legacy_kick_once_reason;
static uint64_t probeNowUs(void) {uint64_t ns;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
bool native_legacy_prepare_scanout(void) {
    native_legacy_prepare_calls=native_legacy_prepare_calls+1;native_legacy_prepare_reason=0;native_legacy_prepare_attempts=0;
    if (!boot_progress_prepare_scanout) return true;
    if (ml_at_interrupt_context()) {native_legacy_prepare_reason=1;return false;}
    if (!ml_get_interrupts_enabled()) {native_legacy_prepare_reason=2;return false;}
    // Yield to any in-flight diagnostic writer; never spin on a preempted
    // holder or claim a frame that the console can still overwrite.
    const uint64_t began=probeNowUs();
    for (unsigned attempt=0;attempt<100;++attempt) {
        native_legacy_prepare_attempts=attempt+1;
        if (boot_progress_prepare_scanout()) {
            if(attempt) {
                native_legacy_prepare_retried=native_legacy_prepare_retried+1;native_legacy_prepare_wait_us=native_legacy_prepare_wait_us+(probeNowUs()-began);
                if(attempt+1>native_legacy_prepare_max_attempts)native_legacy_prepare_max_attempts=attempt+1;
            }
            return true;
        }
        IOSleep(1);
    }
    native_legacy_prepare_failures=native_legacy_prepare_failures+1;native_legacy_prepare_wait_us=native_legacy_prepare_wait_us+(probeNowUs()-began);
    native_legacy_prepare_reason=3;
    if(boot_progress_cancel_scanout)boot_progress_cancel_scanout();
    return false;
}
//: Append one unsigned decimal to a bounded record.  Hand-formatted because
//: this translation unit includes no header that declares snprintf -- the
//: same constraint HidInputProbe.cpp records.  Ten digits is the widest a
//: 32-bit value can be, so text[10] truncates rather than overflows.
static unsigned native_legacy_put_unsigned(char *record,unsigned used,unsigned size,unsigned value) {
    char text[10];unsigned digits=0;
    if(!value)text[digits++]=(char)0x30;
    while(value&&digits<sizeof(text)){text[digits++]=(char)(0x30+value%10);value/=10;}
    while(digits&&used<size-1)record[used++]=text[--digits];
    return used;
}
//: Append one bounded literal.  Returns the new length, never past size-1.
static unsigned native_legacy_put_literal(char *record,unsigned used,unsigned size,const char *text,unsigned length) {
    for(unsigned i=0;i<length&&used<size-1;++i)record[used++]=text[i];
    return used;
}
//: THE ONE OBSERVABLE POINT IN THIS FILE, and it was missing.
//:
//: native_legacy_stage walks 1..8 and 10, native_legacy_surface_status and
//: native_legacy_surface_found record whether IOSurface loaded and whether
//: IOCoreSurfaceRoot appeared inside the wait, and native_legacy_dt_attached
//: records the /arm-io gop-display attach.  Every one of them was written by
//: a path in native_legacy_display_late_body and read by nothing, so "the
//: display never came up" and "the display came up and drew nothing" were
//: the same silence from the host.
//:
//: MEASURED, and the reason this exists: two boots, 3538 s and 1500 s, both
//: froze the framebuffer onto the UniXNU boot menu by t=121 s and never moved
//: it again, while the guest channel delivered dyld.cache_read at t=914 s and
//: t=1055 s.  The kernel was running userspace code with a display showing a
//: boot menu, and no record said which stage the display path had reached.
//: The boot args carry offline-legacy-surface-wait-ms=600000, so the wait
//: was still open when the screen stopped -- the path had not been given up
//: on, it simply never produced a frame.
//:
//: Bounded: at most four checkpoints per boot (two in the early init, two in
//: the late body), so at most four records, each at most one 2000 ms
//: transport deadline.  Every append is bounded by size-1 regardless.
__attribute__((noinline)) void native_legacy_display_checkpoint(void) {
    __asm__ volatile("" ::: "memory");
    static char const prefix[]="{\"event\":\"diag.legacy_display_status\",\"stage\":";
    static char const surface[]=",\"surface_status\":";
    static char const found[]=",\"surface_found\":";
    static char const dt[]=",\"dt\":";
    static char const suffix[]=",\"source\":\"XNU.LegacyDisplayProbe\"}\n";
    char record[192];unsigned used=0;
    used=native_legacy_put_literal(record,used,sizeof(record),prefix,sizeof(prefix)-1);
    used=native_legacy_put_unsigned(record,used,sizeof(record),native_legacy_stage);
    used=native_legacy_put_literal(record,used,sizeof(record),surface,sizeof(surface)-1);
    used=native_legacy_put_unsigned(record,used,sizeof(record),(unsigned)(native_legacy_surface_status<0?0u:(unsigned)native_legacy_surface_status));
    used=native_legacy_put_literal(record,used,sizeof(record),found,sizeof(found)-1);
    used=native_legacy_put_unsigned(record,used,sizeof(record),native_legacy_surface_found);
    used=native_legacy_put_literal(record,used,sizeof(record),dt,sizeof(dt)-1);
    used=native_legacy_put_unsigned(record,used,sizeof(record),native_legacy_dt_attached);
    used=native_legacy_put_literal(record,used,sizeof(record),suffix,sizeof(suffix)-1);
    record[used]=0;
    native_console_report(record,used);
}
__attribute__((noinline)) void native_legacy_gop_mapping_checkpoint(void) {__asm__ volatile("" ::: "memory");}
__attribute__((noinline)) void native_legacy_client_checkpoint(void) {__asm__ volatile("" ::: "memory");}
void native_legacy_pm_observed(unsigned stage,IOReturn status,unsigned long ordinal) {
    native_legacy_pm_stage=stage;native_legacy_pm_status=status;native_legacy_pm_ordinal=(uint32_t)ordinal;
    native_legacy_pm_calls=native_legacy_pm_calls+1;
    if(stage==3)native_legacy_pm_callbacks=native_legacy_pm_callbacks+1;
}
__attribute__((noinline)) void native_legacy_swap_validation_checkpoint(bool,unsigned,const void *,const void *) {__asm__ volatile("" ::: "memory");}
// Last rejected swap request, copied from the original request layout the validator reads (offsets
// qualified in NativeLegacyDisplay::validateSwap). Observation only; nothing here changes the verdict.
volatile uint32_t native_legacy_swap_accepted_count,native_legacy_swap_rejected_count,native_legacy_rej_reason;
volatile uint64_t native_legacy_rej_surfaces[4];
volatile uint32_t native_legacy_rej_state[3],native_legacy_rej_options[3],native_legacy_rej_size[2],native_legacy_rej_rects[2][4];
char native_legacy_rej_class[32];
volatile uint64_t native_legacy_rej_surface[7];   // width, height, format, planes, stride, allocation, descriptor bytes of the last rejected surface
void native_legacy_surface_rejected(uint64_t width,uint64_t height,uint32_t format,uint32_t planes,uint64_t stride,uint64_t allocation,uint64_t bytes) {
    native_legacy_rej_surface[0]=width;native_legacy_rej_surface[1]=height;native_legacy_rej_surface[2]=format;native_legacy_rej_surface[3]=planes;
    native_legacy_rej_surface[4]=stride;native_legacy_rej_surface[5]=allocation;native_legacy_rej_surface[6]=bytes;
}
static void recordRejectedSwap(unsigned reason,const void *request,const void *surface) {
    const unsigned char *bytes=reinterpret_cast<const unsigned char *>(request);
    if(!bytes)return;
    native_legacy_rej_reason=reason;
    uint64_t surfaces[4]={};memcpy(surfaces,bytes+2624,sizeof(surfaces));
    for(unsigned i=0;i<4;++i)native_legacy_rej_surfaces[i]=surfaces[i];
    const unsigned stateOffsets[3]={792,796,800},optionOffsets[3]={804,808,812};
    for(unsigned i=0;i<3;++i){uint32_t a=0,b=0;memcpy(&a,bytes+stateOffsets[i],4);memcpy(&b,bytes+optionOffsets[i],4);
        native_legacy_rej_state[i]=a;native_legacy_rej_options[i]=b;}
    uint32_t w=0,h=0;memcpy(&w,bytes+816,4);memcpy(&h,bytes+820,4);native_legacy_rej_size[0]=w;native_legacy_rej_size[1]=h;
    const unsigned rectOffsets[2]={17,81};
    for(unsigned r=0;r<2;++r){uint32_t rect[4]={};memcpy(rect,bytes+rectOffsets[r],sizeof(rect));
        for(unsigned i=0;i<4;++i)native_legacy_rej_rects[r][i]=rect[i];}
    char name[32]={};
    if(surface){const OSObject *object=reinterpret_cast<const OSObject *>(surface);
        const OSMetaClass *meta=object->getMetaClass();const char *cls=meta?meta->getClassName():nullptr;
        if(cls)strlcpy(name,cls,sizeof(name));}
    memcpy(native_legacy_rej_class,name,sizeof(name));
}
// Content statistics of presented frames (copy = kind 0, blank fill = kind 1): the first 32 are kept, with the time
// in seconds since the first frame. A per-frame record shows the animation cadence and when the OS blanks the panel.
#define NATIVE_LEGACY_FRAMES 32
volatile uint32_t native_legacy_frame_count;
volatile uint32_t native_legacy_frame_kind[NATIVE_LEGACY_FRAMES];
volatile uint32_t native_legacy_frame_time[NATIVE_LEGACY_FRAMES];
volatile uint64_t native_legacy_frame_stats[NATIVE_LEGACY_FRAMES][5];
static uint64_t frameEpoch;
void native_legacy_frame_observed(unsigned kind,uint64_t count,uint64_t minx,uint64_t miny,uint64_t maxx,uint64_t maxy) {
    uint32_t n=native_legacy_frame_count;
    uint64_t now,frequency;
    __asm__ volatile("isb\n\tmrs %0, cntpct_el0":"=r"(now));__asm__ volatile("mrs %0, cntfrq_el0":"=r"(frequency));
    if(!frameEpoch)frameEpoch=now;
    if(n<NATIVE_LEGACY_FRAMES) {
        native_legacy_frame_kind[n]=kind;
        native_legacy_frame_time[n]=frequency?(uint32_t)((now-frameEpoch)/frequency):0;
        native_legacy_frame_stats[n][0]=count;native_legacy_frame_stats[n][1]=minx;native_legacy_frame_stats[n][2]=miny;
        native_legacy_frame_stats[n][3]=maxx;native_legacy_frame_stats[n][4]=maxy;
    }
    native_legacy_frame_count=n+1;
}
volatile uint32_t native_legacy_frame_colors[8][4];
volatile uint64_t native_legacy_frame_color_counts[8][4],native_legacy_frame_color_other[8];
// Called right after the matching native_legacy_frame_observed(); fills slot count-1 (or the blank slot).
void native_legacy_frame_colors_observed(const uint32_t *colors,const uint64_t *counts,uint64_t other) {
    uint32_t n=native_legacy_frame_count;
    if(!n || n>8)return;   // dominant colours are kept for the first eight frames only
    n-=1;
    for(unsigned i=0;i<4;++i){native_legacy_frame_colors[n][i]=colors[i];native_legacy_frame_color_counts[n][i]=counts[i];}
    native_legacy_frame_color_other[n]=other;
}
// The life of every accepted swap, from the kernel's side: validated (the client submitted it), offered to the GPU copy (or started on the CPU),
// and retired. Times are microseconds since boot; the last 64 are kept.
struct SwapEvent {uint64_t submitted,offered,done;uint32_t number;};
static SwapEvent swapEvents[64];static uint32_t swapEventCount;
extern "C" void native_legacy_swap_stage(unsigned stage) {
    const uint64_t now=probeNowUs();
    uint32_t total=swapEventCount;
    if(stage==0) {
        SwapEvent &e=swapEvents[total%64];e={now,0,0,total+1};swapEventCount=total+1;return;
    }
    uint32_t first=total>64?total-64:0;
    for(uint32_t i=first;i<total;++i) {
        SwapEvent &e=swapEvents[i%64];
        if(stage==1 && !e.offered && !e.done){e.offered=now;return;}
        if(stage==2 && !e.done){e.done=now;if(!e.offered)e.offered=now;return;}
    }
}
extern "C" unsigned native_legacy_swap_timeline(char *out,unsigned cap) {
    unsigned at=0;out[0]=0;
    uint32_t total=swapEventCount,first=total>40?total-40:0;
    for(uint32_t i=first;i<total && at+90<cap;++i) {
        const SwapEvent &e=swapEvents[i%64];
        uint64_t previous=i>first?swapEvents[(i-1)%64].submitted:e.submitted;
        at+=snprintf(out+at,cap-at,"W%u t=%llu.%03llus +%llums offer=%lldms done=%lldms\n",e.number,(unsigned long long)(e.submitted/1000000),
            (unsigned long long)((e.submitted/1000)%1000),(unsigned long long)((e.submitted-previous)/1000),
            e.offered?(long long)((e.offered-e.submitted)/1000):-1LL,e.done?(long long)((e.done-e.submitted)/1000):-1LL);
    }
    return at;
}
extern "C" unsigned native_legacy_prepare_report(char *out,unsigned cap) {
    return snprintf(out,cap,"prepare calls=%u failures=%u retried=%u max_attempts=%u waited_ms=%llu",native_legacy_prepare_calls,native_legacy_prepare_failures,
        native_legacy_prepare_retried,native_legacy_prepare_max_attempts,(unsigned long long)(native_legacy_prepare_wait_us/1000));
}
void native_legacy_swap_validation_observed(bool accepted,unsigned reason,const void *request,const void *surface) {
    if(accepted)native_legacy_swap_stage(0);
    if(accepted)native_legacy_swap_accepted_count=native_legacy_swap_accepted_count+1;
    else {native_legacy_swap_rejected_count=native_legacy_swap_rejected_count+1;recordRejectedSwap(reason,request,surface);}
    native_legacy_swap_validation_calls=native_legacy_swap_validation_calls+1;
    native_legacy_swap_validation_result=accepted;native_legacy_swap_validation_reason=reason;
    native_legacy_swap_request=(uintptr_t)request;native_legacy_swap_surface=(uintptr_t)surface;
    native_legacy_swap_validation_checkpoint(accepted,reason,request,surface);
}
// Time of the pixel copy into the scanout buffer (counter ticks): swaps completed, last/maximum/sum, for the banner.
volatile uint64_t native_legacy_copy_n,native_legacy_copy_last,native_legacy_copy_max,native_legacy_copy_sum,native_legacy_copy_bytes_last,native_legacy_copy_bytes_sum;
void native_legacy_copy_timing_observed(uint64_t ticks,uint64_t bytes) {
    native_legacy_copy_bytes_last=bytes;native_legacy_copy_bytes_sum=native_legacy_copy_bytes_sum+bytes;
    native_legacy_copy_n=native_legacy_copy_n+1;native_legacy_copy_last=ticks;native_legacy_copy_sum=native_legacy_copy_sum+ticks;
    if(ticks>native_legacy_copy_max)native_legacy_copy_max=ticks;
}
__attribute__((noinline)) void native_legacy_transfer_observed(IOReturn status,const void *request,uint64_t bytes) {
    native_legacy_transfer_calls=native_legacy_transfer_calls+1;
    if(status==kIOReturnSuccess)native_legacy_swap_stage(2);
    native_legacy_transfer_status=status;native_legacy_transfer_request=(uintptr_t)request;native_legacy_transfer_bytes=bytes;
    if(status!=kIOReturnSuccess && boot_progress_stage) {
        char message[120];snprintf(message,sizeof(message),"DISPLAY COPY FAILED status=%08x prepare=%u tries=%u",
          (unsigned)status,native_legacy_prepare_reason,native_legacy_prepare_attempts);
        boot_progress_stage(message);
    }
}
__attribute__((noinline)) void native_legacy_kick_observed(unsigned gate) {
    native_legacy_kick_calls=native_legacy_kick_calls+1;
    native_legacy_kick_gate=gate;
}
// Task #36.  gate 1 is kickOnce's precondition return, gate 2 its
// request/validation return, gate 0 means both were passed.  reason names which
// sub-condition of gate 1 fired: 1 not enabled, 2 already transferred,
// 3 no logicalPM, 4 no bootMapping, 5 not primary, 6 no pending request,
// 7 request failed validation.
__attribute__((noinline)) void native_legacy_kick_once_observed(unsigned gate,unsigned reason) {
    native_legacy_kick_once_calls=native_legacy_kick_once_calls+1;
    native_legacy_kick_once_gate=gate;
    native_legacy_kick_once_reason=reason;
}
bool native_legacy_gpu_offer(void *display,IOMemoryDescriptor *memory,uint64_t stride,uint32_t width,uint32_t height) {
    return gpuHandoffPresenter && native_gop_gpu_offer(gpuHandoffPresenter,display,memory,stride,width,height);
}
void native_legacy_gpu_fallback_observed(int status) {
    native_legacy_gpu_last_status=(uint32_t)status;native_legacy_gpu_fallbacks=native_legacy_gpu_fallbacks+1;
}
__attribute__((noinline)) void native_legacy_retirement_observed(IOService *display) {
    const unsigned char *bytes=reinterpret_cast<const unsigned char *>(display);
    uint64_t current=0,pending=0;uint32_t completed=0,presented=0,frames=0;
    memcpy(&current,bytes+5472,8);memcpy(&pending,bytes+5480,8);
    memcpy(&completed,bytes+5820,4);memcpy(&presented,bytes+5824,4);memcpy(&frames,bytes+252,4);
    native_legacy_retired_current=current;native_legacy_retired_pending=pending;
    native_legacy_retired_generation=completed;native_legacy_presented_generation=presented;native_legacy_retired_frames=frames;
    native_legacy_retirement_calls=native_legacy_retirement_calls+1;
}
// The display object's properties as they are now (the registration copy above predates setDisplayRefreshProperties and the rest): for the
// UFS log. Returns the bytes written (NUL-terminated).
extern "C" unsigned native_legacy_properties_now(char *out,unsigned cap) {
    if(!out || !cap)return 0;
    out[0]=0;
    IOService *display=reinterpret_cast<IOService *>(native_legacy_object);
    if(!display)return 0;
    OSSerialize *xml=OSSerialize::withCapacity(8192);
    unsigned size=0;
    if(xml && display->serializeProperties(xml)) {
        size=xml->getLength();if(size>=cap)size=cap-1;
        memcpy(out,xml->text(),size);out[size]=0;
    }
    OSSafeReleaseNULL(xml);
    return size;
}
void native_legacy_registration_observed(IOService *display) {
    native_legacy_registration_calls=native_legacy_registration_calls+1;
    OSSerialize *xml=OSSerialize::withCapacity(2048);
    if(xml && display->serializeProperties(xml)) {
        unsigned size=xml->getLength();if(size>=sizeof(native_legacy_properties))size=sizeof(native_legacy_properties)-1;
        memcpy(native_legacy_properties,xml->text(),size);native_legacy_properties[size]=0;native_legacy_properties_length=size;
    }
    OSSafeReleaseNULL(xml);
}
void native_legacy_client_enter(task_t task,uint32_t type) {
    native_legacy_client_calls=native_legacy_client_calls+1;native_legacy_client_task=(uintptr_t)task;
    native_legacy_client_type=type;native_legacy_client_pid=task?proc_pid((proc_t)get_bsdtask_info(task)):0;
}
// Every framebuffer user client seen (object, pid), with what it does: the original client's vtable is copied and two entries (externalMethod, slot 168,
// and registerNotificationPort(port,type,refCon), slot 175) are pointed at wrappers that count and forward to the original function. The original code
// is not touched; only this object's vptr names the copy. XNU's ARM64 vptr points at slot 0 (the original code dispatches with ldr x8,[vptr,#slot*8]).
enum {kSeenClients=8,kSelectors=96,kRegEvents=8,kVtableSlots=256};
struct ClientStat {
    uintptr_t object;int pid;uint32_t calls,fails;uint32_t selector_calls[kSelectors],selector_fails[kSelectors],selector_status[kSelectors];
    // Task #41: the first argument set EACH selector was called with.  Selector 6
    // is the wait, not the swap (NativeLegacyDisplay.cpp:91-93), so the swap
    // submission cannot be identified from selector 6's arguments.  The swap is a
    // SwapSetLayer / SwapSetBackgroundColor request (NativeLegacyRequest.h:4-5),
    // so the submission is the selector whose arguments carry a layer or a
    // background colour -- and that is only visible if every selector's arguments
    // are recorded, not one selector's.
    //
    // Addendum EL: the scalar-only version narrowed the candidates to selectors 29
    // and 57 -- the two that carry NO scalar input and refuse -- but could not
    // separate them, because no scalar input is exactly why they are candidates.
    // A layer and a surface are not scalars, so the structure is where the shape
    // lives.  Record its size and its first words for the selectors that have one.
    uint64_t sel_args[kSelectors][3];uint8_t sel_args_set[kSelectors];
    uint32_t sel_struct_size[kSelectors];uint8_t sel_struct_set[kSelectors];
    uint64_t sel_struct_words[kSelectors][4];
    unsigned regs;struct {uint32_t type;uint64_t port;uint32_t ref,status;} reg[kRegEvents];
    uint64_t sel6_first[3],sel6_last[3];uint32_t sel6_distinct;uint64_t sel6_seen[8][3];uint32_t sel6_calls[8],sel6_fails[8],sel6_status[8];
    uint32_t seq_count;struct {uint32_t a,b,c,status,done,presented;} seq[24];
    uintptr_t original_external,original_register;
};
static ClientStat seenClients[kSeenClients];static unsigned seenClientCount;
typedef IOReturn (*ExternalFn)(IOUserClient *,uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,OSObject *,void *);
typedef IOReturn (*RegisterFn)(IOUserClient *,mach_port_t,UInt32,UInt32);
static ClientStat *statFor(IOUserClient *client) {
    for(unsigned i=0;i<seenClientCount;++i)if(seenClients[i].object==(uintptr_t)client)return &seenClients[i];
    return nullptr;
}
static IOReturn wrappedExternal(IOUserClient *client,uint32_t selector,IOExternalMethodArguments *args,IOExternalMethodDispatch *dispatch,OSObject *target,void *ref) {
    ClientStat *stat=statFor(client);
    IOReturn status=stat?reinterpret_cast<ExternalFn>(stat->original_external)(client,selector,args,dispatch,target,ref):kIOReturnNotAttached;
    if(stat) {
        if(selector==6 && args && args->scalarInput && args->scalarInputCount>=3) {
            const uint64_t *in=args->scalarInput;
            if(!stat->sel6_distinct && !stat->sel6_first[0] && !stat->sel6_first[1])memcpy(stat->sel6_first,in,sizeof stat->sel6_first);
            memcpy(stat->sel6_last,in,sizeof stat->sel6_last);
            int at=-1;
            for(unsigned k=0;k<stat->sel6_distinct && k<8;++k)if(!memcmp(stat->sel6_seen[k],in,24))at=(int)k;
            if(at<0 && stat->sel6_distinct<8){at=(int)stat->sel6_distinct++;memcpy(stat->sel6_seen[at],in,24);}
            if(stat->seq_count<24){
                auto &e=stat->seq[stat->seq_count++];e.a=(uint32_t)in[0];e.b=(uint32_t)in[1];e.c=(uint32_t)in[2];e.status=(uint32_t)status;
                const unsigned char *object=reinterpret_cast<const unsigned char *>(native_legacy_object);
                e.done=e.presented=0;if(object){memcpy(&e.done,object+5820,4);memcpy(&e.presented,object+5824,4);}
            }
            if(at>=0){++stat->sel6_calls[at];if(status)++stat->sel6_fails[at];stat->sel6_status[at]=(uint32_t)status;}
        }
        ++stat->calls;if(status)++stat->fails;
        if(selector<kSelectors){
            ++stat->selector_calls[selector];if(status)++stat->selector_fails[selector];stat->selector_status[selector]=(uint32_t)status;
            // Task #41: the FIRST argument set each selector was called with, so
            // the swap submission can be recognised by its shape rather than
            // guessed from its number.  Recorded once per selector; the swap is a
            // layer or background-colour request, so a selector whose arguments
            // never look like one is not the submission.
            if(!stat->sel_args_set[selector] && args && args->scalarInput && args->scalarInputCount>=1){
                stat->sel_args[selector][0]=args->scalarInput[0];
                stat->sel_args[selector][1]=args->scalarInputCount>1?args->scalarInput[1]:0;
                stat->sel_args[selector][2]=args->scalarInputCount>2?args->scalarInput[2]:0;
                stat->sel_args_set[selector]=1;
            }
            // Addendum EL: the STRUCTURE, for the selectors whose arguments are
            // not scalars.  A SwapSetLayer request carries a layer and a surface
            // in its structure, so this is the only place the swap submission's
            // shape is visible.
            if(!stat->sel_struct_set[selector] && args && args->structureInput && args->structureInputSize>=8){
                stat->sel_struct_size[selector]=(uint32_t)args->structureInputSize;
                const uint64_t *w=static_cast<const uint64_t *>(args->structureInput);
                for(unsigned k=0;k<4 && (k+1)*8<=args->structureInputSize;++k)stat->sel_struct_words[selector][k]=w[k];
                stat->sel_struct_set[selector]=1;
            }
        }
    }
    return status;
}
static IOReturn wrappedRegister(IOUserClient *client,mach_port_t port,UInt32 type,UInt32 refCon) {
    ClientStat *stat=statFor(client);
    IOReturn status=stat?reinterpret_cast<RegisterFn>(stat->original_register)(client,port,type,refCon):kIOReturnNotAttached;
    if(stat && stat->regs<kRegEvents){auto &e=stat->reg[stat->regs++];e.type=type;e.port=(uint64_t)(uintptr_t)port;e.ref=refCon;e.status=(uint32_t)status;}
    return status;
}
static void interpose(IOUserClient *client,int pid) {
    if(seenClientCount>=kSeenClients)return;
    ClientStat &stat=seenClients[seenClientCount];
    memset(&stat,0,sizeof stat);
    uintptr_t *vtable=*reinterpret_cast<uintptr_t **>(client);
    uintptr_t *copy=static_cast<uintptr_t *>(IOMalloc(kVtableSlots*sizeof(uintptr_t)));
    if(!copy)return;
    memcpy(copy,vtable,kVtableSlots*sizeof(uintptr_t));
    stat.object=(uintptr_t)client;stat.pid=pid;stat.original_external=copy[168];stat.original_register=copy[175];
    copy[168]=(uintptr_t)&wrappedExternal;copy[175]=(uintptr_t)&wrappedRegister;
    ++seenClientCount;
    *reinterpret_cast<uintptr_t **>(client)=copy;
}
// The display object's state words the original wait selector tests (offsets 5832..5864, as dwords), for the log.
extern "C" unsigned native_legacy_state_words(char *out,unsigned cap) {
    out[0]=0;
    const unsigned char *object=reinterpret_cast<const unsigned char *>(native_legacy_object);
    if(!object)return 0;
    unsigned at=snprintf(out,cap,"d+5808:");
    for(unsigned o=5808;o<5872 && at+16<cap;o+=4){uint32_t v;memcpy(&v,object+o,4);at+=snprintf(out+at,cap-at," %x",v);}
    return at;
}
extern "C" unsigned native_legacy_clients_dump(char *out,unsigned cap) {
    unsigned at=0;out[0]=0;
    for(unsigned c=0;c<seenClientCount && at+120<cap;++c) {
        const ClientStat &s=seenClients[c];
        at+=snprintf(out+at,cap-at,"C%u pid=%d calls=%u fails=%u regs=%u",c,s.pid,s.calls,s.fails,s.regs);
        for(unsigned r=0;r<s.regs && r<kRegEvents && at+60<cap;++r)
            at+=snprintf(out+at,cap-at," reg(t%u,p%llx,r%x)=%x",s.reg[r].type,(unsigned long long)s.reg[r].port,s.reg[r].ref,s.reg[r].status);
        at+=snprintf(out+at,cap-at,"\n");
        for(unsigned k=0;k<s.sel6_distinct && k<8 && at+100<cap;++k)
            at+=snprintf(out+at,cap-at,"  sel6 args %llx %llx %llx: %u calls %u fails last %x\n",(unsigned long long)s.sel6_seen[k][0],(unsigned long long)s.sel6_seen[k][1],(unsigned long long)s.sel6_seen[k][2],s.sel6_calls[k],s.sel6_fails[k],s.sel6_status[k]);
        for(unsigned k=0;k<s.seq_count && at+90<cap;++k)
            at+=snprintf(out+at,cap-at,"  s6#%u %x %x %x -> %x [5820]=%x [5824]=%x\n",k,s.seq[k].a,s.seq[k].b,s.seq[k].c,s.seq[k].status,s.seq[k].done,s.seq[k].presented);
        unsigned n=0;
        for(unsigned sel=0;sel<kSelectors && at+40<cap;++sel) {
            if(!s.selector_calls[sel])continue;
            if(!n)at+=snprintf(out+at,cap-at,"  sel");
            at+=snprintf(out+at,cap-at," %u:%u/%u/%x",sel,s.selector_calls[sel],s.selector_fails[sel],s.selector_status[sel]);
            if(++n%6==0)at+=snprintf(out+at,cap-at,"\n  sel");
        }
        if(n)at+=snprintf(out+at,cap-at,"\n");
        // Task #41: every selector's FIRST argument set, so the swap submission
        // can be recognised by its shape.  A swap is a SwapSetLayer /
        // SwapSetBackgroundColor request (NativeLegacyRequest.h:4-5), so the
        // submission is the selector whose arguments carry a layer or a
        // background colour.
        for(unsigned sel=0;sel<kSelectors && at+70<cap;++sel) {
            if(!s.selector_calls[sel] || !s.sel_args_set[sel])continue;
            at+=snprintf(out+at,cap-at,"  sel%u args %llx %llx %llx\n",sel,
                         (unsigned long long)s.sel_args[sel][0],
                         (unsigned long long)s.sel_args[sel][1],
                         (unsigned long long)s.sel_args[sel][2]);
        }
        // Addendum EL: the structure of each selector that has one.  A swap is a
        // SwapSetLayer / SwapSetBackgroundColor request, so the submission is the
        // selector whose structure carries a layer or a background colour.
        for(unsigned sel=0;sel<kSelectors && at+90<cap;++sel) {
            if(!s.selector_calls[sel] || !s.sel_struct_set[sel])continue;
            at+=snprintf(out+at,cap-at,"  sel%u struct %u bytes: %llx %llx %llx %llx\n",sel,
                         s.sel_struct_size[sel],
                         (unsigned long long)s.sel_struct_words[sel][0],
                         (unsigned long long)s.sel_struct_words[sel][1],
                         (unsigned long long)s.sel_struct_words[sel][2],
                         (unsigned long long)s.sel_struct_words[sel][3]);
        }
    }
    return at;
}
// ---- the display's own vtable, traced -------------------------------------
// The one instrument that can say which of the display's methods a refused
// selector drives.  Static BL-reachability cannot: addendum AI measured that
// selector 74's dispatch function makes 5 direct BL calls and its five callees
// make 26 indirect BLR between them, so a walk stops after six nodes and
// reports "reaches nothing" for a selector whose refusal demonstrably came from
// slot 213.  The virtual calls sit one level down, all in leaves.
//
// So the display's vtable is copied and every entry replaced with a thunk that
// counts the call and branches to the original.  x16/x17 are IP0/IP1 and x9/x10
// are temporaries, so nothing in x0-x7 or the return in x0 is disturbed -- a
// thunk is transparent, which is the whole reason this can be left armed
// through a boot.
//
// OFF unless offline-legacy-slot-trace=1 is on the command line, so a defect here costs one
// boot rather than the board.
// ---- the display's own vtable, lifted --------------------------------------
// The one instrument that can say whether the refusals are the gate.
//
// WHY IT IS NOT A THUNK ANY MORE, and this is the third build failure on this
// instrument, so the reasoning is recorded rather than the outcome.  The first
// version copied the display's vtable and replaced every entry with a naked
// AArch64 thunk that counted the call and branched to the original.  Three
// builds failed on it:
//
//   run 41906787039  six assembler errors -- the Apple kext relocation model
//                    refuses `adrp sym; add :lo12:sym` and wants
//                    `sym@GOTPAGE` / `sym@GOTPAGEOFF`;
//   run 41915052472  the kernel LINK -- a symbol named from inline asm is
//                    emitted as an undefined reference, and neither a `static`
//                    symbol nor a tentative `__DATA,__common` definition
//                    satisfies one;
//   run 41924165302  the kernel LINK again -- and this is the one that settled
//                    it.  `qemu_defconfig`, the FIRST pass, links this driver's
//                    objects DIRECTLY and succeeds.  `qemu_root_defconfig`, the
//                    SECOND pass (PRD 8.1's root chain), links them out of
//                    `libNativeLegacyDisplay.late.a` and fails.  Same source,
//                    same object, different link shape.
//
// The rule that falls out of those three is that ld64 does not resolve an
// asm-created undefined reference against a definition in an archive member.
// There is no form of the asm that avoids creating one.  So the thunks are gone.
//
// WHAT REPLACES THEM, and why it is enough.  A refusing stub writes NOTHING.
// Its body is `MOV w0,#0x2c7` and a RET.  So answering it needs only a function
// that returns kIOReturnSuccess and touches nothing else -- an ordinary C++
// function, reached from the arm function by compiled code, which links from an
// archive without complaint.  The vtable entry is pointed straight at it and the
// original is never entered.
//
// What is given up is the per-slot call count, which needed the thunks.  What
// replaces it is the probe's own client table, which already records per
// selector the status the display answered -- and THAT is the measurement this
// experiment is for.  "Which selector's status changed" is answerable from the
// table; "how many times was slot 213 entered" was never the question.
//
// OFF unless offline-legacy-lift-refusals=1 is on the command line, so a defect
// here costs one boot rather than the board.
enum {kDisplaySlots=358};
// extern "C" and not static: the host reads these by symbol name over QMP, and
// a file-local symbol is not guaranteed to be in the image's table the way a
// global is.  extern "C" keeps the name exactly as spelled here.
//
// THE INITIALIZERS ARE LOAD-BEARING, and run 41931696815 is why.  In C++ a
// declaration at namespace scope with no initializer is a DECLARATION, not a
// definition: clang emits nothing for it and the symbol does not exist in the
// object at all.  The link then fails with "undefined symbol" naming a symbol
// the reader can see spelled three lines above, which reads as a linkage puzzle
// and is a missing definition.  Established locally with ld64.lld, which ships
// in this project's msys64 tree: of five declaration forms, the only one that
// fails to link either way is the one with no initializer, and its symbol is
// absent from llvm-objdump -t entirely.
//
// There is deliberately NO per-slot call counter.  The thunks that would have
// filled one are gone -- see the banner above -- and an array the host can read
// that is always zero is worse than no array, because it looks like a
// measurement.  The measurement is the probe's own client table, which records
// per selector the status the display answered.
extern "C" uintptr_t native_legacy_slot_original[kDisplaySlots]={};
//: How many entries the copied vtable holds.  The host checks it is 358, which
//: is how it knows the copy installed; it is not a count of anything that
//: happened.
extern "C" uint32_t native_legacy_slot_armed_count=0;
//: How many refusing stubs were answered instead of entered.  Zero means the
//: classifier found nothing, which is a finding about the classifier, not about
//: the display.
extern "C" uint32_t native_legacy_slot_lifted_count=0;
static bool native_legacy_slot_trace_armed;

// The answer.  It writes x0 and nothing else, which is precisely the property
// the refusing stub it replaces already has -- so the lift changes the return
// value and no out parameter a caller might read.  A caller that dereferenced
// those outputs would already be reading uninitialised memory today, and it is
// not crashing today.  The lift is not more dangerous than the status quo; it
// is the status quo with a different answer.
extern "C" uintptr_t native_legacy_slot_success(void) { return 0; }

// Addendum AC's classifier.  A slot refuses only if its function is a STUB:
// `MOV w0,#0x2c7` with nothing CALLED before it and a RET soon after.  The
// "nothing called" clause is what separates a stub from a real implementation
// with a dead tail -- the LEAF() guards at 306, 307 and 337 are
// `MOV w0,#<slot>; BL native_legacy_backend_guard` with a noreturn panic, and
// the compiler still emits a dead `MOV w0,#0x2c7` after them.  Matching those
// would answer a guard's call with success instead of panicking, which is not
// what an experiment should do.
//
// BOUNDED to the first 24 words (96 bytes), the same window the host classifier
// read.  A stub is shorter than that; the bound is recorded because an
// unbounded read here would run off the end of a page.
static bool native_legacy_slot_is_refusing_stub(uintptr_t fn) {
    if(!fn || (fn>>48)!=0xFFFF)return false;
    const uint32_t *words=reinterpret_cast<const uint32_t *>(fn);
    for(unsigned j=0;j<24;++j) {
        uint32_t w=words[j];
        if((w&0xFFE00000)!=0x52800000) {
            if((w&0xFC000000)==0x94000000)return false;   // a BL: not a stub
            continue;
        }
        if(((w>>5)&0xFFFF)!=0x2C7 || (w&0x1F)!=0)continue; // MOV w0,#0x2c7
        for(unsigned m=j;m<j+8 && m<24;++m)if(words[m]==0xD65F03C0)return true;
        return false;
    }
    return false;
}

static void native_legacy_slot_trace_arm(void) {
    if(native_legacy_slot_trace_armed || !native_legacy_object)return;
    unsigned lift=0;PE_parse_boot_argn("offline-legacy-lift-refusals",&lift,sizeof(lift));
    // A single slot, for bisecting once the collective answer is in.  0 means
    // every refusing slot, which is the first experiment: addendum AH lifted ONE
    // refusal of twelve and nothing moved, so the question is whether they are
    // the gate together.
    //
    // Addendum ET: a RANGE as well, because a single slot forces a linear scan.
    // Lifting a contiguous range and asking whether a given selector succeeds is
    // a membership test -- the selector succeeds if and only if its own slot is
    // inside the range -- and that predicate is monotonic in the right
    // direction, so binary search applies.  Sixty candidates resolve in six
    // probes instead of sixty.  Lifting extra slots in the range cannot
    // un-refuse the selector under test; it can only make more of them succeed.
    unsigned only=0;PE_parse_boot_argn("offline-legacy-lift-slot",&only,sizeof(only));
    unsigned from=0,to=0;
    PE_parse_boot_argn("offline-legacy-lift-from",&from,sizeof(from));
    PE_parse_boot_argn("offline-legacy-lift-to",&to,sizeof(to));
    if(!lift)return;
    uintptr_t *vtable=*reinterpret_cast<uintptr_t **>(native_legacy_object);
    uintptr_t *copy=static_cast<uintptr_t *>(IOMalloc(kDisplaySlots*sizeof(uintptr_t)));
    if(!copy)return;
    memcpy(copy,vtable,kDisplaySlots*sizeof(uintptr_t));
    unsigned lifted=0;
    for(unsigned i=0;i<kDisplaySlots;++i) {
        // A kernel virtual address has its top 16 bits set.  The test is >> 48,
        // NOT >> 40: for a 48-bit address >> 40 leaves 24 bits and is false for
        // every valid pointer, which is the defect that made an earlier guard in
        // this project reject everything it was meant to check.
        native_legacy_slot_original[i]=copy[i];
        if(only && i!=only)continue;
        // Addendum ET: the range.  from/to are inclusive bounds; 0 means
        // unbounded on that side, so an unset pair leaves the behaviour exactly
        // as it was.
        if(from && i<from)continue;
        if(to && i>=to)continue;
        if(copy[i] && (copy[i]>>48)==0xFFFF && native_legacy_slot_is_refusing_stub(copy[i])) {
            copy[i]=(uintptr_t)native_legacy_slot_success;++lifted;
        }
    }
    *reinterpret_cast<uintptr_t **>(native_legacy_object)=copy;
    native_legacy_slot_trace_armed=true;
    native_legacy_slot_lifted_count=lifted;
    native_legacy_slot_armed_count=kDisplaySlots;
    // The copy is never freed and the object's vtable pointer is never restored.
    // Both are deliberate: the lift has to survive every call for the rest of
    // the boot, and this whole path is gated on a boot argument, so a board
    // booted without it is byte-identical to one that never had the code.  What
    // it does mean is that anything comparing the object's vtable pointer
    // against the original will see a different value, and the tracer this
    // replaces had exactly the same property.
}

void native_legacy_client_observed(IOReturn status,IOUserClient *client) {
    if(client && !status)interpose(client,native_legacy_client_pid);
    native_legacy_slot_trace_arm();
    native_legacy_client_status=status;native_legacy_client_object=(uintptr_t)client;
    if(client)strlcpy(native_legacy_client_class,client->getMetaClass()->getClassName(),sizeof(native_legacy_client_class));
    native_legacy_client_checkpoint();
}
__attribute__((noinline)) bool native_legacy_gop_mapping_observed(IOService *object,IOMemoryMap *mapping,uint32_t width,uint32_t height) {
    if((uintptr_t)object!=native_legacy_object)panic("NativeLegacyDisplay: wrong mapping owner");
    native_legacy_gop_map=(uintptr_t)mapping;native_legacy_gop_virtual=mapping->getVirtualAddress();
    native_legacy_gop_physical=mapping->getPhysicalAddress();native_legacy_gop_bytes=mapping->getLength();
    native_legacy_gop_width=width;native_legacy_gop_height=height;
    if(boot_progress_pulse_anchor && !diagnosticAnchorMapping) {
        PE_Video anchorVideo={};
        if(!PE_current_console(&anchorVideo) && anchorVideo.v_depth==32 &&
           anchorVideo.v_rowBytes<=65536 &&
           anchorVideo.v_width==width && anchorVideo.v_height==height &&
           anchorVideo.v_baseAddr==mapping->getPhysicalAddress() && !anchorVideo.v_rotate) {
            mapping->retain();
            if(boot_progress_pulse_anchor(mapping->getVirtualAddress(),mapping->getLength(),width,height,
                (unsigned)anchorVideo.v_rowBytes))diagnosticAnchorMapping=mapping;
            else mapping->release();
        }
    }
    native_legacy_gop_mapping_checkpoint();
    unsigned enabled=0;PE_parse_boot_argn("offline-legacy-unit-scale",&enabled,sizeof(enabled));
    if(!enabled)return true;
    native_legacy_unit_scale_calls=native_legacy_unit_scale_calls+1;native_legacy_unit_scale_status=1;
    unsigned phase=0;PE_parse_boot_argn("offline-legacy-phase",&phase,sizeof(phase));
    PE_Video video={};
    // The value is the UI scale: logical points = actual scanout pixels / scale (1..3). It changes
    // only the published display-scale; the scanout copy still moves one actual pixel per pixel.
    if(enabled<1 || enabled>3 || phase!=4 || unitScaleChosen || PE_current_console(&video) ||
       video.v_width!=width || video.v_height!=height || video.v_rotate ||
       mapping->getPhysicalAddress()!=video.v_baseAddr)return false;
    // Explicit offline UI policy: `enabled` actual GOP scanout pixels per logical
    // point in each direction (1 = native pixels). This is not physical pixel density,
    // panel timing, an Apple device identity or a synthesized mode list.
    unitScaleChosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
    if(!unitScaleChosen || !publishOrMatchWord(unitScaleChosen,"display-scale",enabled,unitScaleAdded) ||
       !publishOrMatchWord(unitScaleChosen,"display-rotation",video.v_rotate,unitRotationAdded)) {
        finishUnitScalePolicy(false);return false;
    }
    OSData *scale=OSDynamicCast(OSData,unitScaleChosen->getProperty("display-scale"));
    OSData *rotation=OSDynamicCast(OSData,unitScaleChosen->getProperty("display-rotation"));
    uint32_t actualScale=0,actualRotation=0;
    if(!scale || !rotation || scale->getLength()!=4 || rotation->getLength()!=4) {
        finishUnitScalePolicy(false);return false;
    }
    memcpy(&actualScale,scale->getBytesNoCopy(),4);memcpy(&actualRotation,rotation->getBytesNoCopy(),4);
    native_legacy_chosen_scale=actualScale;native_legacy_chosen_rotation=actualRotation;
    native_legacy_unit_scale_status=actualScale==enabled && actualRotation==video.v_rotate?0:2;
    if(native_legacy_unit_scale_status){finishUnitScalePolicy(false);return false;}
    return true;
}
__attribute__((noreturn,noinline)) void native_legacy_backend_guard(unsigned slot,void *object,void *caller) {
    native_legacy_guard_slot=slot;native_legacy_guard_object=(uintptr_t)object;native_legacy_guard_caller=(uintptr_t)caller;
    panic("NativeLegacyDisplay: leaf=%u caller=%llx stage=%u PM=%u swap=%u/%u copy=%08x bytes=%llu retire=%u handoff=%u/%u",
      slot,(uint64_t)(uintptr_t)caller,native_legacy_stage,native_legacy_pm_stage,
      native_legacy_swap_validation_result,native_legacy_swap_validation_reason,
      (unsigned)native_legacy_transfer_status,native_legacy_transfer_bytes,native_legacy_retirement_calls,
      native_legacy_prepare_reason,native_legacy_prepare_attempts);
}
void native_legacy_display_probe(void);
void native_legacy_display_probe(void) {
    // The registration path and optional original-userclient inspection both
    // reach this boot-thread entry. Run class constructors only once; a second
    // postModLoad would attempt to register the same metaclasses again.
    if(native_legacy_stage)return;
    native_legacy_stage=1;void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if(!handle){native_legacy_display_checkpoint();return;}
    native_legacy_display_static_init();native_gop_presenter_static_init();native_legacy_meta_status=OSMetaClass::postModLoad(handle);
    if(native_legacy_meta_status){native_legacy_display_checkpoint();return;}
    PE_Video video={};native_legacy_stage=2;
    if(PE_current_console(&video) || !video.v_baseAddr || !video.v_width || !video.v_height || video.v_depth!=32
        || video.v_rotate || video.v_width>16384 || video.v_height>16384 || video.v_rowBytes<video.v_width*4
        || video.v_rowBytes>UINT64_MAX/video.v_height){native_legacy_display_checkpoint();return;}
    IOService *display=native_legacy_display_allocate();native_legacy_object=(uintptr_t)display;
    if(!display){native_legacy_display_checkpoint();return;}
    native_legacy_object_size=display->getMetaClass()->getClassSize();
    native_legacy_parent_match=(display->metaCast("IOMobileFramebufferLegacy") && display->metaCast("IOMobileFramebuffer") && display->metaCast("IOService"));
    native_legacy_stage=3;native_legacy_init_result=display->init();
    unsigned phase=1;PE_parse_boot_argn("offline-legacy-phase",&phase,sizeof(phase));
    if(native_legacy_init_result && (phase==2 || phase==3 || phase==4)) {
        // IOSurface matches the real IOBSD resource, published by bsd_autoconf.
        // Retain this actual object until that normal lifecycle boundary.
        pendingLegacyDisplay=display;return;
    }
    native_legacy_stage=7;display->release();native_legacy_released=1;native_legacy_stage=8;
    native_legacy_display_checkpoint();
}
void native_legacy_display_late_probe(void);
extern void native_keystore_probe(void);
extern void native_hid_input_probe(void);
extern void native_virtio_input_probe(void);
extern void native_shared_window_probe(void);
extern void native_qemu_metal_publish(void);
extern void native_software_discovery_publish(void);
static void native_legacy_display_late_body(void *, wait_result_t);
void native_legacy_display_late_probe(void) {
    native_keystore_probe();
    native_hid_input_probe();
    native_virtio_input_probe();
    native_shared_window_probe();
    native_software_discovery_publish();
    native_qemu_metal_publish();
    if(!pendingLegacyDisplay)return;
    /* Everything below waits up to five seconds for the "IOCoreSurfaceRoot"
     * name, and that name is published by IOSurfaceRoot::start() -- which the
     * guest log shows happening at t=63.6 s, interleaved with the Data and
     * SystemOS mounts, both "requested by: init (pid 1)".  init runs only after
     * bsd_init() returns, so waiting here blocks start_init(), which blocks the
     * very mounts that publish the service.  Measured: mountroot completes at
     * t=49.6 s, so no point inside bsd_init() is late enough.
     *
     * So the wait runs on its own thread and this function returns at once.
     * The five probes above stay on the boot thread, where they were measured
     * to work; none of them is needed to mount the root volume. */
    thread_t thread=THREAD_NULL;
    if(kernel_thread_start(native_legacy_display_late_body,NULL,&thread)==KERN_SUCCESS) {
        thread_deallocate(thread);
    }
}
static void native_legacy_display_late_body(void *__unused arg,wait_result_t __unused wait) {
    IOService *display=pendingLegacyDisplay;if(!display){native_legacy_display_checkpoint();return;}pendingLegacyDisplay=nullptr;
    native_legacy_surface_status=OSKextLoadKextWithIdentifier("com.apple.iokit.IOSurface");
    /* Five seconds was calibrated for a wait that ran before mountroot.  It now
     * runs on its own thread, so a longer wait costs nothing but time: the
     * service the guest log times at t=63.6 s is 14 s after mountroot completes
     * at t=49.6 s.  The default is 120 s and it is a boot arg so a host can
     * shorten it without a rebuild. */
    unsigned surface_wait_ms=120000;
    PE_parse_boot_argn("offline-legacy-surface-wait-ms",&surface_wait_ms,sizeof(surface_wait_ms));
    native_legacy_surface_wait_ms=surface_wait_ms;
    OSDictionary *matching=IOService::nameMatching("IOCoreSurfaceRoot");
    IOService *surface=matching?IOService::waitForMatchingService(matching,(uint64_t)surface_wait_ms*1000000ULL):nullptr;
    native_legacy_surface_found=surface!=nullptr;OSSafeReleaseNULL(surface);OSSafeReleaseNULL(matching);
    PE_Video video={};
    if(!native_legacy_surface_status && native_legacy_surface_found && !PE_current_console(&video)
        && video.v_baseAddr && video.v_width && video.v_height && video.v_depth==32
        && !video.v_rotate && video.v_width<=16384 && video.v_height<=16384
        && video.v_rowBytes>=video.v_width*4 && video.v_rowBytes<=UINT64_MAX/video.v_height) {
        native_legacy_video_base=video.v_baseAddr;native_legacy_video_bytes=video.v_rowBytes*video.v_height;
        // A real firmware framebuffer resource, with addresses and extent
        // obtained from the actual GOP handoff. It has no invented compatible,
        // display list, panel timing, GPU/fence completion or vendor registers.
        native_legacy_stage=4;IOPlatformDevice *nub=new IOPlatformDevice;
        native_legacy_nub=(uintptr_t)nub;bool nubAttached=false,nubStarted=false,attached=false,started=false;
        IORegistryEntry *displayTreeParent=nullptr;bool dtAttached=false;
        IOService *parent=IOService::getResourceService();
        IODeviceMemory *memory=IODeviceMemory::withRange(video.v_baseAddr,video.v_rowBytes*video.v_height);
        OSArray *ranges=OSArray::withCapacity(1);
        if(nub && memory && ranges && parent && nub->init()) {
            ranges->setObject(memory);nub->setDeviceMemory(ranges);nub->setName("NativeGOPFramebuffer");
            nubAttached=nub->attach(parent);if(nubAttached)nubStarted=nub->start(parent);
            if(nubStarted) {
                unsigned phase=0;PE_parse_boot_argn("offline-legacy-phase",&phase,sizeof(phase));
                if(phase==4) {
                    // This is the actually implemented GOP display subsystem,
                    // attached to its real IOPlatformDevice memory resource.
                    // No Apple/vendor compatible or panel timing is supplied.
                    displayTreeParent=IORegistryEntry::fromPath("/arm-io",gIODTPlane);
                    const char type[]="display-subsystem";OSData *data=OSData::withBytes(type,sizeof(type));
                    if(displayTreeParent && data && nub->setProperty("device_type",data)) {
                        nub->setName("gop-display",gIODTPlane);dtAttached=nub->attachToParent(displayTreeParent,gIODTPlane);
                    }
                    OSSafeReleaseNULL(data);native_legacy_dt_attached=dtAttached;
                }
                if(phase==3) {
                    IOService *presenter=native_gop_presenter_create();bool presenterAttached=false;
                    if(presenter && presenter->init() && (presenterAttached=presenter->attach(nub)) && presenter->start(nub)) {
                        // Explicit own transport service only. The incomplete
                        // original framebuffer backend is never published.
                        activeGOPPresenter=presenter;activeGOPNub=nub;nub=nullptr;nubAttached=false;nubStarted=false;
                    } else {if(presenterAttached)presenter->detach(nub);OSSafeReleaseNULL(presenter);}
                } else {
                    SInt32 score=0;native_legacy_probe_match=(phase!=4 || dtAttached) && display->probe(nub,&score)==display;
                    if(native_legacy_probe_match)attached=display->attach(nub);
                    if(attached){native_legacy_stage=5;started=display->start(nub);native_legacy_start_result=started;}
                    if(started && phase==4) {
                        activeLegacyDisplay=display;activeGOPNub=nub;native_legacy_stage=10;
                        {
                            unsigned gpu=0;PE_parse_boot_argn("sheng-fb-gpu",&gpu,sizeof gpu);
                            if(gpu==1) {
                                IOService *handoff=native_gop_presenter_create();
                                // Not a child of the display subsystem's node: the original display code enumerates that node's children (int-075/077:
                                // SpringBoard found no display and crashed with the presenter attached there, registered or not).
                                IOService *home=IOService::getResourceService();
                                bool handoffAttached=false;
                                if(handoff && home)native_gop_presenter_set_source(handoff,nub);
                                if(handoff && home && handoff->init() && (handoffAttached=handoff->attach(home)) && handoff->start(home))gpuHandoffPresenter=handoff;
                                else {if(handoffAttached)handoff->detach(home);OSSafeReleaseNULL(handoff);}
                            }
                        }
                        finishUnitScalePolicy(true);
                        OSSafeReleaseNULL(displayTreeParent);
                        native_legacy_display_checkpoint();return;
                    }
                }
            }
        }
        native_legacy_stage=6;finishUnitScalePolicy(false);
        if(started)display->stop(nub);if(attached)display->detach(nub);
        if(dtAttached)nub->detachFromParent(displayTreeParent,gIODTPlane);
        if(nubStarted)nub->stop(parent);if(nubAttached)nub->detach(parent);
        OSSafeReleaseNULL(ranges);OSSafeReleaseNULL(memory);OSSafeReleaseNULL(nub);
        OSSafeReleaseNULL(displayTreeParent);
    }
    native_legacy_stage=7;display->release();native_legacy_released=1;native_legacy_stage=8;
    native_legacy_display_checkpoint();
}
}
