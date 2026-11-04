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
        if(selector<kSelectors){++stat->selector_calls[selector];if(status)++stat->selector_fails[selector];stat->selector_status[selector]=(uint32_t)status;}
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
enum {kDisplaySlots=358};
// The five the host reads by symbol name over QMP, and they carry THREE
// attributes that are each load-bearing and none of which is decoration.
//
// extern "C" -- a file-local symbol is not guaranteed to be in the image's
// table the way a global is, and the name must be exactly as spelled here.
//
// section("__DATA,unipad_slots") -- NOT __DATA,__common.  Run 41915052472
// failed its second pass at the kernel LINK, not the compile, with
// "ld: symbol(s) not found for architecture arm64" naming exactly these
// symbols and native_legacy_slot_common, every reference from inside the very
// object that defines them.  The reason is that a symbol named from inline asm
// is emitted as an UNDEFINED reference no matter what -- the assembler cannot
// know the reference is intra-unit, so it refuses to resolve it -- and ld will
// only satisfy such a reference against a real, global definition.  A
// TENTATIVE definition in __common is not one: commons are merged in a later
// pass, by which time the undefined reference has already been reported.  An
// explicit section makes the definition real.  Reproduced locally before the
// pin moved, with the same target and mode the runner uses.
//
// An explicit zero initializer -- because a section-attributed declaration
// WITHOUT one is treated as a mere declaration and clang emits nothing at all,
// which is a silent hole rather than a build error.
//
// native_legacy_slot_common below is global for the same reason: a `b` from
// inline asm creates an undefined reference, and a file-local symbol cannot
// satisfy one.
extern "C" __attribute__((section("__DATA,unipad_slots"))) uint32_t native_legacy_slot_calls[kDisplaySlots]={};
extern "C" __attribute__((section("__DATA,unipad_slots"))) uintptr_t native_legacy_slot_original[kDisplaySlots]={};
extern "C" __attribute__((section("__DATA,unipad_slots"))) uint32_t native_legacy_slot_lifted[kDisplaySlots]={};
extern "C" __attribute__((section("__DATA,unipad_slots"))) uint32_t native_legacy_slot_armed_count=0;
extern "C" __attribute__((section("__DATA,unipad_slots"))) uint32_t native_legacy_slot_lifted_count=0;
static bool native_legacy_slot_trace_armed;

// GLOBAL, and that is not a style choice: the per-slot stubs reach it with a
// `b` from inline asm, which emits an undefined reference, and a file-local
// symbol cannot satisfy one.  Run 41915052472's link named this symbol too.
extern "C" __attribute__((naked,used)) void native_legacy_slot_common(void) {
    // x16 = the slot index, set by the per-slot stub below.
    //
    // GOT-relative, and this is not a stylistic choice: the Apple kext
    // relocation model REFUSES the ELF form.  `adrp x17, sym; add x17, x17,
    // :lo12:sym` assembles for Linux and is rejected here with "ADR/ADRP
    // relocations must be GOT relative" and "unknown AArch64 fixup kind!",
    // which is what run 41906787039 failed on -- six errors, all of them this
    // instruction, and the pre-flight that was supposed to catch it was green
    // because it had been assembled for the wrong target.  `sym@GOTPAGE` /
    // `sym@GOTPAGEOFF` is what the kext toolchain accepts; it costs one extra
    // load per array, which on a path that runs a few hundred times a boot is
    // nothing.
    //
    // The label is NUMERIC rather than .L-named for the same class of reason:
    // the Apple integrated assembler refuses a .L name as the target of a
    // conditional branch ("conditional branch requires assembler-local
    // label").  GNU as accepts both, so the numeric form is the portable one.
    //
    // The lift is three more instructions at the end, and it is why this thunk
    // is not a pure tail call any more.  After counting, it reads this slot's
    // lift flag; if the flag is set the method is answered kIOReturnSuccess
    // here and the original is never entered, otherwise control branches to it
    // exactly as before.  The flag is only ever set for a slot whose function
    // is a refusing stub, and a refusing stub writes nothing -- so the lift
    // changes the return value and nothing else.  See the classifier below.
    __asm__ volatile(
        "adrp x17, native_legacy_slot_original@GOTPAGE\n\t"
        "ldr  x17, [x17, native_legacy_slot_original@GOTPAGEOFF]\n\t"
        "ldr  x17, [x17, x16, lsl #3]\n\t"          // x17 = the original
        "adrp x9, native_legacy_slot_calls@GOTPAGE\n\t"
        "ldr  x9, [x9, native_legacy_slot_calls@GOTPAGEOFF]\n\t"
        "ldr  w10, [x9, x16, lsl #2]\n\t"
        "add  w10, w10, #1\n\t"
        "str  w10, [x9, x16, lsl #2]\n\t"
        "adrp x9, native_legacy_slot_lifted@GOTPAGE\n\t"
        "ldr  x9, [x9, native_legacy_slot_lifted@GOTPAGEOFF]\n\t"
        "ldr  w10, [x9, x16, lsl #2]\n\t"
        "cbz  w10, 1f\n\t"
        "mov  w0, #0\n\t"                           // kIOReturnSuccess
        "ret\n"
        "1:\n\t"
        "br   x17\n\t");
}

// Addendum AC's classifier, in the kernel, so the lift needs no host in the
// loop.  A slot refuses only if its function is a STUB: `MOV w0,#0x2c7` with
// nothing CALLED before it and a RET soon after.  The "nothing called" clause
// is what separates a stub from a real implementation with a dead tail -- the
// LEAF() guards at 306, 307 and 337 are `MOV w0,#<slot>; BL
// native_legacy_backend_guard` with a noreturn panic, and the compiler still
// emits a dead `MOV w0,#0x2c7` after them.  Matching those would answer a
// guard's call with success instead of panicking, which is not what an
// experiment should do.
//
// BOUNDED to the first 24 words (96 bytes), which is the same window the host
// classifier read.  A stub is shorter than that; the bound is recorded because
// an unbounded read here would run off the end of a page.
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

// One stub per slot.  The slot number is stringified rather than passed as an
// operand: a naked function's body is a single asm statement and immediates
// avoid any register allocation the attribute forbids.
#define NATIVE_LEGACY_SLOT_THUNK(n) \
    __attribute__((naked,used)) static void native_legacy_slot_thunk_##n(void) { \
        __asm__ volatile("movz x16, #" #n "\n\tb native_legacy_slot_common"); \
    }

NATIVE_LEGACY_SLOT_THUNK(0)
NATIVE_LEGACY_SLOT_THUNK(1)
NATIVE_LEGACY_SLOT_THUNK(2)
NATIVE_LEGACY_SLOT_THUNK(3)
NATIVE_LEGACY_SLOT_THUNK(4)
NATIVE_LEGACY_SLOT_THUNK(5)
NATIVE_LEGACY_SLOT_THUNK(6)
NATIVE_LEGACY_SLOT_THUNK(7)
NATIVE_LEGACY_SLOT_THUNK(8)
NATIVE_LEGACY_SLOT_THUNK(9)
NATIVE_LEGACY_SLOT_THUNK(10)
NATIVE_LEGACY_SLOT_THUNK(11)
NATIVE_LEGACY_SLOT_THUNK(12)
NATIVE_LEGACY_SLOT_THUNK(13)
NATIVE_LEGACY_SLOT_THUNK(14)
NATIVE_LEGACY_SLOT_THUNK(15)
NATIVE_LEGACY_SLOT_THUNK(16)
NATIVE_LEGACY_SLOT_THUNK(17)
NATIVE_LEGACY_SLOT_THUNK(18)
NATIVE_LEGACY_SLOT_THUNK(19)
NATIVE_LEGACY_SLOT_THUNK(20)
NATIVE_LEGACY_SLOT_THUNK(21)
NATIVE_LEGACY_SLOT_THUNK(22)
NATIVE_LEGACY_SLOT_THUNK(23)
NATIVE_LEGACY_SLOT_THUNK(24)
NATIVE_LEGACY_SLOT_THUNK(25)
NATIVE_LEGACY_SLOT_THUNK(26)
NATIVE_LEGACY_SLOT_THUNK(27)
NATIVE_LEGACY_SLOT_THUNK(28)
NATIVE_LEGACY_SLOT_THUNK(29)
NATIVE_LEGACY_SLOT_THUNK(30)
NATIVE_LEGACY_SLOT_THUNK(31)
NATIVE_LEGACY_SLOT_THUNK(32)
NATIVE_LEGACY_SLOT_THUNK(33)
NATIVE_LEGACY_SLOT_THUNK(34)
NATIVE_LEGACY_SLOT_THUNK(35)
NATIVE_LEGACY_SLOT_THUNK(36)
NATIVE_LEGACY_SLOT_THUNK(37)
NATIVE_LEGACY_SLOT_THUNK(38)
NATIVE_LEGACY_SLOT_THUNK(39)
NATIVE_LEGACY_SLOT_THUNK(40)
NATIVE_LEGACY_SLOT_THUNK(41)
NATIVE_LEGACY_SLOT_THUNK(42)
NATIVE_LEGACY_SLOT_THUNK(43)
NATIVE_LEGACY_SLOT_THUNK(44)
NATIVE_LEGACY_SLOT_THUNK(45)
NATIVE_LEGACY_SLOT_THUNK(46)
NATIVE_LEGACY_SLOT_THUNK(47)
NATIVE_LEGACY_SLOT_THUNK(48)
NATIVE_LEGACY_SLOT_THUNK(49)
NATIVE_LEGACY_SLOT_THUNK(50)
NATIVE_LEGACY_SLOT_THUNK(51)
NATIVE_LEGACY_SLOT_THUNK(52)
NATIVE_LEGACY_SLOT_THUNK(53)
NATIVE_LEGACY_SLOT_THUNK(54)
NATIVE_LEGACY_SLOT_THUNK(55)
NATIVE_LEGACY_SLOT_THUNK(56)
NATIVE_LEGACY_SLOT_THUNK(57)
NATIVE_LEGACY_SLOT_THUNK(58)
NATIVE_LEGACY_SLOT_THUNK(59)
NATIVE_LEGACY_SLOT_THUNK(60)
NATIVE_LEGACY_SLOT_THUNK(61)
NATIVE_LEGACY_SLOT_THUNK(62)
NATIVE_LEGACY_SLOT_THUNK(63)
NATIVE_LEGACY_SLOT_THUNK(64)
NATIVE_LEGACY_SLOT_THUNK(65)
NATIVE_LEGACY_SLOT_THUNK(66)
NATIVE_LEGACY_SLOT_THUNK(67)
NATIVE_LEGACY_SLOT_THUNK(68)
NATIVE_LEGACY_SLOT_THUNK(69)
NATIVE_LEGACY_SLOT_THUNK(70)
NATIVE_LEGACY_SLOT_THUNK(71)
NATIVE_LEGACY_SLOT_THUNK(72)
NATIVE_LEGACY_SLOT_THUNK(73)
NATIVE_LEGACY_SLOT_THUNK(74)
NATIVE_LEGACY_SLOT_THUNK(75)
NATIVE_LEGACY_SLOT_THUNK(76)
NATIVE_LEGACY_SLOT_THUNK(77)
NATIVE_LEGACY_SLOT_THUNK(78)
NATIVE_LEGACY_SLOT_THUNK(79)
NATIVE_LEGACY_SLOT_THUNK(80)
NATIVE_LEGACY_SLOT_THUNK(81)
NATIVE_LEGACY_SLOT_THUNK(82)
NATIVE_LEGACY_SLOT_THUNK(83)
NATIVE_LEGACY_SLOT_THUNK(84)
NATIVE_LEGACY_SLOT_THUNK(85)
NATIVE_LEGACY_SLOT_THUNK(86)
NATIVE_LEGACY_SLOT_THUNK(87)
NATIVE_LEGACY_SLOT_THUNK(88)
NATIVE_LEGACY_SLOT_THUNK(89)
NATIVE_LEGACY_SLOT_THUNK(90)
NATIVE_LEGACY_SLOT_THUNK(91)
NATIVE_LEGACY_SLOT_THUNK(92)
NATIVE_LEGACY_SLOT_THUNK(93)
NATIVE_LEGACY_SLOT_THUNK(94)
NATIVE_LEGACY_SLOT_THUNK(95)
NATIVE_LEGACY_SLOT_THUNK(96)
NATIVE_LEGACY_SLOT_THUNK(97)
NATIVE_LEGACY_SLOT_THUNK(98)
NATIVE_LEGACY_SLOT_THUNK(99)
NATIVE_LEGACY_SLOT_THUNK(100)
NATIVE_LEGACY_SLOT_THUNK(101)
NATIVE_LEGACY_SLOT_THUNK(102)
NATIVE_LEGACY_SLOT_THUNK(103)
NATIVE_LEGACY_SLOT_THUNK(104)
NATIVE_LEGACY_SLOT_THUNK(105)
NATIVE_LEGACY_SLOT_THUNK(106)
NATIVE_LEGACY_SLOT_THUNK(107)
NATIVE_LEGACY_SLOT_THUNK(108)
NATIVE_LEGACY_SLOT_THUNK(109)
NATIVE_LEGACY_SLOT_THUNK(110)
NATIVE_LEGACY_SLOT_THUNK(111)
NATIVE_LEGACY_SLOT_THUNK(112)
NATIVE_LEGACY_SLOT_THUNK(113)
NATIVE_LEGACY_SLOT_THUNK(114)
NATIVE_LEGACY_SLOT_THUNK(115)
NATIVE_LEGACY_SLOT_THUNK(116)
NATIVE_LEGACY_SLOT_THUNK(117)
NATIVE_LEGACY_SLOT_THUNK(118)
NATIVE_LEGACY_SLOT_THUNK(119)
NATIVE_LEGACY_SLOT_THUNK(120)
NATIVE_LEGACY_SLOT_THUNK(121)
NATIVE_LEGACY_SLOT_THUNK(122)
NATIVE_LEGACY_SLOT_THUNK(123)
NATIVE_LEGACY_SLOT_THUNK(124)
NATIVE_LEGACY_SLOT_THUNK(125)
NATIVE_LEGACY_SLOT_THUNK(126)
NATIVE_LEGACY_SLOT_THUNK(127)
NATIVE_LEGACY_SLOT_THUNK(128)
NATIVE_LEGACY_SLOT_THUNK(129)
NATIVE_LEGACY_SLOT_THUNK(130)
NATIVE_LEGACY_SLOT_THUNK(131)
NATIVE_LEGACY_SLOT_THUNK(132)
NATIVE_LEGACY_SLOT_THUNK(133)
NATIVE_LEGACY_SLOT_THUNK(134)
NATIVE_LEGACY_SLOT_THUNK(135)
NATIVE_LEGACY_SLOT_THUNK(136)
NATIVE_LEGACY_SLOT_THUNK(137)
NATIVE_LEGACY_SLOT_THUNK(138)
NATIVE_LEGACY_SLOT_THUNK(139)
NATIVE_LEGACY_SLOT_THUNK(140)
NATIVE_LEGACY_SLOT_THUNK(141)
NATIVE_LEGACY_SLOT_THUNK(142)
NATIVE_LEGACY_SLOT_THUNK(143)
NATIVE_LEGACY_SLOT_THUNK(144)
NATIVE_LEGACY_SLOT_THUNK(145)
NATIVE_LEGACY_SLOT_THUNK(146)
NATIVE_LEGACY_SLOT_THUNK(147)
NATIVE_LEGACY_SLOT_THUNK(148)
NATIVE_LEGACY_SLOT_THUNK(149)
NATIVE_LEGACY_SLOT_THUNK(150)
NATIVE_LEGACY_SLOT_THUNK(151)
NATIVE_LEGACY_SLOT_THUNK(152)
NATIVE_LEGACY_SLOT_THUNK(153)
NATIVE_LEGACY_SLOT_THUNK(154)
NATIVE_LEGACY_SLOT_THUNK(155)
NATIVE_LEGACY_SLOT_THUNK(156)
NATIVE_LEGACY_SLOT_THUNK(157)
NATIVE_LEGACY_SLOT_THUNK(158)
NATIVE_LEGACY_SLOT_THUNK(159)
NATIVE_LEGACY_SLOT_THUNK(160)
NATIVE_LEGACY_SLOT_THUNK(161)
NATIVE_LEGACY_SLOT_THUNK(162)
NATIVE_LEGACY_SLOT_THUNK(163)
NATIVE_LEGACY_SLOT_THUNK(164)
NATIVE_LEGACY_SLOT_THUNK(165)
NATIVE_LEGACY_SLOT_THUNK(166)
NATIVE_LEGACY_SLOT_THUNK(167)
NATIVE_LEGACY_SLOT_THUNK(168)
NATIVE_LEGACY_SLOT_THUNK(169)
NATIVE_LEGACY_SLOT_THUNK(170)
NATIVE_LEGACY_SLOT_THUNK(171)
NATIVE_LEGACY_SLOT_THUNK(172)
NATIVE_LEGACY_SLOT_THUNK(173)
NATIVE_LEGACY_SLOT_THUNK(174)
NATIVE_LEGACY_SLOT_THUNK(175)
NATIVE_LEGACY_SLOT_THUNK(176)
NATIVE_LEGACY_SLOT_THUNK(177)
NATIVE_LEGACY_SLOT_THUNK(178)
NATIVE_LEGACY_SLOT_THUNK(179)
NATIVE_LEGACY_SLOT_THUNK(180)
NATIVE_LEGACY_SLOT_THUNK(181)
NATIVE_LEGACY_SLOT_THUNK(182)
NATIVE_LEGACY_SLOT_THUNK(183)
NATIVE_LEGACY_SLOT_THUNK(184)
NATIVE_LEGACY_SLOT_THUNK(185)
NATIVE_LEGACY_SLOT_THUNK(186)
NATIVE_LEGACY_SLOT_THUNK(187)
NATIVE_LEGACY_SLOT_THUNK(188)
NATIVE_LEGACY_SLOT_THUNK(189)
NATIVE_LEGACY_SLOT_THUNK(190)
NATIVE_LEGACY_SLOT_THUNK(191)
NATIVE_LEGACY_SLOT_THUNK(192)
NATIVE_LEGACY_SLOT_THUNK(193)
NATIVE_LEGACY_SLOT_THUNK(194)
NATIVE_LEGACY_SLOT_THUNK(195)
NATIVE_LEGACY_SLOT_THUNK(196)
NATIVE_LEGACY_SLOT_THUNK(197)
NATIVE_LEGACY_SLOT_THUNK(198)
NATIVE_LEGACY_SLOT_THUNK(199)
NATIVE_LEGACY_SLOT_THUNK(200)
NATIVE_LEGACY_SLOT_THUNK(201)
NATIVE_LEGACY_SLOT_THUNK(202)
NATIVE_LEGACY_SLOT_THUNK(203)
NATIVE_LEGACY_SLOT_THUNK(204)
NATIVE_LEGACY_SLOT_THUNK(205)
NATIVE_LEGACY_SLOT_THUNK(206)
NATIVE_LEGACY_SLOT_THUNK(207)
NATIVE_LEGACY_SLOT_THUNK(208)
NATIVE_LEGACY_SLOT_THUNK(209)
NATIVE_LEGACY_SLOT_THUNK(210)
NATIVE_LEGACY_SLOT_THUNK(211)
NATIVE_LEGACY_SLOT_THUNK(212)
NATIVE_LEGACY_SLOT_THUNK(213)
NATIVE_LEGACY_SLOT_THUNK(214)
NATIVE_LEGACY_SLOT_THUNK(215)
NATIVE_LEGACY_SLOT_THUNK(216)
NATIVE_LEGACY_SLOT_THUNK(217)
NATIVE_LEGACY_SLOT_THUNK(218)
NATIVE_LEGACY_SLOT_THUNK(219)
NATIVE_LEGACY_SLOT_THUNK(220)
NATIVE_LEGACY_SLOT_THUNK(221)
NATIVE_LEGACY_SLOT_THUNK(222)
NATIVE_LEGACY_SLOT_THUNK(223)
NATIVE_LEGACY_SLOT_THUNK(224)
NATIVE_LEGACY_SLOT_THUNK(225)
NATIVE_LEGACY_SLOT_THUNK(226)
NATIVE_LEGACY_SLOT_THUNK(227)
NATIVE_LEGACY_SLOT_THUNK(228)
NATIVE_LEGACY_SLOT_THUNK(229)
NATIVE_LEGACY_SLOT_THUNK(230)
NATIVE_LEGACY_SLOT_THUNK(231)
NATIVE_LEGACY_SLOT_THUNK(232)
NATIVE_LEGACY_SLOT_THUNK(233)
NATIVE_LEGACY_SLOT_THUNK(234)
NATIVE_LEGACY_SLOT_THUNK(235)
NATIVE_LEGACY_SLOT_THUNK(236)
NATIVE_LEGACY_SLOT_THUNK(237)
NATIVE_LEGACY_SLOT_THUNK(238)
NATIVE_LEGACY_SLOT_THUNK(239)
NATIVE_LEGACY_SLOT_THUNK(240)
NATIVE_LEGACY_SLOT_THUNK(241)
NATIVE_LEGACY_SLOT_THUNK(242)
NATIVE_LEGACY_SLOT_THUNK(243)
NATIVE_LEGACY_SLOT_THUNK(244)
NATIVE_LEGACY_SLOT_THUNK(245)
NATIVE_LEGACY_SLOT_THUNK(246)
NATIVE_LEGACY_SLOT_THUNK(247)
NATIVE_LEGACY_SLOT_THUNK(248)
NATIVE_LEGACY_SLOT_THUNK(249)
NATIVE_LEGACY_SLOT_THUNK(250)
NATIVE_LEGACY_SLOT_THUNK(251)
NATIVE_LEGACY_SLOT_THUNK(252)
NATIVE_LEGACY_SLOT_THUNK(253)
NATIVE_LEGACY_SLOT_THUNK(254)
NATIVE_LEGACY_SLOT_THUNK(255)
NATIVE_LEGACY_SLOT_THUNK(256)
NATIVE_LEGACY_SLOT_THUNK(257)
NATIVE_LEGACY_SLOT_THUNK(258)
NATIVE_LEGACY_SLOT_THUNK(259)
NATIVE_LEGACY_SLOT_THUNK(260)
NATIVE_LEGACY_SLOT_THUNK(261)
NATIVE_LEGACY_SLOT_THUNK(262)
NATIVE_LEGACY_SLOT_THUNK(263)
NATIVE_LEGACY_SLOT_THUNK(264)
NATIVE_LEGACY_SLOT_THUNK(265)
NATIVE_LEGACY_SLOT_THUNK(266)
NATIVE_LEGACY_SLOT_THUNK(267)
NATIVE_LEGACY_SLOT_THUNK(268)
NATIVE_LEGACY_SLOT_THUNK(269)
NATIVE_LEGACY_SLOT_THUNK(270)
NATIVE_LEGACY_SLOT_THUNK(271)
NATIVE_LEGACY_SLOT_THUNK(272)
NATIVE_LEGACY_SLOT_THUNK(273)
NATIVE_LEGACY_SLOT_THUNK(274)
NATIVE_LEGACY_SLOT_THUNK(275)
NATIVE_LEGACY_SLOT_THUNK(276)
NATIVE_LEGACY_SLOT_THUNK(277)
NATIVE_LEGACY_SLOT_THUNK(278)
NATIVE_LEGACY_SLOT_THUNK(279)
NATIVE_LEGACY_SLOT_THUNK(280)
NATIVE_LEGACY_SLOT_THUNK(281)
NATIVE_LEGACY_SLOT_THUNK(282)
NATIVE_LEGACY_SLOT_THUNK(283)
NATIVE_LEGACY_SLOT_THUNK(284)
NATIVE_LEGACY_SLOT_THUNK(285)
NATIVE_LEGACY_SLOT_THUNK(286)
NATIVE_LEGACY_SLOT_THUNK(287)
NATIVE_LEGACY_SLOT_THUNK(288)
NATIVE_LEGACY_SLOT_THUNK(289)
NATIVE_LEGACY_SLOT_THUNK(290)
NATIVE_LEGACY_SLOT_THUNK(291)
NATIVE_LEGACY_SLOT_THUNK(292)
NATIVE_LEGACY_SLOT_THUNK(293)
NATIVE_LEGACY_SLOT_THUNK(294)
NATIVE_LEGACY_SLOT_THUNK(295)
NATIVE_LEGACY_SLOT_THUNK(296)
NATIVE_LEGACY_SLOT_THUNK(297)
NATIVE_LEGACY_SLOT_THUNK(298)
NATIVE_LEGACY_SLOT_THUNK(299)
NATIVE_LEGACY_SLOT_THUNK(300)
NATIVE_LEGACY_SLOT_THUNK(301)
NATIVE_LEGACY_SLOT_THUNK(302)
NATIVE_LEGACY_SLOT_THUNK(303)
NATIVE_LEGACY_SLOT_THUNK(304)
NATIVE_LEGACY_SLOT_THUNK(305)
NATIVE_LEGACY_SLOT_THUNK(306)
NATIVE_LEGACY_SLOT_THUNK(307)
NATIVE_LEGACY_SLOT_THUNK(308)
NATIVE_LEGACY_SLOT_THUNK(309)
NATIVE_LEGACY_SLOT_THUNK(310)
NATIVE_LEGACY_SLOT_THUNK(311)
NATIVE_LEGACY_SLOT_THUNK(312)
NATIVE_LEGACY_SLOT_THUNK(313)
NATIVE_LEGACY_SLOT_THUNK(314)
NATIVE_LEGACY_SLOT_THUNK(315)
NATIVE_LEGACY_SLOT_THUNK(316)
NATIVE_LEGACY_SLOT_THUNK(317)
NATIVE_LEGACY_SLOT_THUNK(318)
NATIVE_LEGACY_SLOT_THUNK(319)
NATIVE_LEGACY_SLOT_THUNK(320)
NATIVE_LEGACY_SLOT_THUNK(321)
NATIVE_LEGACY_SLOT_THUNK(322)
NATIVE_LEGACY_SLOT_THUNK(323)
NATIVE_LEGACY_SLOT_THUNK(324)
NATIVE_LEGACY_SLOT_THUNK(325)
NATIVE_LEGACY_SLOT_THUNK(326)
NATIVE_LEGACY_SLOT_THUNK(327)
NATIVE_LEGACY_SLOT_THUNK(328)
NATIVE_LEGACY_SLOT_THUNK(329)
NATIVE_LEGACY_SLOT_THUNK(330)
NATIVE_LEGACY_SLOT_THUNK(331)
NATIVE_LEGACY_SLOT_THUNK(332)
NATIVE_LEGACY_SLOT_THUNK(333)
NATIVE_LEGACY_SLOT_THUNK(334)
NATIVE_LEGACY_SLOT_THUNK(335)
NATIVE_LEGACY_SLOT_THUNK(336)
NATIVE_LEGACY_SLOT_THUNK(337)
NATIVE_LEGACY_SLOT_THUNK(338)
NATIVE_LEGACY_SLOT_THUNK(339)
NATIVE_LEGACY_SLOT_THUNK(340)
NATIVE_LEGACY_SLOT_THUNK(341)
NATIVE_LEGACY_SLOT_THUNK(342)
NATIVE_LEGACY_SLOT_THUNK(343)
NATIVE_LEGACY_SLOT_THUNK(344)
NATIVE_LEGACY_SLOT_THUNK(345)
NATIVE_LEGACY_SLOT_THUNK(346)
NATIVE_LEGACY_SLOT_THUNK(347)
NATIVE_LEGACY_SLOT_THUNK(348)
NATIVE_LEGACY_SLOT_THUNK(349)
NATIVE_LEGACY_SLOT_THUNK(350)
NATIVE_LEGACY_SLOT_THUNK(351)
NATIVE_LEGACY_SLOT_THUNK(352)
NATIVE_LEGACY_SLOT_THUNK(353)
NATIVE_LEGACY_SLOT_THUNK(354)
NATIVE_LEGACY_SLOT_THUNK(355)
NATIVE_LEGACY_SLOT_THUNK(356)
NATIVE_LEGACY_SLOT_THUNK(357)

static uintptr_t native_legacy_slot_thunks_init[kDisplaySlots] = {(uintptr_t)&native_legacy_slot_thunk_0,(uintptr_t)&native_legacy_slot_thunk_1,(uintptr_t)&native_legacy_slot_thunk_2,(uintptr_t)&native_legacy_slot_thunk_3,(uintptr_t)&native_legacy_slot_thunk_4,(uintptr_t)&native_legacy_slot_thunk_5,(uintptr_t)&native_legacy_slot_thunk_6,(uintptr_t)&native_legacy_slot_thunk_7,(uintptr_t)&native_legacy_slot_thunk_8,(uintptr_t)&native_legacy_slot_thunk_9,(uintptr_t)&native_legacy_slot_thunk_10,(uintptr_t)&native_legacy_slot_thunk_11,(uintptr_t)&native_legacy_slot_thunk_12,(uintptr_t)&native_legacy_slot_thunk_13,(uintptr_t)&native_legacy_slot_thunk_14,(uintptr_t)&native_legacy_slot_thunk_15,(uintptr_t)&native_legacy_slot_thunk_16,(uintptr_t)&native_legacy_slot_thunk_17,(uintptr_t)&native_legacy_slot_thunk_18,(uintptr_t)&native_legacy_slot_thunk_19,(uintptr_t)&native_legacy_slot_thunk_20,(uintptr_t)&native_legacy_slot_thunk_21,(uintptr_t)&native_legacy_slot_thunk_22,(uintptr_t)&native_legacy_slot_thunk_23,(uintptr_t)&native_legacy_slot_thunk_24,(uintptr_t)&native_legacy_slot_thunk_25,(uintptr_t)&native_legacy_slot_thunk_26,(uintptr_t)&native_legacy_slot_thunk_27,(uintptr_t)&native_legacy_slot_thunk_28,(uintptr_t)&native_legacy_slot_thunk_29,(uintptr_t)&native_legacy_slot_thunk_30,(uintptr_t)&native_legacy_slot_thunk_31,(uintptr_t)&native_legacy_slot_thunk_32,(uintptr_t)&native_legacy_slot_thunk_33,(uintptr_t)&native_legacy_slot_thunk_34,(uintptr_t)&native_legacy_slot_thunk_35,(uintptr_t)&native_legacy_slot_thunk_36,(uintptr_t)&native_legacy_slot_thunk_37,(uintptr_t)&native_legacy_slot_thunk_38,(uintptr_t)&native_legacy_slot_thunk_39,(uintptr_t)&native_legacy_slot_thunk_40,(uintptr_t)&native_legacy_slot_thunk_41,(uintptr_t)&native_legacy_slot_thunk_42,(uintptr_t)&native_legacy_slot_thunk_43,(uintptr_t)&native_legacy_slot_thunk_44,(uintptr_t)&native_legacy_slot_thunk_45,(uintptr_t)&native_legacy_slot_thunk_46,(uintptr_t)&native_legacy_slot_thunk_47,(uintptr_t)&native_legacy_slot_thunk_48,(uintptr_t)&native_legacy_slot_thunk_49,(uintptr_t)&native_legacy_slot_thunk_50,(uintptr_t)&native_legacy_slot_thunk_51,(uintptr_t)&native_legacy_slot_thunk_52,(uintptr_t)&native_legacy_slot_thunk_53,(uintptr_t)&native_legacy_slot_thunk_54,(uintptr_t)&native_legacy_slot_thunk_55,(uintptr_t)&native_legacy_slot_thunk_56,(uintptr_t)&native_legacy_slot_thunk_57,(uintptr_t)&native_legacy_slot_thunk_58,(uintptr_t)&native_legacy_slot_thunk_59,(uintptr_t)&native_legacy_slot_thunk_60,(uintptr_t)&native_legacy_slot_thunk_61,(uintptr_t)&native_legacy_slot_thunk_62,(uintptr_t)&native_legacy_slot_thunk_63,(uintptr_t)&native_legacy_slot_thunk_64,(uintptr_t)&native_legacy_slot_thunk_65,(uintptr_t)&native_legacy_slot_thunk_66,(uintptr_t)&native_legacy_slot_thunk_67,(uintptr_t)&native_legacy_slot_thunk_68,(uintptr_t)&native_legacy_slot_thunk_69,(uintptr_t)&native_legacy_slot_thunk_70,(uintptr_t)&native_legacy_slot_thunk_71,(uintptr_t)&native_legacy_slot_thunk_72,(uintptr_t)&native_legacy_slot_thunk_73,(uintptr_t)&native_legacy_slot_thunk_74,(uintptr_t)&native_legacy_slot_thunk_75,(uintptr_t)&native_legacy_slot_thunk_76,(uintptr_t)&native_legacy_slot_thunk_77,(uintptr_t)&native_legacy_slot_thunk_78,(uintptr_t)&native_legacy_slot_thunk_79,(uintptr_t)&native_legacy_slot_thunk_80,(uintptr_t)&native_legacy_slot_thunk_81,(uintptr_t)&native_legacy_slot_thunk_82,(uintptr_t)&native_legacy_slot_thunk_83,(uintptr_t)&native_legacy_slot_thunk_84,(uintptr_t)&native_legacy_slot_thunk_85,(uintptr_t)&native_legacy_slot_thunk_86,(uintptr_t)&native_legacy_slot_thunk_87,(uintptr_t)&native_legacy_slot_thunk_88,(uintptr_t)&native_legacy_slot_thunk_89,(uintptr_t)&native_legacy_slot_thunk_90,(uintptr_t)&native_legacy_slot_thunk_91,(uintptr_t)&native_legacy_slot_thunk_92,(uintptr_t)&native_legacy_slot_thunk_93,(uintptr_t)&native_legacy_slot_thunk_94,(uintptr_t)&native_legacy_slot_thunk_95,(uintptr_t)&native_legacy_slot_thunk_96,(uintptr_t)&native_legacy_slot_thunk_97,(uintptr_t)&native_legacy_slot_thunk_98,(uintptr_t)&native_legacy_slot_thunk_99,(uintptr_t)&native_legacy_slot_thunk_100,(uintptr_t)&native_legacy_slot_thunk_101,(uintptr_t)&native_legacy_slot_thunk_102,(uintptr_t)&native_legacy_slot_thunk_103,(uintptr_t)&native_legacy_slot_thunk_104,(uintptr_t)&native_legacy_slot_thunk_105,(uintptr_t)&native_legacy_slot_thunk_106,(uintptr_t)&native_legacy_slot_thunk_107,(uintptr_t)&native_legacy_slot_thunk_108,(uintptr_t)&native_legacy_slot_thunk_109,(uintptr_t)&native_legacy_slot_thunk_110,(uintptr_t)&native_legacy_slot_thunk_111,(uintptr_t)&native_legacy_slot_thunk_112,(uintptr_t)&native_legacy_slot_thunk_113,(uintptr_t)&native_legacy_slot_thunk_114,(uintptr_t)&native_legacy_slot_thunk_115,(uintptr_t)&native_legacy_slot_thunk_116,(uintptr_t)&native_legacy_slot_thunk_117,(uintptr_t)&native_legacy_slot_thunk_118,(uintptr_t)&native_legacy_slot_thunk_119,(uintptr_t)&native_legacy_slot_thunk_120,(uintptr_t)&native_legacy_slot_thunk_121,(uintptr_t)&native_legacy_slot_thunk_122,(uintptr_t)&native_legacy_slot_thunk_123,(uintptr_t)&native_legacy_slot_thunk_124,(uintptr_t)&native_legacy_slot_thunk_125,(uintptr_t)&native_legacy_slot_thunk_126,(uintptr_t)&native_legacy_slot_thunk_127,(uintptr_t)&native_legacy_slot_thunk_128,(uintptr_t)&native_legacy_slot_thunk_129,(uintptr_t)&native_legacy_slot_thunk_130,(uintptr_t)&native_legacy_slot_thunk_131,(uintptr_t)&native_legacy_slot_thunk_132,(uintptr_t)&native_legacy_slot_thunk_133,(uintptr_t)&native_legacy_slot_thunk_134,(uintptr_t)&native_legacy_slot_thunk_135,(uintptr_t)&native_legacy_slot_thunk_136,(uintptr_t)&native_legacy_slot_thunk_137,(uintptr_t)&native_legacy_slot_thunk_138,(uintptr_t)&native_legacy_slot_thunk_139,(uintptr_t)&native_legacy_slot_thunk_140,(uintptr_t)&native_legacy_slot_thunk_141,(uintptr_t)&native_legacy_slot_thunk_142,(uintptr_t)&native_legacy_slot_thunk_143,(uintptr_t)&native_legacy_slot_thunk_144,(uintptr_t)&native_legacy_slot_thunk_145,(uintptr_t)&native_legacy_slot_thunk_146,(uintptr_t)&native_legacy_slot_thunk_147,(uintptr_t)&native_legacy_slot_thunk_148,(uintptr_t)&native_legacy_slot_thunk_149,(uintptr_t)&native_legacy_slot_thunk_150,(uintptr_t)&native_legacy_slot_thunk_151,(uintptr_t)&native_legacy_slot_thunk_152,(uintptr_t)&native_legacy_slot_thunk_153,(uintptr_t)&native_legacy_slot_thunk_154,(uintptr_t)&native_legacy_slot_thunk_155,(uintptr_t)&native_legacy_slot_thunk_156,(uintptr_t)&native_legacy_slot_thunk_157,(uintptr_t)&native_legacy_slot_thunk_158,(uintptr_t)&native_legacy_slot_thunk_159,(uintptr_t)&native_legacy_slot_thunk_160,(uintptr_t)&native_legacy_slot_thunk_161,(uintptr_t)&native_legacy_slot_thunk_162,(uintptr_t)&native_legacy_slot_thunk_163,(uintptr_t)&native_legacy_slot_thunk_164,(uintptr_t)&native_legacy_slot_thunk_165,(uintptr_t)&native_legacy_slot_thunk_166,(uintptr_t)&native_legacy_slot_thunk_167,(uintptr_t)&native_legacy_slot_thunk_168,(uintptr_t)&native_legacy_slot_thunk_169,(uintptr_t)&native_legacy_slot_thunk_170,(uintptr_t)&native_legacy_slot_thunk_171,(uintptr_t)&native_legacy_slot_thunk_172,(uintptr_t)&native_legacy_slot_thunk_173,(uintptr_t)&native_legacy_slot_thunk_174,(uintptr_t)&native_legacy_slot_thunk_175,(uintptr_t)&native_legacy_slot_thunk_176,(uintptr_t)&native_legacy_slot_thunk_177,(uintptr_t)&native_legacy_slot_thunk_178,(uintptr_t)&native_legacy_slot_thunk_179,(uintptr_t)&native_legacy_slot_thunk_180,(uintptr_t)&native_legacy_slot_thunk_181,(uintptr_t)&native_legacy_slot_thunk_182,(uintptr_t)&native_legacy_slot_thunk_183,(uintptr_t)&native_legacy_slot_thunk_184,(uintptr_t)&native_legacy_slot_thunk_185,(uintptr_t)&native_legacy_slot_thunk_186,(uintptr_t)&native_legacy_slot_thunk_187,(uintptr_t)&native_legacy_slot_thunk_188,(uintptr_t)&native_legacy_slot_thunk_189,(uintptr_t)&native_legacy_slot_thunk_190,(uintptr_t)&native_legacy_slot_thunk_191,(uintptr_t)&native_legacy_slot_thunk_192,(uintptr_t)&native_legacy_slot_thunk_193,(uintptr_t)&native_legacy_slot_thunk_194,(uintptr_t)&native_legacy_slot_thunk_195,(uintptr_t)&native_legacy_slot_thunk_196,(uintptr_t)&native_legacy_slot_thunk_197,(uintptr_t)&native_legacy_slot_thunk_198,(uintptr_t)&native_legacy_slot_thunk_199,(uintptr_t)&native_legacy_slot_thunk_200,(uintptr_t)&native_legacy_slot_thunk_201,(uintptr_t)&native_legacy_slot_thunk_202,(uintptr_t)&native_legacy_slot_thunk_203,(uintptr_t)&native_legacy_slot_thunk_204,(uintptr_t)&native_legacy_slot_thunk_205,(uintptr_t)&native_legacy_slot_thunk_206,(uintptr_t)&native_legacy_slot_thunk_207,(uintptr_t)&native_legacy_slot_thunk_208,(uintptr_t)&native_legacy_slot_thunk_209,(uintptr_t)&native_legacy_slot_thunk_210,(uintptr_t)&native_legacy_slot_thunk_211,(uintptr_t)&native_legacy_slot_thunk_212,(uintptr_t)&native_legacy_slot_thunk_213,(uintptr_t)&native_legacy_slot_thunk_214,(uintptr_t)&native_legacy_slot_thunk_215,(uintptr_t)&native_legacy_slot_thunk_216,(uintptr_t)&native_legacy_slot_thunk_217,(uintptr_t)&native_legacy_slot_thunk_218,(uintptr_t)&native_legacy_slot_thunk_219,(uintptr_t)&native_legacy_slot_thunk_220,(uintptr_t)&native_legacy_slot_thunk_221,(uintptr_t)&native_legacy_slot_thunk_222,(uintptr_t)&native_legacy_slot_thunk_223,(uintptr_t)&native_legacy_slot_thunk_224,(uintptr_t)&native_legacy_slot_thunk_225,(uintptr_t)&native_legacy_slot_thunk_226,(uintptr_t)&native_legacy_slot_thunk_227,(uintptr_t)&native_legacy_slot_thunk_228,(uintptr_t)&native_legacy_slot_thunk_229,(uintptr_t)&native_legacy_slot_thunk_230,(uintptr_t)&native_legacy_slot_thunk_231,(uintptr_t)&native_legacy_slot_thunk_232,(uintptr_t)&native_legacy_slot_thunk_233,(uintptr_t)&native_legacy_slot_thunk_234,(uintptr_t)&native_legacy_slot_thunk_235,(uintptr_t)&native_legacy_slot_thunk_236,(uintptr_t)&native_legacy_slot_thunk_237,(uintptr_t)&native_legacy_slot_thunk_238,(uintptr_t)&native_legacy_slot_thunk_239,(uintptr_t)&native_legacy_slot_thunk_240,(uintptr_t)&native_legacy_slot_thunk_241,(uintptr_t)&native_legacy_slot_thunk_242,(uintptr_t)&native_legacy_slot_thunk_243,(uintptr_t)&native_legacy_slot_thunk_244,(uintptr_t)&native_legacy_slot_thunk_245,(uintptr_t)&native_legacy_slot_thunk_246,(uintptr_t)&native_legacy_slot_thunk_247,(uintptr_t)&native_legacy_slot_thunk_248,(uintptr_t)&native_legacy_slot_thunk_249,(uintptr_t)&native_legacy_slot_thunk_250,(uintptr_t)&native_legacy_slot_thunk_251,(uintptr_t)&native_legacy_slot_thunk_252,(uintptr_t)&native_legacy_slot_thunk_253,(uintptr_t)&native_legacy_slot_thunk_254,(uintptr_t)&native_legacy_slot_thunk_255,(uintptr_t)&native_legacy_slot_thunk_256,(uintptr_t)&native_legacy_slot_thunk_257,(uintptr_t)&native_legacy_slot_thunk_258,(uintptr_t)&native_legacy_slot_thunk_259,(uintptr_t)&native_legacy_slot_thunk_260,(uintptr_t)&native_legacy_slot_thunk_261,(uintptr_t)&native_legacy_slot_thunk_262,(uintptr_t)&native_legacy_slot_thunk_263,(uintptr_t)&native_legacy_slot_thunk_264,(uintptr_t)&native_legacy_slot_thunk_265,(uintptr_t)&native_legacy_slot_thunk_266,(uintptr_t)&native_legacy_slot_thunk_267,(uintptr_t)&native_legacy_slot_thunk_268,(uintptr_t)&native_legacy_slot_thunk_269,(uintptr_t)&native_legacy_slot_thunk_270,(uintptr_t)&native_legacy_slot_thunk_271,(uintptr_t)&native_legacy_slot_thunk_272,(uintptr_t)&native_legacy_slot_thunk_273,(uintptr_t)&native_legacy_slot_thunk_274,(uintptr_t)&native_legacy_slot_thunk_275,(uintptr_t)&native_legacy_slot_thunk_276,(uintptr_t)&native_legacy_slot_thunk_277,(uintptr_t)&native_legacy_slot_thunk_278,(uintptr_t)&native_legacy_slot_thunk_279,(uintptr_t)&native_legacy_slot_thunk_280,(uintptr_t)&native_legacy_slot_thunk_281,(uintptr_t)&native_legacy_slot_thunk_282,(uintptr_t)&native_legacy_slot_thunk_283,(uintptr_t)&native_legacy_slot_thunk_284,(uintptr_t)&native_legacy_slot_thunk_285,(uintptr_t)&native_legacy_slot_thunk_286,(uintptr_t)&native_legacy_slot_thunk_287,(uintptr_t)&native_legacy_slot_thunk_288,(uintptr_t)&native_legacy_slot_thunk_289,(uintptr_t)&native_legacy_slot_thunk_290,(uintptr_t)&native_legacy_slot_thunk_291,(uintptr_t)&native_legacy_slot_thunk_292,(uintptr_t)&native_legacy_slot_thunk_293,(uintptr_t)&native_legacy_slot_thunk_294,(uintptr_t)&native_legacy_slot_thunk_295,(uintptr_t)&native_legacy_slot_thunk_296,(uintptr_t)&native_legacy_slot_thunk_297,(uintptr_t)&native_legacy_slot_thunk_298,(uintptr_t)&native_legacy_slot_thunk_299,(uintptr_t)&native_legacy_slot_thunk_300,(uintptr_t)&native_legacy_slot_thunk_301,(uintptr_t)&native_legacy_slot_thunk_302,(uintptr_t)&native_legacy_slot_thunk_303,(uintptr_t)&native_legacy_slot_thunk_304,(uintptr_t)&native_legacy_slot_thunk_305,(uintptr_t)&native_legacy_slot_thunk_306,(uintptr_t)&native_legacy_slot_thunk_307,(uintptr_t)&native_legacy_slot_thunk_308,(uintptr_t)&native_legacy_slot_thunk_309,(uintptr_t)&native_legacy_slot_thunk_310,(uintptr_t)&native_legacy_slot_thunk_311,(uintptr_t)&native_legacy_slot_thunk_312,(uintptr_t)&native_legacy_slot_thunk_313,(uintptr_t)&native_legacy_slot_thunk_314,(uintptr_t)&native_legacy_slot_thunk_315,(uintptr_t)&native_legacy_slot_thunk_316,(uintptr_t)&native_legacy_slot_thunk_317,(uintptr_t)&native_legacy_slot_thunk_318,(uintptr_t)&native_legacy_slot_thunk_319,(uintptr_t)&native_legacy_slot_thunk_320,(uintptr_t)&native_legacy_slot_thunk_321,(uintptr_t)&native_legacy_slot_thunk_322,(uintptr_t)&native_legacy_slot_thunk_323,(uintptr_t)&native_legacy_slot_thunk_324,(uintptr_t)&native_legacy_slot_thunk_325,(uintptr_t)&native_legacy_slot_thunk_326,(uintptr_t)&native_legacy_slot_thunk_327,(uintptr_t)&native_legacy_slot_thunk_328,(uintptr_t)&native_legacy_slot_thunk_329,(uintptr_t)&native_legacy_slot_thunk_330,(uintptr_t)&native_legacy_slot_thunk_331,(uintptr_t)&native_legacy_slot_thunk_332,(uintptr_t)&native_legacy_slot_thunk_333,(uintptr_t)&native_legacy_slot_thunk_334,(uintptr_t)&native_legacy_slot_thunk_335,(uintptr_t)&native_legacy_slot_thunk_336,(uintptr_t)&native_legacy_slot_thunk_337,(uintptr_t)&native_legacy_slot_thunk_338,(uintptr_t)&native_legacy_slot_thunk_339,(uintptr_t)&native_legacy_slot_thunk_340,(uintptr_t)&native_legacy_slot_thunk_341,(uintptr_t)&native_legacy_slot_thunk_342,(uintptr_t)&native_legacy_slot_thunk_343,(uintptr_t)&native_legacy_slot_thunk_344,(uintptr_t)&native_legacy_slot_thunk_345,(uintptr_t)&native_legacy_slot_thunk_346,(uintptr_t)&native_legacy_slot_thunk_347,(uintptr_t)&native_legacy_slot_thunk_348,(uintptr_t)&native_legacy_slot_thunk_349,(uintptr_t)&native_legacy_slot_thunk_350,(uintptr_t)&native_legacy_slot_thunk_351,(uintptr_t)&native_legacy_slot_thunk_352,(uintptr_t)&native_legacy_slot_thunk_353,(uintptr_t)&native_legacy_slot_thunk_354,(uintptr_t)&native_legacy_slot_thunk_355,(uintptr_t)&native_legacy_slot_thunk_356,(uintptr_t)&native_legacy_slot_thunk_357};
static void native_legacy_slot_trace_arm(void) {
    if(native_legacy_slot_trace_armed || !native_legacy_object)return;
    unsigned enabled=0;PE_parse_boot_argn("offline-legacy-slot-trace",&enabled,sizeof(enabled));
    unsigned lift=0;PE_parse_boot_argn("offline-legacy-lift-refusals",&lift,sizeof(lift));
    if(!enabled)return;
    uintptr_t *vtable=*reinterpret_cast<uintptr_t **>(native_legacy_object);
    uintptr_t *copy=static_cast<uintptr_t *>(IOMalloc(kDisplaySlots*sizeof(uintptr_t)));
    if(!copy)return;
    memcpy(copy,vtable,kDisplaySlots*sizeof(uintptr_t));
    unsigned armed=0,lifted=0;
    for(unsigned i=0;i<kDisplaySlots;++i) {
        // A kernel virtual address has its top 16 bits set.  The test is >> 48,
        // NOT >> 40: for a 48-bit address >> 40 leaves 24 bits and is false for
        // every valid pointer, which is the defect that made an earlier guard in
        // this project reject everything it was meant to check.
        native_legacy_slot_original[i]=copy[i];
        native_legacy_slot_lifted[i]=0;
        if(copy[i] && (copy[i]>>48)==0xFFFF){
            if(lift && native_legacy_slot_is_refusing_stub(copy[i])){native_legacy_slot_lifted[i]=1;++lifted;}
            copy[i]=native_legacy_slot_thunks_init[i];++armed;
        }
    }
    *reinterpret_cast<uintptr_t **>(native_legacy_object)=copy;
    native_legacy_slot_trace_armed=true;
    native_legacy_slot_armed_count=armed;
    native_legacy_slot_lifted_count=lifted;
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
static void native_legacy_display_late_body(void *, wait_result_t);
void native_legacy_display_late_probe(void) {
    native_keystore_probe();
    native_hid_input_probe();
    native_virtio_input_probe();
    native_shared_window_probe();
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
