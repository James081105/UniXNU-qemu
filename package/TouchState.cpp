// SPDX-License-Identifier: BSD-2-Clause-Patent
// The finger set NativeHidInput reports as its touch screen, published under a sequence lock: the producer makes
// native_touch_seq odd, writes count, identifiers and positions (x 0..30480, y 0..20320), then makes it even again.
// On sheng the touch panel probe (TouchProbe.cpp) defines and writes them; on this board VirtioInput.cpp writes them
// from QEMU's tablet, and UniXNU's live-hid.py mailbox through the debugger.
#include <stdint.h>

extern "C" {
volatile uint32_t native_touch_seq, native_touch_count, native_touch_hid_reports;
volatile int native_touch_xs[12], native_touch_ys[12], native_touch_ids[12];
}
