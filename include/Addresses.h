#ifndef DTE_ADDRESSES_H
#define DTE_ADDRESSES_H

// Decoding of `reg` and `ranges`, and translation of bus addresses to CPU
// addresses, as the kernel's of_translate_address() does: a node's `reg`
// is in its parent bus's address space; each bus up to the root maps child
// addresses to its own parent's through `ranges` (empty `ranges` means
// 1:1, no `ranges` means the bus is not memory-mapped, e.g. I2C).
//
// Addresses and sizes of up to two cells (64 bits) are supported; wider
// ones (e.g. 3-cell PCI addresses) are reported as unsupported.

#include "DeviceTree.h"

#include <string>
#include <vector>

namespace DTE {

// #address-cells / #size-cells that apply to the children of `bus`
// (Devicetree Specification defaults: 2 and 1).
struct AddressCells {
    uint32_t address = 2;
    uint32_t size = 1;
};
AddressCells cellsForChildren(const DeviceTreeNode& bus);

struct RegEntry {
    uint64_t address = 0;    // in the parent bus's address space
    uint64_t size = 0;
    bool hasSize = true;     // false when the parent has #size-cells = 0
    bool translated = false;
    uint64_t cpuAddress = 0; // valid when translated
    std::string note;        // why it is not translated
};

struct RangeEntry {
    uint64_t childAddress = 0;
    uint64_t parentAddress = 0;
    uint64_t size = 0;
};

// Decodes the node's `reg` and translates each entry. Returns false (and
// sets `error`) if `reg` is missing or malformed for the parent's cells.
bool decodeReg(const std::shared_ptr<DeviceTreeNode>& node, std::vector<RegEntry>& out,
               std::string* error = nullptr);

// Decodes the bus's `ranges`. An empty `ranges` sets `identity`.
bool decodeRanges(const std::shared_ptr<DeviceTreeNode>& bus, std::vector<RangeEntry>& out, bool& identity,
                  std::string* error = nullptr);

// Translates `address`, given in the address space of the children of
// `bus`, to a CPU address. Returns false and sets `why` if it cannot.
bool translateAddress(const std::shared_ptr<DeviceTreeNode>& bus, uint64_t address, uint64_t& cpuAddress,
                      std::string* why = nullptr);

// "0x1000 size 0x100 -> CPU 0x100001000" / "0x50 (not memory-mapped: ...)"
std::string describeReg(const RegEntry& entry);
// "0x0..0x3fffffff -> 0x100000000"
std::string describeRange(const RangeEntry& entry);

} // namespace DTE

#endif // DTE_ADDRESSES_H
