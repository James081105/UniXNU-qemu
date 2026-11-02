// SPDX-License-Identifier: BSD-2-Clause-Patent
// Default-off original HID observation; a separate explicit gate starts an
// owned offline input source. No entitlement or original provider is replaced.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSKext.h>
#include <pexpert/pexpert.h>
#include <mach/kmod.h>
extern kmod_info_t g_kernel_kmod_info;
extern "C" void native_hid_static_init(void) __asm__("__GLOBAL__sub_I_NativeHidInput.cpp");
extern "C" IOService *native_hid_input_allocate(void);
extern "C" IOService *native_hid_touch_allocate(void);
extern "C" {
extern kern_return_t OSKextLoadKextWithIdentifier(const char *);
volatile uint32_t native_hid_stage,native_hid_resource_found;
volatile int native_hid_load_status=-1;
volatile int native_hid_matching_load_status=-1;
volatile uint32_t native_hid_class_sizes[5];
volatile int native_hid_input_meta_status=-1;
volatile uint64_t native_hid_input_object;
volatile uint32_t native_hid_input_events[8];
volatile int native_hid_input_results[8]={-1,-1,-1,-1,-1,-1,-1,-1};
volatile uint32_t native_hid_input_command;
char native_hid_input_registry[2048];
char native_hid_input_props[1024];
extern volatile uint32_t native_legacy_transfer_calls;
// The scanout the GOP presenter mapped and cross-checked against PE_current_console
// (NativeGOPPresenter.cpp:82-98), exported by it at :14 and :96-97.  Read-only here:
// this file never writes a pixel, it only measures whether the guest did.
extern volatile uint32_t native_gop_ready;
extern volatile uint64_t native_gop_virtual,native_gop_physical;
// The console publisher, the same strong extern the kernel patches use.
//
// PAIRED BY CONFIGURATION, NOT BY LUCK, and that is the whole reason this is safe.
// The only defconfig that sets CONFIG_XNU_NATIVE_LEGACY_DISPLAY -- whose archive
// force-loads this object (build-native-legacy-display.py:252-254 puts HidInputProbe.o
// in libNativeLegacyDisplay.late.a, and build-xnu-4k-baseline.sh:625-627 force-loads it)
// -- is qemu_root_defconfig, and that same config sets
// CONFIG_XNU_NATIVE_VIRTIO_CONSOLE, which is what puts libNativeVirtioConsole.a on the
// kernel link (build-xnu-4k-baseline.sh:672-677).  qemu_defconfig sets NEITHER.  So
// there is no configuration in which this reference is unresolved.
//
// STRONG and never null-checked, for the reason the kernel patches record at length:
// on build 41330622788 a weak_import reference resolved to null and dropped two
// records behind a green build, and the `if (symbol)` guard made the absence look
// deliberate.  A strong reference makes a missing archive fail the link naming the
// symbol instead.
extern int native_console_report(const void *, size_t);
// Optional unattended Home presses for hardware, which has no external harness writing the mailbox:
// offline-hid-auto-home=<seconds after the second presented frame>, offline-hid-auto-home-count (default 1)
// and offline-hid-auto-home-interval (default 20 s). Disabled unless the first argument is set.
static unsigned autoConfigured,autoFirst,autoCount,autoInterval,autoSent;
static uint64_t autoNext,autoFrequency;
static uint64_t counterNow(void){uint64_t v;__asm__ volatile("isb\n\tmrs %0, cntpct_el0":"=r"(v));return v;}
bool native_hid_input_requested(void) {
    if(native_legacy_transfer_calls<2)return false;
    if(!autoConfigured) {
        autoConfigured=1;autoFirst=0;autoCount=1;autoInterval=20;
        PE_parse_boot_argn("offline-hid-auto-home",&autoFirst,sizeof(autoFirst));
        PE_parse_boot_argn("offline-hid-auto-home-count",&autoCount,sizeof(autoCount));
        PE_parse_boot_argn("offline-hid-auto-home-interval",&autoInterval,sizeof(autoInterval));
        if(autoCount>8)autoCount=8;if(autoInterval<5)autoInterval=5;
        __asm__ volatile("mrs %0, cntfrq_el0":"=r"(autoFrequency));
        if(autoFirst && autoFrequency)autoNext=counterNow()+(uint64_t)autoFirst*autoFrequency;
    }
    if(autoFirst && autoFrequency && autoSent<autoCount && counterNow()>=autoNext) {
        ++autoSent;autoNext+=(uint64_t)autoInterval*autoFrequency;return true;
    }
    if(native_hid_input_command!=1)return false;
    native_hid_input_command=0;return true;
}
static void describeInput(IORegistryEntry *entry,unsigned depth,size_t &used) {
    if(depth>4 || used>=sizeof(native_hid_input_registry)-128)return;
    int count=snprintf(native_hid_input_registry+used,sizeof(native_hid_input_registry)-used,
        "%u %s [%s]\n",depth,entry->getName(),entry->getMetaClass()->getClassName());
    if(count<=0 || (size_t)count>=sizeof(native_hid_input_registry)-used)return;
    used+=count;OSIterator *it=entry->getChildIterator(gIOServicePlane);
    if(!it)return;
    while(OSObject *child=it->getNextObject()) {
        IORegistryEntry *next=OSDynamicCast(IORegistryEntry,child);
        if(next)describeInput(next,depth+1,used);
    }
    it->release();
}
// Compact one property of the first IOHIDEventDriver below entry: XML tags dropped, key end -> '=', value end -> ' '.
static IORegistryEntry *findEventDriver(IORegistryEntry *parent,unsigned depth) {
    if(!depth)return nullptr;
    OSIterator *it=parent->getChildIterator(gIOServicePlane);if(!it)return nullptr;
    IORegistryEntry *found=nullptr;
    while(OSObject *child=it->getNextObject()) {
        IORegistryEntry *entry=OSDynamicCast(IORegistryEntry,child);
        if(!entry)continue;
        if(entry->metaCast("IOHIDEventDriver")){found=entry;break;}
        if((found=findEventDriver(entry,depth-1)))break;
    }
    it->release();return found;
}
static void appendProperty(IORegistryEntry *driver,const char *key,const char *label,size_t &used) {
    OSObject *value=driver->copyProperty(key);
    int n=snprintf(native_hid_input_props+used,sizeof(native_hid_input_props)-used,"%s:",label);
    if(n>0)used+=n;
    if(value) {
        OSSerialize *text=OSSerialize::withCapacity(4096);
        if(text && value->serialize(text)) {
            const char *in=text->text();bool tag=false;char name[16];unsigned nameLen=0;
            for(unsigned i=0;in && in[i] && used<sizeof(native_hid_input_props)-2;++i) {
                char c=in[i];
                if(c=='<'){tag=true;nameLen=0;continue;}
                if(tag) {
                    if(c=='>') {
                        tag=false;name[nameLen]=0;
                        if(!strcmp(name,"/key"))native_hid_input_props[used++]='=';
                        else if(name[0]=='/')native_hid_input_props[used++]=' ';
                        else if(!strcmp(name,"true/")||!strcmp(name,"false/")){
                            used+=snprintf(native_hid_input_props+used,sizeof(native_hid_input_props)-used,"%s ",name[0]=='t'?"T":"F");
                        }
                    } else if(nameLen<sizeof(name)-1)name[nameLen++]=c;
                    continue;
                }
                if(c=='\n'||c=='\t')continue;
                native_hid_input_props[used++]=c;
            }
        }
        OSSafeReleaseNULL(text);OSSafeReleaseNULL(value);
    } else if(used<sizeof(native_hid_input_props)-6) {memcpy(native_hid_input_props+used,"none ",5);used+=5;}
    native_hid_input_props[used]=0;
}
/* ---------------------------------------------------------------------------
 * M9: did the input change what is on screen?
 *
 * THE PRD VETO IS THE WHOLE REASON THIS EXISTS.  milestones.py:1745-1752::
 *
 *     @_predicate("homescreen_screenshot_only")
 *     def _homescreen_screenshot_only(snapshot):
 *         return _ok(snapshot, "p.screendump") and not _ok(snapshot, "p.springboard_interaction")
 *
 * A homescreen screenshot plus a mouse click is not evidence of an operable
 * SpringBoard -- it is a screenshot and a click.  The gate refuses to be satisfied
 * by the pair, so `springboard.interaction` cannot mean "input was delivered".  It
 * has to mean the input changed what is on screen, and those are different facts.
 *
 * BOTH HALVES ARE KERNEL-SIDE, which matters because the guest userspace is Apple's
 * real code and cannot be modified to emit anything:
 *
 *   the screen -- NativeGOPPresenter::start() maps the scanout writable and
 *     cross-checks the mapping against PE_current_console(): same v_baseAddr,
 *     v_width, v_height, v_rowBytes, depth 32, no rotate.  It then publishes
 *     native_gop_ready and native_gop_virtual.  A digest over those bytes is a
 *     MEASUREMENT of what the user is looking at, not an inference about it.
 *   the input -- native_hid_input_observed() below is called on every report this
 *     driver hands to the HID stack, from touchSend() (:188), buttonsAction()
 *     (:204) and tick() (:244).
 *
 * So: digest, deliver, digest again, report only if the two differ.
 *
 * WHAT THIS DOES NOT PROVE, SAID PLAINLY.  A digest changing proves the screen
 * changed after input.  It does not prove the user's intent was honoured -- a clock
 * redraw would also change it.  The record therefore carries BOTH digests rather
 * than a boolean, so a reader can see exactly which two states were compared
 * instead of trusting a claim.  It is bounded to a few records per boot because
 * the channel's send() waits synchronously (NativeVirtioConsole.cpp:226 sets the
 * deadline, :250 is the wait) and the gate keeps only the first record for an
 * event name anyway.
 * --------------------------------------------------------------------------- */

// A 64-bit FNV-1a digest over the firmware scanout, or false if there is nothing
// verified to read.  The cross-checks are NativeGOPPresenter::start()'s own, and
// the one that matters most is the last: native_gop_physical must equal
// video.v_baseAddr, which is the presenter proving the mapping this file reads
// through is the mapping it validated.
static uint64_t screen_digest(bool *readable) {
    PE_Video video={};
    *readable=false;
    if(!native_gop_ready || !native_gop_virtual)return 0;
    // The presenter publishes native_gop_physical and native_gop_virtual and THEN
    // raises native_gop_ready (NativeGOPPresenter.cpp:96-97).  Nothing orders those
    // writes on the writer's side and this file cannot add a barrier there, so the
    // read side takes one: see the flag, barrier, then read the data.  The
    // v_baseAddr cross-check below is the second line of defence -- a stale or
    // garbage virtual address almost certainly comes with a stale physical one,
    // and the check fails closed rather than hashing the wrong memory.
    __asm__ volatile("dmb ish":::"memory");
    if(PE_current_console(&video) || !video.v_baseAddr || !video.v_width || !video.v_height ||
       video.v_depth!=32 || video.v_rotate ||
       video.v_rowBytes<video.v_width*4 || video.v_rowBytes>UINT64_MAX/video.v_height)return 0;
    if(native_gop_physical!=video.v_baseAddr)return 0;
    // The presenter writes the frame and issues `dsb oshst` before counting it
    // (NativeGOPPresenter.cpp:120); read it back under the same ordering.
    __asm__ volatile("dmb ish":::"memory");
    const uint64_t bytes=video.v_rowBytes*video.v_height;
    const volatile uint8_t *base=reinterpret_cast<const volatile uint8_t *>(native_gop_virtual);
    uint64_t hash=1469598103934665603ULL;
    // Eight bytes at a time where the row allows it: a 3048x2032x32 frame is ~24 MB,
    // and a byte-at-a-time pass over it on the work loop's timer thread is long
    // enough to delay the next input report.  The tail is byte-wise because
    // v_rowBytes is only guaranteed to be a multiple of 4.  A direct volatile
    // 64-bit load rather than memcpy: this translation unit has no reason to pull
    // in <string.h> for one aligned copy, and ARM64 handles the unaligned tail
    // case through the byte loop instead.
    uint64_t i=0;
    for(;i+8<=bytes;i+=8) {
        const uint64_t word=*reinterpret_cast<const volatile uint64_t *>(base+i);
        hash^=word;hash*=1099511628211ULL;
    }
    for(;i<bytes;++i) {hash^=base[i];hash*=1099511628211ULL;}
    *readable=true;
    return hash;
}
static char const interaction_hex[]="0123456789abcdef";
// The longest record this can build, NUL included: 44 prefix + 16 + 11 + 16 + 32
// suffix is 119, so 160 can only be under-filled.
#define INTERACTION_RECORD_MAX 160
static uint64_t interaction_before;
static bool interaction_armed;
static unsigned interaction_reports;
#define INTERACTION_REPORT_LIMIT 4
// Hand-formatted, not snprintf: this translation unit does not include the header
// that declares it, and the same constraint is why the kernel patches hand-format.
// Every copy is bounded by the buffer even though the worst case is 121 of 160
// bytes, because a future edit to one of the three literals should fail safe
// rather than silently overrun.
//
// THE ORDER IS prefix, before, middle, after, suffix AND NOTHING ELSE.  An earlier
// revision of this function carried a `parts[]` loop that copied all three literals
// up front and then copied middle and suffix again after the hex, which produced
// `{"event":...,"before":"","after":"<hex><middle><hex><suffix>` -- a record that
// compiles clean and parses as nothing.  That is the exact defect two drafts of the
// mount emitter had, and the only thing that catches it is reassembling the
// literals the way the preprocessor will and parsing the result, which
// local-private/tmp/check-interaction-emitter.py does.
static void report_interaction(uint64_t before,uint64_t after) {
    if(interaction_reports>=INTERACTION_REPORT_LIMIT)return;
    static char const prefix[]="{\"event\":\"springboard.interaction\",\"before\":\"";
    static char const middle[]="\",\"after\":\"";
    static char const suffix[]="\",\"source\":\"XNU.HidInputProbe\"}\n";
    char record[INTERACTION_RECORD_MAX];
    unsigned used=0;
    const unsigned limit=sizeof(record)-1;
    for(unsigned k=0;k<sizeof(prefix)-1 && used<limit;++k)record[used++]=prefix[k];
    for(int shift=60;shift>=0 && used<limit;shift-=4)record[used++]=interaction_hex[(before>>shift)&0xf];
    for(unsigned k=0;k<sizeof(middle)-1 && used<limit;++k)record[used++]=middle[k];
    for(int shift=60;shift>=0 && used<limit;shift-=4)record[used++]=interaction_hex[(after>>shift)&0xf];
    for(unsigned k=0;k<sizeof(suffix)-1 && used<limit;++k)record[used++]=suffix[k];
    record[used]=0;
    native_console_report(record,used);
    ++interaction_reports;
}
// ARM.  Events 4, 5 and 6 are the reports this driver actually delivered: 4 is a
// press or release, 5 the same while held, 6 a touch frame.  0, 1, 2, 3 and 7 are
// probe lifecycle and liveness, and arming on those would make the record about
// the driver's own bookkeeping rather than about the guest.
//
// THE EARLY RETURN IS A PERFORMANCE BOUND, not a correctness one.  Each arm hashes
// the whole frame (~24 MB, three million 8-byte reads on TCG), and a held key
// delivers a report every 100 ms.  Without it, ten reports inside one compare
// window would be ten hashes; with it there are at most two per second -- one arm
// and one compare -- which is the same order as the registry walk the checkpoint
// already does every second.
static void interaction_arm(void) {
    if(interaction_armed || interaction_reports>=INTERACTION_REPORT_LIMIT)return;
    bool readable=false;
    interaction_before=screen_digest(&readable);
    interaction_armed=readable;
}
// COMPARE, from the timer's own cadence.  tick() fires this every second (100 ms
// while a key is held), so the "after" sample is taken up to a second after the
// input -- long enough for SpringBoard to have drawn, short enough that the guest
// has not moved on to something unrelated.
static void interaction_compare(void) {
    if(!interaction_armed)return;
    interaction_armed=false;
    bool readable=false;
    const uint64_t after=screen_digest(&readable);
    if(readable && after!=interaction_before)report_interaction(interaction_before,after);
}
__attribute__((noinline)) void native_hid_live_checkpoint(IOService *device) {
    size_t used=0;native_hid_input_registry[0]=0;describeInput(device,0,used);
    if(IORegistryEntry *driver=findEventDriver(device,4)) {
        size_t props=0;native_hid_input_props[0]=0;
        // backboardd's digitizer service classification reads Built-In/DisplayIntegrated from the event service it
        // opened, not from the HID device below it; publish them on the service itself (idempotent, once per second).
        // Set once only: every setProperty on a service with attached user clients is a registry property-change
        // notification, and repeating it each second keeps backboardd/hidd busy re-evaluating the service.
        static IORegistryEntry *publishedFor;
        if(device->getProperty("Built-In") && publishedFor!=driver) {
            publishedFor=driver;
            driver->setProperty("Built-In",kOSBooleanTrue);driver->setProperty("DisplayIntegrated",kOSBooleanTrue);
            // backboardd (-[BKHIDDirectTouchEventProcessor processEvent:sender:dispatcher:], decoded from the target) drops a
            // digitizer event unless the sending service has PrimaryUsagePage 0x0D and PrimaryUsage 1, 4 or 0x20
            // ("ignoring digitizer event ... from unsupported service"). The combined device lists its keyboard collection
            // first, so the services would publish (1,6); publish Digitizer/Touch Screen on the services after they have
            // matched (setting it on the device would change driver matching, which is observed to depend on the layout).
            driver->setProperty("PrimaryUsagePage",(unsigned long long)0x0d,32);driver->setProperty("PrimaryUsage",(unsigned long long)4,32);
            // backboardd's digitizer filter is {DeviceUsagePage, DeviceUsage, Built-In}; make the flag visible on the
            // interface (the event service's provider) as well, not only on the device two levels down.
            if(IORegistryEntry *interface=driver->getParentEntry(gIOServicePlane)) {
                interface->setProperty("Built-In",kOSBooleanTrue);interface->setProperty("DisplayIntegrated",kOSBooleanTrue);
                interface->setProperty("PrimaryUsagePage",(unsigned long long)0x0d,32);interface->setProperty("PrimaryUsage",(unsigned long long)4,32);
            }
        }
        // The Digitizer/DebugState dumps (appendProperty) are not read on hardware runs without a photograph and walk
        // driver-internal state from a foreign thread; only the cheap Built-In readback stays.
        appendProperty(driver,"Built-In","BI",props);
        appendProperty(driver,"PrimaryUsagePage","PP",props);appendProperty(driver,"PrimaryUsage","PU",props);
    }
    __asm__ volatile("" ::: "memory");
    // The M9 half that turns an input into evidence: digest the screen now and
    // compare it with the one taken when the report was delivered.  Placed here
    // rather than in the report path because this runs on the timer's own
    // cadence, and the guest needs time between the two samples to have drawn.
    interaction_compare();
}
void native_hid_input_observed(unsigned event,int result) {
    if(event<8){native_hid_input_events[event]=native_hid_input_events[event]+1;native_hid_input_results[event]=result;}
    // A report this driver actually delivered (see interaction_arm for why 4/5/6
    // and not the others), and it succeeded: result is an IOReturn, where 0 is
    // success and kIOReturnNotOpen would need the IOKit header to name.
    if((event==4||event==5||event==6) && result==0)interaction_arm();
}
__attribute__((noinline)) void native_hid_checkpoint(void) { __asm__ volatile("" ::: "memory"); }
void native_hid_input_probe(void) {
    unsigned enabled=0;
    if(!PE_parse_boot_argn("offline-hid-input",&enabled,sizeof(enabled)) || enabled!=1)return;
    if(native_hid_stage)return;
    native_hid_stage=1;
    native_hid_load_status=OSKextLoadKextWithIdentifier("com.apple.iokit.IOHIDFamily");
    if(!native_hid_load_status)
        native_hid_matching_load_status=OSKext::loadKextWithIdentifier("com.apple.iokit.IOHIDEventDriver",
            true,false,kOSKextExcludeNone,kOSKextExcludeNone,nullptr);
    native_hid_stage=2;
    const char *names[]={"IOHIDUserDevice","IOHIDEventDriver","IOHIDEventService","IOHIDResourceDeviceUserClient","IOHIDDevice"};
    for(unsigned i=0;i<5;++i) {
        const OSSymbol *name=OSSymbol::withCString(names[i]);
        const OSMetaClass *meta=name?OSMetaClass::getMetaClassWithName(name):nullptr;
        if(meta)native_hid_class_sizes[i]=meta->getClassSize();
        OSSafeReleaseNULL(name);
    }
    if(native_hid_load_status==0) {
        OSDictionary *match=IOService::serviceMatching("IOHIDResource");
        IOService *resource=match?IOService::waitForMatchingService(match,5000000000ULL):nullptr;
        native_hid_resource_found=resource!=nullptr;
        unsigned sendHome=0;PE_parse_boot_argn("offline-hid-home",&sendHome,sizeof(sendHome));
        if(resource && sendHome==1 && native_hid_class_sizes[4]==208) {
            void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
            if(handle) {
                native_hid_static_init();native_hid_input_meta_status=OSMetaClass::postModLoad(handle);
                if(!native_hid_input_meta_status) {
                    IOService *device=native_hid_input_allocate();
                    native_hid_input_object=(uintptr_t)device;
                    bool valid=device && device->metaCast("IOHIDDevice") && device->init();
                    native_hid_input_observed(0,valid?0:kIOReturnError);
                    bool attached=valid && device->attach(resource);
                    native_hid_input_observed(1,attached?0:kIOReturnError);
                    bool started=attached && device->start(resource);
                    if(!started) {
                        native_hid_input_observed(7,kIOReturnError);
                        if(attached)device->detach(resource);
                        OSSafeReleaseNULL(device);native_hid_input_object=0;
                    }
                    unsigned touchHid=0;
                    unsigned split=0;
                    PE_parse_boot_argn("sheng-touch-split",&split,sizeof(split));
                    if(started && split==1 && PE_parse_boot_argn("sheng-touch-hid",&touchHid,sizeof(touchHid)) && touchHid==1) {
                        // Separate digitizer service (primary usage Digitizer/Touch Screen, Built-In).
                        IOService *touch=native_hid_touch_allocate();
                        bool touchStarted=touch && touch->metaCast("IOHIDDevice") && touch->init() && touch->attach(resource) && touch->start(resource);
                        if(!touchStarted && touch){touch->detach(resource);OSSafeReleaseNULL(touch);}
                    }
                    // The offline devices live for this bounded boot. Its
                    // service attachment and timer remain genuine live objects.
                }
            }
        }
        OSSafeReleaseNULL(resource);OSSafeReleaseNULL(match);
    }
    native_hid_stage=3;native_hid_checkpoint();
}
}
