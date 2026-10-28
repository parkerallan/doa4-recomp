// Byte access to guest memory, for code that must also run in host tests.
//
// The app backs it with the runtime's guest memory (TranslateVirtual); a host
// test backs it with a map. Guest memory is big-endian.

#pragma once

#include <cstdint>
#include <functional>

namespace doa4 {

struct GuestBytes {
  std::function<uint8_t(uint32_t address)> read;
  std::function<void(uint32_t address, uint8_t value)> write;

  uint8_t Read(uint32_t address) const { return read(address); }
  void Write(uint32_t address, uint8_t value) const { write(address, value); }
  uint32_t ReadBe32(uint32_t address) const {
    return (uint32_t(Read(address)) << 24) | (uint32_t(Read(address + 1)) << 16) |
           (uint32_t(Read(address + 2)) << 8) | uint32_t(Read(address + 3));
  }
};

}  // namespace doa4
