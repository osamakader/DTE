#include "Addresses.h"

#include <cstdio>

namespace DTE {

namespace {

std::string hex(uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

bool fail(std::string* error, const std::string& msg) {
    if (error) *error = msg;
    return false;
}

// Combines `count` cells starting at `pos` (count <= 2).
uint64_t combine(const std::vector<uint32_t>& cells, size_t pos, uint32_t count) {
    uint64_t v = 0;
    for (uint32_t i = 0; i < count; ++i) v = (v << 32) | cells[pos + i];
    return v;
}

bool singleCell(const DeviceTreeNode& node, const char* name, uint32_t& out) {
    const auto* p = node.findProperty(name);
    if (!p || !p->isCells() || p->getValueAsCells().size() != 1) return false;
    out = p->getValueAsCells()[0];
    return true;
}

bool supported(uint32_t cells, const std::string& what, std::string* error) {
    if (cells > 2) {
        return fail(error, std::to_string(cells) + "-cell " + what + " (e.g. PCI) are not supported");
    }
    return true;
}

} // namespace

AddressCells cellsForChildren(const DeviceTreeNode& bus) {
    AddressCells c;
    singleCell(bus, "#address-cells", c.address);
    singleCell(bus, "#size-cells", c.size);
    return c;
}

bool decodeRanges(const std::shared_ptr<DeviceTreeNode>& bus, std::vector<RangeEntry>& out, bool& identity,
                  std::string* error) {
    out.clear();
    identity = false;
    const auto* ranges = static_cast<const DeviceTreeNode&>(*bus).findProperty("ranges");
    if (!ranges) return fail(error, bus->getFullPath() + " has no 'ranges'");
    if (ranges->isEmpty()) {
        identity = true;
        return true;
    }
    if (!ranges->isCells()) return fail(error, "'ranges' must be a list of cells");

    const auto parent = bus->getParent();
    const AddressCells child = cellsForChildren(*bus);
    const uint32_t parentAddressCells = parent ? cellsForChildren(*parent).address : child.address;
    if (!supported(child.address, "child addresses", error) || !supported(parentAddressCells, "parent addresses", error) ||
        !supported(child.size, "sizes", error)) {
        return false;
    }

    const auto cells = ranges->getValueAsCells();
    const size_t entry = static_cast<size_t>(child.address) + parentAddressCells + child.size;
    if (entry == 0 || cells.size() % entry != 0) {
        return fail(error, "'ranges' has " + std::to_string(cells.size()) +
                               " cells, which is not a multiple of child #address-cells + parent #address-cells + "
                               "child #size-cells (" + std::to_string(child.address) + " + " +
                               std::to_string(parentAddressCells) + " + " + std::to_string(child.size) + ")");
    }
    for (size_t i = 0; i < cells.size(); i += entry) {
        RangeEntry r;
        r.childAddress = combine(cells, i, child.address);
        r.parentAddress = combine(cells, i + child.address, parentAddressCells);
        r.size = combine(cells, i + child.address + parentAddressCells, child.size);
        out.push_back(r);
    }
    return true;
}

bool translateAddress(const std::shared_ptr<DeviceTreeNode>& bus, uint64_t address, uint64_t& cpuAddress,
                      std::string* why) {
    uint64_t a = address;
    for (auto cur = bus; cur && cur->getParent(); cur = cur->getParent()) {
        if (!static_cast<const DeviceTreeNode&>(*cur).findProperty("ranges")) {
            return fail(why, "not memory-mapped: " + cur->getFullPath() + " has no 'ranges'");
        }
        std::vector<RangeEntry> ranges;
        bool identity = false;
        std::string error;
        if (!decodeRanges(cur, ranges, identity, &error)) {
            return fail(why, cur->getFullPath() + ": " + error);
        }
        if (identity) continue;

        bool mapped = false;
        for (const auto& r : ranges) {
            if (a >= r.childAddress && a - r.childAddress < r.size) {
                a = r.parentAddress + (a - r.childAddress);
                mapped = true;
                break;
            }
        }
        if (!mapped) {
            return fail(why, hex(a) + " is outside the 'ranges' of " + cur->getFullPath());
        }
    }
    cpuAddress = a;
    return true;
}

bool decodeReg(const std::shared_ptr<DeviceTreeNode>& node, std::vector<RegEntry>& out, std::string* error) {
    out.clear();
    const auto* reg = static_cast<const DeviceTreeNode&>(*node).findProperty("reg");
    if (!reg) return fail(error, "no 'reg' property");
    if (!reg->isCells()) return fail(error, "'reg' must be a list of cells");

    const auto parent = node->getParent();
    const AddressCells c = parent ? cellsForChildren(*parent) : AddressCells{};
    if (!supported(c.address, "addresses", error) || !supported(c.size, "sizes", error)) return false;

    const auto cells = reg->getValueAsCells();
    const size_t entry = static_cast<size_t>(c.address) + c.size;
    if (entry == 0 || cells.empty() || cells.size() % entry != 0) {
        return fail(error, "'reg' has " + std::to_string(cells.size()) +
                               " cells, which is not a multiple of #address-cells + #size-cells (" +
                               std::to_string(c.address) + " + " + std::to_string(c.size) + ")");
    }
    for (size_t i = 0; i < cells.size(); i += entry) {
        RegEntry e;
        e.address = combine(cells, i, c.address);
        e.size = combine(cells, i + c.address, c.size);
        e.hasSize = c.size > 0;
        if (parent) {
            e.translated = translateAddress(parent, e.address, e.cpuAddress, &e.note);
        }
        out.push_back(e);
    }
    return true;
}

std::string describeReg(const RegEntry& e) {
    std::string s = hex(e.address);
    if (e.hasSize) s += " size " + hex(e.size);
    if (e.translated) {
        if (e.cpuAddress != e.address) s += " -> CPU " + hex(e.cpuAddress);
    } else {
        s += " (" + e.note + ")";
    }
    return s;
}

std::string describeRange(const RangeEntry& r) {
    const uint64_t last = r.size ? r.childAddress + r.size - 1 : r.childAddress;
    return hex(r.childAddress) + ".." + hex(last) + " -> " + hex(r.parentAddress);
}

} // namespace DTE
