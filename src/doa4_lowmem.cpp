// See doa4_lowmem.h.

#include "doa4_lowmem.h"

#include <rex/logging.h>
#include <rex/system/xmemory.h>

namespace doa4 {

bool ReserveLowGuestMemory(rex::memory::Memory* memory) {
  if (!memory) {
    REXLOG_WARN("[doa4][MEM] no guest memory; low heap range not reserved");
    return false;
  }
  rex::memory::BaseHeap* heap = memory->LookupHeap(kLowGuestFloor0);
  if (!heap) {
    REXLOG_WARN("[doa4][MEM] no heap at {:#010x}; low heap range not reserved", kLowGuestFloor0);
    return false;
  }
  const uint32_t size = kLowGuestFloor - kLowGuestFloor0;
  const bool ok = heap->AllocFixed(kLowGuestFloor0, size, heap->page_size(),
                                   rex::memory::kMemoryAllocationReserve,
                                   rex::memory::kMemoryProtectNoAccess);
  if (ok) {
    REXLOG_INFO("[doa4][MEM] reserved guest {:#010x}-{:#010x}: 4 KB-page allocations start at {:#010x}",
                kLowGuestFloor0, kLowGuestFloor - 1, kLowGuestFloor);
  } else {
    REXLOG_ERROR("[doa4][MEM] could not reserve guest {:#010x}-{:#010x} (already in use); fighter data "
                 "may be loaded low and Ryu Hayabusa / Brad Wong can crash",
                 kLowGuestFloor0, kLowGuestFloor - 1);
  }
  return ok;
}

}  // namespace doa4
