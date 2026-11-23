// Structural validation of a device tree (DeviceTree::check).

#include "DeviceTree.h"
#include "DtsValue.h"
#include "Addresses.h"
#include "Interrupts.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <map>
#include <set>

namespace DTE {

std::string ValidationIssue::toString() const {
    std::string s = isError() ? "error: " : "warning: ";
    s += path;
    if (!property.empty()) s += ":" + property;
    return s + ": " + message;
}

namespace {

// Defaults from the Devicetree Specification when the parent does not
// specify them.
constexpr uint32_t kDefaultAddressCells = 2;
constexpr uint32_t kDefaultSizeCells = 1;

class Checker {
public:
    std::vector<ValidationIssue> run(const DeviceTree& tree) {
        tree_ = &tree;
        const auto root = tree.getRoot();
        // Problems found while decoding phandle references, by source node
        for (auto& ref : tree.references()) {
            if (!ref.problem.empty()) referenceProblems_.emplace(ref.source.get(), ref);
        }
        if (!root) {
            error("/", "", "Tree has no root node");
            return issues_;
        }
        collectPhandles(root);
        checkRoot(*root);
        visit(root);
        return issues_;
    }

private:
    std::vector<ValidationIssue> issues_;
    std::map<uint32_t, std::string> phandles_; // value -> first node path
    std::shared_ptr<DeviceTreeNode> root_;
    std::multimap<const DeviceTreeNode*, PhandleReference> referenceProblems_;
    const DeviceTree* tree_ = nullptr;

    void add(ValidationIssue::Severity sev, const std::string& path, const std::string& prop,
             const std::string& msg) {
        issues_.push_back({sev, path, prop, msg});
    }
    void error(const std::string& path, const std::string& prop, const std::string& msg) {
        add(ValidationIssue::Severity::Error, path, prop, msg);
    }
    void warning(const std::string& path, const std::string& prop, const std::string& msg) {
        add(ValidationIssue::Severity::Warning, path, prop, msg);
    }

    static std::string hex(uint64_t v) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(v));
        return buf;
    }

    // Single-cell property value, if it is exactly one cell.
    static bool singleCell(const DeviceTreeProperty* p, uint32_t& out) {
        if (!p || !p->isCells()) return false;
        auto cells = p->getValueAsCells();
        if (cells.size() != 1) return false;
        out = cells[0];
        return true;
    }

    void collectPhandles(const std::shared_ptr<DeviceTreeNode>& root) {
        root_ = root;
        std::function<void(const std::shared_ptr<DeviceTreeNode>&)> walk =
            [&](const std::shared_ptr<DeviceTreeNode>& n) {
                for (const char* name : {"phandle", "linux,phandle"}) {
                    const auto* p = static_cast<const DeviceTreeNode&>(*n).findProperty(name);
                    if (!p) continue;
                    uint32_t v = 0;
                    if (!singleCell(p, v)) {
                        error(n->getFullPath(), name, "must be a single cell");
                        continue;
                    }
                    if (v == 0 || v == 0xffffffff) {
                        error(n->getFullPath(), name, "value 0x" + hex(v) + " is reserved");
                        continue;
                    }
                    auto [it, inserted] = phandles_.emplace(v, n->getFullPath());
                    if (!inserted && it->second != n->getFullPath()) {
                        error(n->getFullPath(), name,
                              "duplicate phandle 0x" + hex(v) + " (also used by " + it->second + ")");
                    }
                }
                for (const auto& c : n->getChildren()) walk(c);
            };
        walk(root);
    }

    void checkRoot(const DeviceTreeNode& root) {
        for (const char* required : {"compatible", "model", "#address-cells", "#size-cells"}) {
            if (!root.findProperty(required)) {
                warning("/", "", std::string("root node is missing the required '") + required + "' property");
            }
        }
    }

    void visit(const std::shared_ptr<DeviceTreeNode>& node) {
        const std::string path = node->getFullPath();
        const auto& n = static_cast<const DeviceTreeNode&>(*node);
        const auto parent = node->getParent();

        std::string why;
        if (parent && !isValidNodeName(n.getName(), &why)) {
            error(path, "", why);
        }

        std::set<std::string> seen;
        for (const auto& child : n.getChildren()) {
            if (!seen.insert(child->getName()).second) {
                error(path, "", "duplicate child node '" + child->getName() + "'");
            }
        }

        for (const auto& prop : n.getProperties()) {
            if (!isValidPropertyName(prop.getName(), &why)) {
                error(path, prop.getName(), why);
            }
        }

        checkCellCounts(path, n);
        if (parent) checkReg(path, n, *parent);
        checkAddressing(path, node);
        checkCompatible(path, n);
        checkStatus(path, n);
        checkReferences(path, n);
        checkInterrupts(path, node);
        if (parent && !parent->getParent() && n.getName() == "aliases") checkAliases(path, n);

        for (const auto& child : n.getChildren()) visit(child);
    }

    void checkCellCounts(const std::string& path, const DeviceTreeNode& n) {
        for (const char* name : {"#address-cells", "#size-cells"}) {
            const auto* p = n.findProperty(name);
            uint32_t v = 0;
            if (p && !singleCell(p, v)) error(path, name, "must be a single cell");
        }
    }

    static uint32_t cellsOf(const DeviceTreeNode& parent, const char* name, uint32_t fallback) {
        uint32_t v = fallback;
        singleCell(parent.findProperty(name), v);
        return v;
    }

    void checkReg(const std::string& path, const DeviceTreeNode& n, const DeviceTreeNode& parent) {
        const auto at = n.getName().find('@');
        const std::string unit = at == std::string::npos ? "" : n.getName().substr(at + 1);
        const auto* reg = n.findProperty("reg");

        if (!reg) {
            // Nodes like /cpus/cpu@0 always have reg; a unit address without
            // reg (or ranges, for buses) is suspicious.
            if (!unit.empty() && !n.findProperty("ranges")) {
                warning(path, "", "node has a unit address but no 'reg' property");
            }
            return;
        }
        if (!reg->isCells()) {
            error(path, "reg", "must be a list of 32-bit cells");
            return;
        }

        const uint32_t ac = cellsOf(parent, "#address-cells", kDefaultAddressCells);
        const uint32_t sc = cellsOf(parent, "#size-cells", kDefaultSizeCells);
        const auto cells = reg->getValueAsCells();
        const size_t entry = static_cast<size_t>(ac) + sc;
        if (entry == 0 || cells.empty() || cells.size() % entry != 0) {
            error(path, "reg",
                  "has " + std::to_string(cells.size()) + " cells, which is not a multiple of the parent's "
                  "#address-cells + #size-cells (" + std::to_string(ac) + " + " + std::to_string(sc) + ")");
            return;
        }

        if (unit.empty()) {
            warning(path, "", "node has a 'reg' property but no unit address (expected name@" +
                                  expectedUnit(cells, ac) + ")");
            return;
        }
        if (unit.size() > 2 && (unit.compare(0, 2, "0x") == 0 || unit.compare(0, 2, "0X") == 0)) {
            warning(path, "", "unit address should not start with 0x");
            return;
        }
        // Only plain hex addresses up to 64 bits can be compared reliably;
        // bus-specific forms like "0,1" are left alone.
        if (ac == 0 || ac > 2 || unit.find(',') != std::string::npos) return;
        std::string lower = unit;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::string expected = expectedUnit(cells, ac);
        if (lower != expected) {
            warning(path, "", "unit address '" + unit + "' does not match the first reg address (0x" +
                                  expected + ")");
        }
    }

    static std::string expectedUnit(const std::vector<uint32_t>& cells, uint32_t ac) {
        uint64_t addr = 0;
        for (uint32_t i = 0; i < ac && i < 2 && i < cells.size(); ++i) addr = (addr << 32) | cells[i];
        return hex(addr);
    }

    // ranges must decode; reg must fall inside the parent buses' windows
    void checkAddressing(const std::string& path, const std::shared_ptr<DeviceTreeNode>& node) {
        const auto& n = static_cast<const DeviceTreeNode&>(*node);
        std::string error;
        if (const auto* ranges = n.findProperty("ranges"); ranges && node->getParent()) {
            std::vector<RangeEntry> entries;
            bool identity = false;
            if (!decodeRanges(node, entries, identity, &error) && error.find("not supported") == std::string::npos) {
                this->error(path, "ranges", error);
            }
        }
        std::vector<RegEntry> regs;
        if (node->getParent() && n.findProperty("reg") && decodeReg(node, regs, &error)) {
            for (const auto& r : regs) {
                if (!r.translated && r.note.find("outside the 'ranges'") != std::string::npos) {
                    warning(path, "reg", "address " + r.note);
                    break;
                }
            }
        }
    }

    void checkCompatible(const std::string& path, const DeviceTreeNode& n) {
        const auto* p = n.findProperty("compatible");
        if (!p) return;
        if (p->isEmpty() || !(p->isString() || p->isStringList())) {
            error(path, "compatible", "must be a string or a list of strings");
        }
    }

    void checkStatus(const std::string& path, const DeviceTreeNode& n) {
        const auto* p = n.findProperty("status");
        if (!p) return;
        if (!p->isString() || p->isEmpty()) {
            error(path, "status", "must be a string");
            return;
        }
        const auto& v = std::get<std::string>(p->getValue());
        if (v != "okay" && v != "disabled" && v != "reserved" && v != "fail" && v.rfind("fail-", 0) != 0) {
            warning(path, "status", "unknown value \"" + v + "\" (expected okay, disabled, reserved, fail or fail-sss)");
        }
    }

    // interrupt-parent, clocks, gpios, ... (see DeviceTree::references)
    void checkReferences(const std::string& path, const DeviceTreeNode& n) {
        auto [begin, end] = referenceProblems_.equal_range(&n);
        for (auto it = begin; it != end; ++it) {
            error(path, it->second.property, it->second.problem);
        }
    }

    // interrupts / interrupts-extended must resolve to an interrupt
    // controller (through interrupt-map nexus nodes if needed)
    void checkInterrupts(const std::string& path, const std::shared_ptr<DeviceTreeNode>& node) {
        const auto& n = static_cast<const DeviceTreeNode&>(*node);
        if (!n.findProperty("interrupts") && !n.findProperty("interrupts-extended")) return;
        std::vector<InterruptSpec> specs;
        std::string err;
        if (!decodeInterrupts(*tree_, node, specs, &err)) {
            error(path, n.findProperty("interrupts-extended") ? "interrupts-extended" : "interrupts", err);
            return;
        }
        auto [begin, end] = referenceProblems_.equal_range(&n);
        for (const auto& spec : specs) {
            if (spec.problem.empty()) continue;
            bool reported = false; // already flagged as a broken phandle reference
            for (auto it = begin; it != end; ++it) {
                if (it->second.property == spec.property && it->second.problem == spec.problem) reported = true;
            }
            if (!reported) error(path, spec.property, spec.problem);
        }
    }

    void checkAliases(const std::string& path, const DeviceTreeNode& n) {
        for (const auto& prop : n.getProperties()) {
            const auto& name = prop.getName();
            if (!std::all_of(name.begin(), name.end(), [](unsigned char c) {
                    return std::islower(c) || std::isdigit(c) || c == '-';
                })) {
                warning(path, name, "alias names should only use lowercase letters, digits and '-'");
            }
            if (!prop.isString() || prop.isEmpty()) {
                error(path, name, "alias must be a path string");
                continue;
            }
            const auto& target = std::get<std::string>(prop.getValue());
            if (target.empty() || target[0] != '/' || !root_->findNodeByPath(target)) {
                warning(path, name, "alias points to \"" + target + "\", which does not exist");
            }
        }
    }
};

} // namespace

std::vector<ValidationIssue> DeviceTree::check() const {
    return Checker().run(*this);
}

bool DeviceTree::validate() const {
    for (const auto& issue : check()) {
        if (issue.isError()) return false;
    }
    return true;
}

} // namespace DTE
