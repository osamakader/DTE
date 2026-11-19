// Phandle lookup and decoding of phandle references (DeviceTree).

#include "DeviceTree.h"

#include <cstdio>
#include <functional>
#include <map>

namespace DTE {

namespace {

// How a property holds phandles.
struct RefSpec {
    const char* cellsName;  // target property giving the specifier size; nullptr: no args
    bool cellsOptional;     // missing #...-cells means 0 instead of an error
    bool single;            // exactly one phandle cell
};

// Property names that hold phandle references, following dtc's checks.
const std::map<std::string, RefSpec>& namedSpecs() {
    static const std::map<std::string, RefSpec> specs = {
        // single phandles
        {"interrupt-parent", {nullptr, false, true}},
        {"phy-handle", {nullptr, false, true}},
        {"next-level-cache", {nullptr, false, true}},
        {"remote-endpoint", {nullptr, false, true}},
        {"operating-points-v2", {nullptr, false, true}},
        // lists of phandles without arguments
        {"memory-region", {nullptr, false, false}},
        {"nvmem-cells", {nullptr, false, false}},
        {"cpu-idle-states", {nullptr, false, false}},
        // phandle + specifier lists
        {"clocks", {"#clock-cells", false, false}},
        {"assigned-clocks", {"#clock-cells", false, false}},
        {"assigned-clock-parents", {"#clock-cells", false, false}},
        {"cooling-device", {"#cooling-cells", false, false}},
        {"dmas", {"#dma-cells", false, false}},
        {"hwlocks", {"#hwlock-cells", false, false}},
        {"interconnects", {"#interconnect-cells", false, false}},
        {"interrupts-extended", {"#interrupt-cells", false, false}},
        {"io-channels", {"#io-channel-cells", false, false}},
        {"iommus", {"#iommu-cells", false, false}},
        {"mboxes", {"#mbox-cells", false, false}},
        {"msi-parent", {"#msi-cells", true, false}},
        {"mux-controls", {"#mux-control-cells", false, false}},
        {"phys", {"#phy-cells", false, false}},
        {"power-domains", {"#power-domain-cells", false, false}},
        {"pwms", {"#pwm-cells", false, false}},
        {"resets", {"#reset-cells", false, false}},
        {"sound-dai", {"#sound-dai-cells", false, false}},
        {"thermal-sensors", {"#thermal-sensor-cells", false, false}},
        {"gpios", {"#gpio-cells", false, false}},
        {"gpio", {"#gpio-cells", false, false}},
    };
    return specs;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Spec for a property name, or nullptr if it does not hold phandles.
const RefSpec* specFor(const std::string& name) {
    const auto& named = namedSpecs();
    if (auto it = named.find(name); it != named.end()) return &it->second;

    static const RefSpec gpioSpec{"#gpio-cells", false, false};
    static const RefSpec pinctrlSpec{nullptr, false, false};
    // foo-gpios / foo-gpio, but not counts like nr-gpios / ngpios
    if ((endsWith(name, "-gpios") || endsWith(name, "-gpio")) && name != "nr-gpios") return &gpioSpec;
    // pinctrl-0, pinctrl-1, ...
    if (name.rfind("pinctrl-", 0) == 0 && name.size() > 8 &&
        name.find_first_not_of("0123456789", 8) == std::string::npos) {
        return &pinctrlSpec;
    }
    return nullptr;
}

std::string hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%x", v);
    return buf;
}

using PhandleMap = std::map<uint32_t, std::shared_ptr<DeviceTreeNode>>;

PhandleMap buildPhandleMap(const std::shared_ptr<DeviceTreeNode>& root) {
    PhandleMap map;
    std::function<void(const std::shared_ptr<DeviceTreeNode>&)> walk = [&](const std::shared_ptr<DeviceTreeNode>& n) {
        for (const char* name : {"phandle", "linux,phandle"}) {
            const auto* p = static_cast<const DeviceTreeNode&>(*n).findProperty(name);
            if (p && p->isCells() && p->getValueAsCells().size() == 1) {
                map.emplace(p->getValueAsCells()[0], n); // first definition wins
            }
        }
        for (const auto& c : n->getChildren()) walk(c);
    };
    if (root) walk(root);
    return map;
}

void decodeProperty(const std::shared_ptr<DeviceTreeNode>& node, const DeviceTreeProperty& prop,
                    const RefSpec& spec, const PhandleMap& phandles, std::vector<PhandleReference>& out) {
    auto entry = [&](size_t index, uint32_t ph) {
        PhandleReference r;
        r.source = node;
        r.property = prop.getName();
        r.cellIndex = index;
        r.phandle = ph;
        return r;
    };

    if (!prop.isCells()) {
        if (prop.isEmpty()) return;
        auto r = entry(0, 0);
        r.problem = "must be a list of 32-bit cells holding phandles";
        out.push_back(r);
        return;
    }
    const auto cells = prop.getValueAsCells();
    if (spec.single && cells.size() != 1) {
        auto r = entry(0, cells.empty() ? 0 : cells[0]);
        r.problem = "must be a single phandle";
        out.push_back(r);
        return;
    }

    for (size_t i = 0; i < cells.size();) {
        const uint32_t ph = cells[i];
        if (ph == 0) { // empty entry, e.g. a gpio hole
            ++i;
            continue;
        }
        auto r = entry(i, ph);
        auto it = phandles.find(ph);
        if (it == phandles.end()) {
            r.problem = "refers to phandle " + hex(ph) + ", which no node has";
            out.push_back(r);
            return;
        }
        r.target = it->second;

        size_t argc = 0;
        if (spec.cellsName) {
            const auto* cellsProp = static_cast<const DeviceTreeNode&>(*r.target).findProperty(spec.cellsName);
            if (cellsProp && cellsProp->isCells() && cellsProp->getValueAsCells().size() == 1) {
                argc = cellsProp->getValueAsCells()[0];
            } else if (!cellsProp && spec.cellsOptional) {
                argc = 0;
            } else {
                r.problem = r.target->getFullPath() + " has no valid " + spec.cellsName + " property";
                out.push_back(r);
                return;
            }
        }
        if (i + 1 + argc > cells.size()) {
            r.problem = "is truncated: " + r.target->getFullPath() + " needs " + std::to_string(argc) +
                        " argument cell(s) after the phandle";
            out.push_back(r);
            return;
        }
        r.args.assign(cells.begin() + static_cast<std::ptrdiff_t>(i + 1),
                      cells.begin() + static_cast<std::ptrdiff_t>(i + 1 + argc));
        out.push_back(r);
        i += 1 + argc;
    }
}

bool isInSubtree(const std::shared_ptr<DeviceTreeNode>& node, const DeviceTreeNode* ancestor) {
    for (auto n = node; n; n = n->getParent()) {
        if (n.get() == ancestor) return true;
    }
    return false;
}

} // namespace

std::shared_ptr<DeviceTreeNode> DeviceTree::findNodeByPhandle(uint32_t phandle) const {
    auto map = buildPhandleMap(root_);
    auto it = map.find(phandle);
    return it == map.end() ? nullptr : it->second;
}

std::vector<PhandleReference> DeviceTree::references() const {
    std::vector<PhandleReference> out;
    if (!root_) return out;
    const auto phandles = buildPhandleMap(root_);
    std::function<void(const std::shared_ptr<DeviceTreeNode>&)> walk = [&](const std::shared_ptr<DeviceTreeNode>& n) {
        for (const auto& prop : n->getProperties()) {
            if (const auto* spec = specFor(prop.getName())) {
                decodeProperty(n, prop, *spec, phandles, out);
            }
        }
        for (const auto& c : n->getChildren()) walk(c);
    };
    walk(root_);
    return out;
}

std::vector<PhandleReference> DeviceTree::referencesTo(const std::shared_ptr<DeviceTreeNode>& node,
                                                       bool withDescendants) const {
    std::vector<PhandleReference> out;
    if (!node) return out;
    for (auto& r : references()) {
        if (!r.target) continue;
        if (withDescendants) {
            if (isInSubtree(r.target, node.get()) && !isInSubtree(r.source, node.get())) out.push_back(r);
        } else if (r.target == node) {
            out.push_back(r);
        }
    }
    return out;
}

std::vector<PhandleReference> DeviceTree::referencesIn(const std::shared_ptr<DeviceTreeNode>& node,
                                                       const std::string& property) const {
    std::vector<PhandleReference> out;
    if (!node) return out;
    const auto* prop = static_cast<const DeviceTreeNode&>(*node).findProperty(property);
    const auto* spec = specFor(property);
    if (prop && spec) {
        decodeProperty(node, *prop, *spec, buildPhandleMap(root_), out);
    }
    return out;
}

} // namespace DTE
