// SPDX-License-Identifier: BSD-2-Clause-Patent
// Which external-method selector a sandbox profile admits for a connection.
//
// This is a header of plain C++ on purpose -- no IOKit, no Objective-C, no Mach
// types -- so that the same logic the guest's render server runs is also
// compilable and runnable by the host's own test suite. The first version of it
// lived inline in QemuMetalScanout.mm and had a bug that only an executing test
// could have caught:
//
//     uint32_t admittedSelector(Link &link) {
//         for (uint32_t selector = 0; selector < 256; ++selector)
//             if (tunnel(link, kProbe, nullptr, 0, nullptr, 0) == 0) return selector;
//         return 0;
//     }
//
// ``tunnel`` read the selector out of ``link.selector``, which the loop never
// wrote -- so this probed selector 0 two hundred and fifty-six times. It then
// answered ``0`` both for "selector 0 is admitted" and for "nothing is
// admitted", and the caller's ``if (!link.selector)`` treated 0 as failure. The
// two readings were therefore indistinguishable, and the one that was wrong was
// the quiet one: a profile that admitted selector 0 produced a scanout client
// that logged "the profile admits no external-method selector" and never asked
// the presenter for a frame.
//
// A source-level guard cannot see that. It can see that the loop exists, that
// ``tunnel`` is called, and that the return value is compared -- all of which
// were true of the broken version. What separates them is running the loop with
// a probe that admits different selectors, which is what this header makes
// possible and what tests/unit/test_qemu_metal_selector.py does.
#ifndef QEMU_METAL_SELECTOR_H
#define QEMU_METAL_SELECTOR_H

#include <cstdint>

namespace qemu_metal_selector {

// A selector is 0..255. This is not one, so "nothing was admitted" and
// "selector 0 was admitted" are two different answers rather than the same zero.
constexpr uint32_t kNoSelector = 0xffffffffu;

// Whether ``selector`` was admitted. Implemented by the caller: a probe is a
// tunnel call whose method is kProbe, and the driver answers success for any
// selector the connection's profile lists and refuses every other one.
using Probe = bool (*)(uint32_t selector, void *context);

// The first selector the probe admits, or kNoSelector.
//
// ``probe`` is called exactly once per selector in ascending order and the loop
// stops at the first success -- the order is not a preference, it is what makes
// a run reproducible: two profiles admitting different selectors produce
// different logs, and a scan that shuffled would make the same machine report
// different selectors on different boots.
inline uint32_t admittedSelector(Probe probe, void *context) {
    if (!probe) return kNoSelector;
    for (uint32_t selector = 0; selector < 256; ++selector)
        if (probe(selector, context)) return selector;
    return kNoSelector;
}

}  // namespace qemu_metal_selector

#endif  // QEMU_METAL_SELECTOR_H
