#include "Interrupts.h"

#include <cstdio>

namespace DTE {

namespace {

constexpr int kMaxDepth = 16; // guards against interrupt-map loops

std::string hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%x", v);
    return buf;
}

std::string cellsText(const std::vector<uint32_t>& cells) {
    std::string s = "<";
    for (size_t i = 0; i < cells.size(); ++i) s += (i ? " " : "") + hex(cells[i]);
    return s + ">";
}

const DeviceTreeProperty* prop(const std::shared_ptr<DeviceTreeNode>& n, const char* name) {
    return static_cast<const DeviceTreeNode&>(*n).findProperty(name);
}

bool singleCell(const std::shared_ptr<DeviceTreeNode>& n, const char* name, uint32_t& out) {
    const auto* p = prop(n, name);
    if (!p || !p->isCells() || p->getValueAsCells().size() != 1) return false;
    out = p->getValueAsCells()[0];
    return true;
}

std::vector<uint32_t> cellsOf(const std::shared_ptr<DeviceTreeNode>& n, const char* name) {
    const auto* p = prop(n, name);
    return p && p->isCells() ? p->getValueAsCells() : std::vector<uint32_t>{};
}

std::shared_ptr<DeviceTreeNode> byPhandle(const DeviceTree& tree, const std::shared_ptr<DeviceTreeNode>& n) {
    uint32_t ph = 0;
    return singleCell(n, "interrupt-parent", ph) ? tree.findNodeByPhandle(ph) : nullptr;
}

// Follows the interrupt from `parent` (with `specifier` and the child's
// unit address `unit`) through nexus nodes to an interrupt controller.
void resolve(const DeviceTree& tree, std::shared_ptr<DeviceTreeNode> parent, std::vector<uint32_t> specifier,
             std::vector<uint32_t> unit, InterruptSpec& out) {
    for (int depth = 0; depth < kMaxDepth; ++depth) {
        if (!parent) {
            out.problem = "no interrupt parent found";
            return;
        }
        uint32_t intCells = 0;
        if (!singleCell(parent, "#interrupt-cells", intCells)) {
            out.problem = parent->getFullPath() + " has no valid #interrupt-cells";
            return;
        }
        if (specifier.size() != intCells) {
            out.problem = "specifier " + cellsText(specifier) + " does not match #interrupt-cells = " +
                          std::to_string(intCells) + " of " + parent->getFullPath();
            return;
        }
        if (prop(parent, "interrupt-controller")) {
            out.controller = parent;
            out.specifier = specifier;
            return;
        }

        const auto map = cellsOf(parent, "interrupt-map");
        if (map.empty()) {
            out.problem = parent->getFullPath() + " is neither an interrupt-controller nor has an interrupt-map";
            return;
        }
        // Nexus: key = unit address (#address-cells of the nexus, default 2)
        // followed by the specifier, compared under interrupt-map-mask
        uint32_t addrCells = 2;
        singleCell(parent, "#address-cells", addrCells);
        std::vector<uint32_t> key(addrCells, 0);
        for (uint32_t i = 0; i < addrCells && i < unit.size(); ++i) key[i] = unit[i];
        key.insert(key.end(), specifier.begin(), specifier.end());
        std::vector<uint32_t> mask = cellsOf(parent, "interrupt-map-mask");
        if (mask.empty()) mask.assign(key.size(), 0xffffffff);
        if (mask.size() != key.size()) {
            out.problem = parent->getFullPath() + ": interrupt-map-mask has " + std::to_string(mask.size()) +
                          " cells, expected " + std::to_string(key.size());
            return;
        }

        bool matched = false;
        for (size_t pos = 0; pos < map.size();) {
            if (pos + key.size() + 1 > map.size()) break;
            bool equal = true;
            for (size_t i = 0; i < key.size(); ++i) {
                if ((key[i] ^ map[pos + i]) & mask[i]) equal = false;
            }
            auto next = tree.findNodeByPhandle(map[pos + key.size()]);
            if (!next) {
                out.problem = parent->getFullPath() + ": interrupt-map refers to phandle " +
                              hex(map[pos + key.size()]) + ", which no node has";
                return;
            }
            uint32_t nextAddrCells = 0; // kernel default for interrupt-map parents
            singleCell(next, "#address-cells", nextAddrCells);
            uint32_t nextIntCells = 0;
            if (!singleCell(next, "#interrupt-cells", nextIntCells)) {
                out.problem = next->getFullPath() + " (in the interrupt-map of " + parent->getFullPath() +
                              ") has no valid #interrupt-cells";
                return;
            }
            const size_t start = pos + key.size() + 1;
            const size_t end = start + nextAddrCells + nextIntCells;
            if (end > map.size()) {
                out.problem = parent->getFullPath() + ": interrupt-map is truncated";
                return;
            }
            if (equal) {
                out.route.push_back(parent->getFullPath());
                unit.assign(map.begin() + static_cast<std::ptrdiff_t>(start),
                            map.begin() + static_cast<std::ptrdiff_t>(start + nextAddrCells));
                specifier.assign(map.begin() + static_cast<std::ptrdiff_t>(start + nextAddrCells),
                                 map.begin() + static_cast<std::ptrdiff_t>(end));
                parent = next;
                matched = true;
                break;
            }
            pos = end;
        }
        if (!matched) {
            out.problem = "no interrupt-map entry of " + parent->getFullPath() + " matches " + cellsText(key);
            return;
        }
    }
    out.problem = "interrupt-map nesting is too deep (loop?)";
}

const char* triggerName(uint32_t flags) {
    switch (flags & 0xf) {
    case 1: return "edge rising";
    case 2: return "edge falling";
    case 3: return "edge both";
    case 4: return "level high";
    case 8: return "level low";
    default: return nullptr;
    }
}

bool isGic(const DeviceTreeNode& controller) {
    const auto* compatible = controller.findProperty("compatible");
    if (!compatible) return false;
    for (const auto& c : compatible->getValueAsStrings()) {
        if (c.rfind("arm,gic", 0) == 0 || c.rfind("arm,cortex-a", 0) == 0 || c == "qcom,msm-qgic2") return true;
    }
    return false;
}

} // namespace

std::shared_ptr<DeviceTreeNode> interruptParentOf(const DeviceTree& tree, const std::shared_ptr<DeviceTreeNode>& node) {
    // Like of_irq_find_parent(): follow interrupt-parent, else the tree
    // parent, until a node with #interrupt-cells
    auto child = node;
    for (int depth = 0; child && depth < 256; ++depth) {
        auto p = prop(child, "interrupt-parent") ? byPhandle(tree, child) : child->getParent();
        if (!p) return nullptr;
        if (prop(p, "#interrupt-cells")) return p;
        child = p;
    }
    return nullptr;
}

bool decodeInterrupts(const DeviceTree& tree, const std::shared_ptr<DeviceTreeNode>& node,
                      std::vector<InterruptSpec>& out, std::string* error) {
    out.clear();
    std::vector<std::string> names;
    if (const auto* n = prop(node, "interrupt-names")) names = n->getValueAsStrings();
    const std::vector<uint32_t> unit = cellsOf(node, "reg");
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    if (prop(node, "interrupts-extended")) {
        size_t index = 0;
        for (const auto& ref : tree.referencesIn(node, "interrupts-extended")) {
            InterruptSpec spec;
            spec.index = index;
            spec.property = "interrupts-extended";
            if (index < names.size()) spec.name = names[index];
            if (!ref.problem.empty()) {
                spec.problem = ref.problem;
            } else {
                resolve(tree, ref.target, ref.args, unit, spec);
            }
            out.push_back(spec);
            ++index;
        }
        return true;
    }

    const auto* interrupts = prop(node, "interrupts");
    if (!interrupts) return fail("no 'interrupts' or 'interrupts-extended' property");
    if (!interrupts->isCells()) return fail("'interrupts' must be a list of cells");

    auto parent = interruptParentOf(tree, node);
    if (!parent) {
        // Point at an interrupt-parent that was skipped for lacking #interrupt-cells
        for (auto n = node; n; n = n->getParent()) {
            if (!prop(n, "interrupt-parent")) continue;
            if (auto target = byPhandle(tree, n); target && !prop(target, "#interrupt-cells")) {
                return fail("no interrupt parent found: interrupt-parent " + target->getFullPath() +
                            " has no #interrupt-cells");
            }
            break;
        }
        return fail("no interrupt parent found (no interrupt-parent and no ancestor with #interrupt-cells)");
    }
    uint32_t intCells = 0;
    if (!singleCell(parent, "#interrupt-cells", intCells) || intCells == 0) {
        return fail("interrupt parent " + parent->getFullPath() + " has no valid #interrupt-cells");
    }
    const auto cells = interrupts->getValueAsCells();
    if (cells.size() % intCells != 0) {
        return fail("'interrupts' has " + std::to_string(cells.size()) + " cells, which is not a multiple of " +
                    "#interrupt-cells = " + std::to_string(intCells) + " of " + parent->getFullPath());
    }
    for (size_t i = 0; i < cells.size(); i += intCells) {
        InterruptSpec spec;
        spec.index = i / intCells;
        spec.property = "interrupts";
        if (spec.index < names.size()) spec.name = names[spec.index];
        resolve(tree, parent,
                std::vector<uint32_t>(cells.begin() + static_cast<std::ptrdiff_t>(i),
                                      cells.begin() + static_cast<std::ptrdiff_t>(i + intCells)),
                unit, spec);
        out.push_back(spec);
    }
    return true;
}

std::string describeSpecifier(const DeviceTreeNode& controller, const std::vector<uint32_t>& spec) {
    if (spec.size() == 3 && isGic(controller) && spec[0] <= 1) {
        const bool spi = spec[0] == 0;
        std::string s = std::string(spi ? "SPI " : "PPI ") + std::to_string(spec[1]) + " (hwirq " +
                        std::to_string(spec[1] + (spi ? 32 : 16)) + ")";
        if (const char* t = triggerName(spec[2])) s += std::string(", ") + t;
        if (!spi && (spec[2] >> 8) & 0xff) s += ", CPU mask " + hex((spec[2] >> 8) & 0xff);
        return s;
    }
    if (spec.size() == 2) {
        if (const char* t = triggerName(spec[1]); t && (spec[1] & ~0xfu) == 0) {
            return "line " + std::to_string(spec[0]) + ", " + t;
        }
    }
    if (spec.size() == 1) return "line " + std::to_string(spec[0]);
    return cellsText(spec);
}

std::string describeInterrupt(const InterruptSpec& spec) {
    std::string s;
    if (!spec.name.empty()) s += spec.name + ": ";
    if (!spec.problem.empty()) return s + "(" + spec.problem + ")";
    s += spec.controller->getFullPath() + " " + describeSpecifier(*spec.controller, spec.specifier);
    for (const auto& via : spec.route) s += " via " + via;
    return s;
}

} // namespace DTE
