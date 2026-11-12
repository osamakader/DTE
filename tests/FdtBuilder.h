#ifndef DTE_TESTS_FDTBUILDER_H
#define DTE_TESTS_FDTBUILDER_H

// Minimal flattened-device-tree writer used to craft valid and deliberately
// malformed blobs for parser tests. It writes exactly what it is told; it
// does not enforce structural validity.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace DTE::test {

class FdtBuilder {
public:
    static constexpr uint32_t BEGIN_NODE = 0x1;
    static constexpr uint32_t END_NODE = 0x2;
    static constexpr uint32_t PROP = 0x3;
    static constexpr uint32_t NOP = 0x4;
    static constexpr uint32_t END = 0x9;

    // Header field byte offsets
    static constexpr size_t OFF_MAGIC = 0;
    static constexpr size_t OFF_TOTALSIZE = 4;
    static constexpr size_t OFF_DT_STRUCT = 8;
    static constexpr size_t OFF_DT_STRINGS = 12;
    static constexpr size_t OFF_MEM_RSVMAP = 16;
    static constexpr size_t OFF_VERSION = 20;
    static constexpr size_t OFF_LAST_COMP = 24;
    static constexpr size_t OFF_BOOT_CPU = 28;
    static constexpr size_t OFF_SIZE_STRINGS = 32;
    static constexpr size_t OFF_SIZE_STRUCT = 36;

    FdtBuilder& beginNode(const std::string& name) {
        token(BEGIN_NODE);
        appendPadded(reinterpret_cast<const uint8_t*>(name.c_str()), name.size() + 1);
        return *this;
    }

    FdtBuilder& endNode() { return token(END_NODE); }
    FdtBuilder& nop() { return token(NOP); }
    FdtBuilder& end() { return token(END); }

    FdtBuilder& token(uint32_t t) {
        be32(structBlock_, t);
        return *this;
    }

    FdtBuilder& prop(const std::string& name, const std::vector<uint8_t>& value) {
        return propWithNameOff(stringOffset(name), value);
    }

    // Emits a property with an explicit (possibly bogus) name offset.
    FdtBuilder& propWithNameOff(uint32_t nameoff, const std::vector<uint8_t>& value) {
        token(PROP);
        be32(structBlock_, static_cast<uint32_t>(value.size()));
        be32(structBlock_, nameoff);
        appendPadded(value.data(), value.size());
        return *this;
    }

    FdtBuilder& propEmpty(const std::string& name) { return prop(name, {}); }

    FdtBuilder& propString(const std::string& name, const std::vector<std::string>& strings) {
        std::vector<uint8_t> v;
        for (const auto& s : strings) {
            v.insert(v.end(), s.begin(), s.end());
            v.push_back('\0');
        }
        return prop(name, v);
    }

    FdtBuilder& propCells(const std::string& name, const std::vector<uint32_t>& cells) {
        std::vector<uint8_t> v;
        for (auto c : cells) be32(v, c);
        return prop(name, v);
    }

    FdtBuilder& reserve(uint64_t address, uint64_t size) {
        reservations_.push_back({address, size});
        return *this;
    }

    FdtBuilder& bootCpu(uint32_t id) {
        bootCpu_ = id;
        return *this;
    }

    // Serialises the blob. version 16 omits size_dt_struct from the header.
    std::vector<uint8_t> build(uint32_t version = 17, uint32_t lastCompVersion = 16) const {
        const size_t headerSize = version >= 17 ? 40 : 36;
        const size_t rsvOff = (headerSize + 7) & ~size_t(7);
        const size_t rsvSize = (reservations_.size() + 1) * 16;
        const size_t structOff = rsvOff + rsvSize;
        const size_t stringsOff = structOff + structBlock_.size();
        const size_t total = stringsOff + stringsBlock_.size();

        std::vector<uint8_t> out(rsvOff, 0);
        put32(out, OFF_MAGIC, 0xd00dfeed);
        put32(out, OFF_TOTALSIZE, static_cast<uint32_t>(total));
        put32(out, OFF_DT_STRUCT, static_cast<uint32_t>(structOff));
        put32(out, OFF_DT_STRINGS, static_cast<uint32_t>(stringsOff));
        put32(out, OFF_MEM_RSVMAP, static_cast<uint32_t>(rsvOff));
        put32(out, OFF_VERSION, version);
        put32(out, OFF_LAST_COMP, lastCompVersion);
        put32(out, OFF_BOOT_CPU, bootCpu_);
        put32(out, OFF_SIZE_STRINGS, static_cast<uint32_t>(stringsBlock_.size()));
        if (version >= 17) {
            put32(out, OFF_SIZE_STRUCT, static_cast<uint32_t>(structBlock_.size()));
        }

        for (const auto& r : reservations_) {
            be64(out, r.first);
            be64(out, r.second);
        }
        be64(out, 0);
        be64(out, 0);

        out.insert(out.end(), structBlock_.begin(), structBlock_.end());
        out.insert(out.end(), stringsBlock_.begin(), stringsBlock_.end());
        return out;
    }

    static void put32(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
        buf[off] = uint8_t(v >> 24);
        buf[off + 1] = uint8_t(v >> 16);
        buf[off + 2] = uint8_t(v >> 8);
        buf[off + 3] = uint8_t(v);
    }

    static uint32_t get32(const std::vector<uint8_t>& buf, size_t off) {
        return (uint32_t(buf[off]) << 24) | (uint32_t(buf[off + 1]) << 16) |
               (uint32_t(buf[off + 2]) << 8) | uint32_t(buf[off + 3]);
    }

private:
    std::vector<uint8_t> structBlock_;
    std::vector<uint8_t> stringsBlock_;
    std::map<std::string, uint32_t> stringOffsets_;
    std::vector<std::pair<uint64_t, uint64_t>> reservations_;
    uint32_t bootCpu_ = 0;

    uint32_t stringOffset(const std::string& name) {
        auto it = stringOffsets_.find(name);
        if (it != stringOffsets_.end()) return it->second;
        auto off = static_cast<uint32_t>(stringsBlock_.size());
        stringsBlock_.insert(stringsBlock_.end(), name.begin(), name.end());
        stringsBlock_.push_back('\0');
        stringOffsets_[name] = off;
        return off;
    }

    void appendPadded(const uint8_t* data, size_t len) {
        structBlock_.insert(structBlock_.end(), data, data + len);
        while (structBlock_.size() % 4) structBlock_.push_back(0);
    }

    static void be32(std::vector<uint8_t>& buf, uint32_t v) {
        buf.push_back(uint8_t(v >> 24));
        buf.push_back(uint8_t(v >> 16));
        buf.push_back(uint8_t(v >> 8));
        buf.push_back(uint8_t(v));
    }

    static void be64(std::vector<uint8_t>& buf, uint64_t v) {
        be32(buf, uint32_t(v >> 32));
        be32(buf, uint32_t(v));
    }
};

} // namespace DTE::test

#endif // DTE_TESTS_FDTBUILDER_H
