#include "Overlay.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <stdexcept>

namespace DTE {

namespace {

const char* const kSpecialNodes[] = {"__symbols__", "__fixups__", "__local_fixups__"};

bool isSpecial(const std::string& name) {
    return std::find(std::begin(kSpecialNodes), std::end(kSpecialNodes), name) != std::end(kSpecialNodes);
}

[[noreturn]] void fail(const std::string& msg) {
    throw std::runtime_error(msg);
}

std::shared_ptr<DeviceTreeNode> findPath(const std::shared_ptr<DeviceTreeNode>& root, const std::string& path) {
    return root->findNodeByPath(path);
}

std::vector<uint32_t> cellsOf(const DeviceTreeNode& node, const std::string& prop, const std::string& context) {
    const auto* p = node.findProperty(prop);
    if (!p) fail(context + ": property '" + prop + "' not found");
    if (p->isEmpty()) return {};
    if (!p->isCells()) fail(context + ": property '" + prop + "' is not a list of cells");
    return p->getValueAsCells();
}

// Highest phandle in the tree (0 if none), ignoring the special nodes.
uint32_t maxPhandle(const std::shared_ptr<DeviceTreeNode>& root) {
    uint32_t max = 0;
    std::function<void(const std::shared_ptr<DeviceTreeNode>&)> walk = [&](const std::shared_ptr<DeviceTreeNode>& n) {
        for (const char* name : {"phandle", "linux,phandle"}) {
            const auto* p = static_cast<const DeviceTreeNode&>(*n).findProperty(name);
            if (p && p->isCells() && p->getValueAsCells().size() == 1) {
                const uint32_t v = p->getValueAsCells()[0];
                if (v != 0xffffffff) max = std::max(max, v);
            }
        }
        for (const auto& c : n->getChildren()) {
            if (!(n->getParent() == nullptr && isSpecial(c->getName()))) walk(c);
        }
    };
    walk(root);
    return max;
}

// Step 1: renumber the overlay's own phandles by `delta`.
void adjustPhandles(const std::shared_ptr<DeviceTreeNode>& root, uint32_t delta) {
    std::function<void(const std::shared_ptr<DeviceTreeNode>&)> walk = [&](const std::shared_ptr<DeviceTreeNode>& n) {
        for (const char* name : {"phandle", "linux,phandle"}) {
            const auto* p = static_cast<const DeviceTreeNode&>(*n).findProperty(name);
            if (!p) continue;
            if (!p->isCells() || p->getValueAsCells().size() != 1) {
                fail(n->getFullPath() + ": invalid " + name + " property");
            }
            const uint32_t v = p->getValueAsCells()[0];
            n->addProperty(DeviceTreeProperty(name, std::vector<uint32_t>{v + delta}));
        }
        for (const auto& c : n->getChildren()) {
            if (!(n->getParent() == nullptr && isSpecial(c->getName()))) walk(c);
        }
    };
    walk(root);
}

// Step 1b: __local_fixups__ mirrors the overlay tree; each property lists
// byte offsets of cells holding overlay-local phandles.
void applyLocalFixups(const std::shared_ptr<DeviceTreeNode>& overlayRoot, uint32_t delta) {
    auto fixups = overlayRoot->findChild("__local_fixups__");
    if (!fixups || delta == 0) return;

    std::function<void(const std::shared_ptr<DeviceTreeNode>&, const std::shared_ptr<DeviceTreeNode>&)> walk =
        [&](const std::shared_ptr<DeviceTreeNode>& fixupNode, const std::shared_ptr<DeviceTreeNode>& target) {
            for (const auto& prop : fixupNode->getProperties()) {
                const std::string context = "__local_fixups__ for " + target->getFullPath();
                auto cells = cellsOf(*target, prop.getName(), context);
                if (!prop.isCells()) fail(context + ": offsets of '" + prop.getName() + "' are not cells");
                for (uint32_t offset : prop.getValueAsCells()) {
                    if (offset % 4 != 0 || offset / 4 >= cells.size()) {
                        fail(context + ": bad offset " + std::to_string(offset) + " in '" + prop.getName() + "'");
                    }
                    cells[offset / 4] += delta;
                }
                target->addProperty(DeviceTreeProperty(prop.getName(), cells));
            }
            for (const auto& child : fixupNode->getChildren()) {
                auto targetChild = target->findChild(child->getName());
                if (!targetChild) {
                    fail("__local_fixups__ refers to missing node " + target->getFullPath() + "/" + child->getName());
                }
                walk(child, targetChild);
            }
        };
    walk(fixups, overlayRoot);
}

// Step 2: resolve references to base labels. Each __fixups__ property is a
// label whose value lists "path:property:offset" locations in the overlay.
void applyFixups(const std::shared_ptr<DeviceTreeNode>& overlayRoot, const std::shared_ptr<DeviceTreeNode>& baseRoot,
                 const SymbolMap& symbols, bool haveSymbols, uint32_t& nextPhandle) {
    auto fixups = overlayRoot->findChild("__fixups__");
    if (!fixups) return;

    for (const auto& prop : fixups->getProperties()) {
        const std::string& label = prop.getName();
        auto sym = symbols.find(label);
        if (sym == symbols.end()) {
            fail("the overlay refers to label '" + label + "', which the base does not define" +
                 (haveSymbols ? std::string()
                              : " (the base has no __symbols__ node: compile it with `dtc -@`, or load it from DTS)"));
        }
        auto baseNode = findPath(baseRoot, sym->second);
        if (!baseNode) fail("label '" + label + "' points to " + sym->second + ", which is not in the base");

        // Labelled nodes compiled without -@ may have no phandle yet
        uint32_t phandle = 0;
        const auto* ph = static_cast<const DeviceTreeNode&>(*baseNode).findProperty("phandle");
        if (!ph) ph = static_cast<const DeviceTreeNode&>(*baseNode).findProperty("linux,phandle");
        if (ph && ph->isCells() && ph->getValueAsCells().size() == 1) {
            phandle = ph->getValueAsCells()[0];
        } else {
            phandle = nextPhandle++;
            baseNode->addProperty(DeviceTreeProperty("phandle", std::vector<uint32_t>{phandle}));
        }

        for (const auto& location : prop.getValueAsStrings()) {
            const auto last = location.rfind(':');
            const auto mid = last == std::string::npos ? std::string::npos : location.rfind(':', last - 1);
            if (mid == std::string::npos) fail("malformed __fixups__ entry '" + location + "' for label '" + label + "'");
            const std::string path = location.substr(0, mid);
            const std::string propName = location.substr(mid + 1, last - mid - 1);
            const unsigned long offset = std::strtoul(location.c_str() + last + 1, nullptr, 10);

            auto node = findPath(overlayRoot, path);
            if (!node) fail("__fixups__ entry '" + location + "' refers to a missing overlay node");
            auto cells = cellsOf(*node, propName, "__fixups__ entry '" + location + "'");
            if (offset % 4 != 0 || offset / 4 >= cells.size()) {
                fail("__fixups__ entry '" + location + "' has a bad offset");
            }
            cells[offset / 4] = phandle;
            node->addProperty(DeviceTreeProperty(propName, cells));
        }
    }
}

// Merges `src` (an __overlay__ node or one of its children) into `dst`.
void merge(const std::shared_ptr<DeviceTreeNode>& src, const std::shared_ptr<DeviceTreeNode>& dst) {
    for (const auto& prop : src->getProperties()) {
        dst->addProperty(prop);
    }
    for (const auto& child : src->getChildren()) {
        auto existing = dst->findChild(child->getName());
        if (!existing) {
            existing = std::make_shared<DeviceTreeNode>(child->getName());
            dst->addChild(existing);
        }
        merge(child, existing);
    }
}

std::shared_ptr<DeviceTreeNode> fragmentTarget(const std::shared_ptr<DeviceTreeNode>& fragment,
                                               const std::shared_ptr<DeviceTreeNode>& baseRoot) {
    const auto& f = static_cast<const DeviceTreeNode&>(*fragment);
    if (const auto* target = f.findProperty("target")) {
        if (!target->isCells() || target->getValueAsCells().size() != 1) {
            fail(fragment->getFullPath() + ": 'target' must be a single phandle");
        }
        const uint32_t ph = target->getValueAsCells()[0];
        DeviceTree lookup;
        lookup.setRoot(baseRoot);
        auto node = lookup.findNodeByPhandle(ph);
        if (!node) fail(fragment->getFullPath() + ": target phandle " + std::to_string(ph) + " is not in the base");
        return node;
    }
    if (const auto* targetPath = f.findProperty("target-path")) {
        if (!targetPath->isString() || targetPath->isEmpty()) {
            fail(fragment->getFullPath() + ": 'target-path' must be a path string");
        }
        const auto& path = std::get<std::string>(targetPath->getValue());
        auto node = findPath(baseRoot, path);
        if (!node) fail(fragment->getFullPath() + ": target-path " + path + " is not in the base");
        return node;
    }
    fail(fragment->getFullPath() + ": fragment has neither 'target' nor 'target-path'");
}

// Step 4: overlay symbols point into fragments
// ("/fragment@0/__overlay__/dev"); rewrite them to the merged location.
void mergeSymbols(const std::shared_ptr<DeviceTreeNode>& overlayRoot, const std::shared_ptr<DeviceTreeNode>& baseRoot,
                  const std::map<std::string, std::string>& fragmentTargets) {
    auto overlaySymbols = overlayRoot->findChild("__symbols__");
    auto baseSymbols = baseRoot->findChild("__symbols__");
    if (!overlaySymbols || !baseSymbols) return;

    for (const auto& prop : overlaySymbols->getProperties()) {
        if (!prop.isString()) continue;
        const auto& path = std::get<std::string>(prop.getValue());
        const auto marker = path.find("/__overlay__");
        if (marker == std::string::npos) continue;
        auto target = fragmentTargets.find(path.substr(0, marker));
        if (target == fragmentTargets.end()) continue;
        const std::string rest = path.substr(marker + std::string("/__overlay__").size());
        std::string merged = target->second == "/" ? rest : target->second + rest;
        if (merged.empty()) merged = "/";
        baseSymbols->addProperty(DeviceTreeProperty(prop.getName(), merged));
    }
}

} // namespace

SymbolMap symbolsOf(const DeviceTree& tree) {
    SymbolMap out;
    auto symbols = tree.findNodeByPath("/__symbols__");
    if (!symbols) return out;
    for (const auto& prop : symbols->getProperties()) {
        if (prop.isString() && !prop.isEmpty()) out[prop.getName()] = std::get<std::string>(prop.getValue());
    }
    return out;
}

bool isOverlay(const DeviceTree& tree) {
    auto root = tree.getRoot();
    if (!root) return false;
    for (const auto& child : root->getChildren()) {
        if (child->getName() == "__fixups__" || child->getName() == "__local_fixups__") return true;
        if (child->findChild("__overlay__")) return true;
    }
    return false;
}

bool applyOverlay(DeviceTree& base, const DeviceTree& overlay, std::string* error, const SymbolMap* baseSymbols) {
    try {
        if (!base.getRoot()) fail("the base tree is empty");
        if (!overlay.getRoot() || !isOverlay(overlay)) {
            fail("not an overlay (no fragments with __overlay__ nodes); compile it from a /plugin/ source");
        }

        // Work on copies so a failure leaves the base untouched
        auto work = base.getRoot()->clone();
        auto ov = overlay.getRoot()->clone();

        const uint32_t delta = maxPhandle(work);
        adjustPhandles(ov, delta);
        applyLocalFixups(ov, delta);

        const bool haveBaseSymbols = base.findNodeByPath("/__symbols__") != nullptr;
        const SymbolMap symbols = haveBaseSymbols ? symbolsOf(base) : (baseSymbols ? *baseSymbols : SymbolMap{});
        uint32_t nextPhandle = std::max(maxPhandle(work), maxPhandle(ov)) + 1;
        applyFixups(ov, work, symbols, haveBaseSymbols || baseSymbols, nextPhandle);

        std::map<std::string, std::string> fragmentTargets; // "/fragment@0" -> target path
        for (const auto& fragment : ov->getChildren()) {
            if (isSpecial(fragment->getName())) continue;
            auto content = fragment->findChild("__overlay__");
            if (!content) continue; // like libfdt, ignore nodes that are not fragments
            auto target = fragmentTarget(fragment, work);
            fragmentTargets[fragment->getFullPath()] = target->getFullPath();
            merge(content, target);
        }

        mergeSymbols(ov, work, fragmentTargets);
        base.setRoot(work);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

} // namespace DTE
