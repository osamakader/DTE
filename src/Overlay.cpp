#include "Overlay.h"
#include "DeviceTreeParser.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <cstdio>
#include <sstream>
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
                  const std::map<std::string, std::string>& fragmentTargets, SymbolMap* extra) {
    auto overlaySymbols = overlayRoot->findChild("__symbols__");
    auto baseSymbols = baseRoot->findChild("__symbols__");
    if (!overlaySymbols || (!baseSymbols && !extra)) return;

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
        if (baseSymbols) baseSymbols->addProperty(DeviceTreeProperty(prop.getName(), merged));
        if (extra) (*extra)[prop.getName()] = merged;
    }
}

} // namespace

namespace {

// State for generateOverlay()
class OverlayWriter {
public:
    OverlayWriter(const DeviceTree& base, const DeviceTree& target, std::vector<std::string>& warnings)
        : base_(base), target_(target), warnings_(warnings) {
        // References whose target node is new (not in the base) become
        // label references; remember the cells to replace.
        for (const auto& ref : target.references()) {
            if (!ref.problem.empty() || !ref.target) continue;
            const std::string targetPath = ref.target->getFullPath();
            if (!base.findNodeByPath(targetPath)) {
                labelCells_[{ref.source.get(), ref.property}][ref.cellIndex] = labelFor(ref.target);
            } else {
                checkExistingReference(ref);
            }
        }
    }

    std::string run() {
        out_ << "/dts-v1/;\n/plugin/;\n";
        visit(target_.getRoot(), base_.getRoot());
        return out_.str();
    }

private:
    const DeviceTree& base_;
    const DeviceTree& target_;
    std::vector<std::string>& warnings_;
    std::ostringstream out_;
    std::map<std::pair<const DeviceTreeNode*, std::string>, std::map<size_t, std::string>> labelCells_;
    std::map<const DeviceTreeNode*, std::string> labels_;

    static bool isPhandleProp(const std::string& name) { return name == "phandle" || name == "linux,phandle"; }

    std::string labelFor(const std::shared_ptr<DeviceTreeNode>& node) {
        auto it = labels_.find(node.get());
        if (it != labels_.end()) return it->second;
        const auto* ph = static_cast<const DeviceTreeNode&>(*node).findProperty("phandle");
        char buf[24];
        std::snprintf(buf, sizeof(buf), "n%x", ph && ph->isCells() && !ph->getValueAsCells().empty()
                                                     ? ph->getValueAsCells()[0]
                                                     : static_cast<unsigned>(labels_.size()));
        return labels_[node.get()] = buf;
    }

    // A reference to an existing node keeps its number, which must be the
    // base node's phandle.
    void checkExistingReference(const PhandleReference& ref) {
        auto baseNode = base_.findNodeByPath(ref.target->getFullPath());
        const auto* ph = static_cast<const DeviceTreeNode&>(*baseNode).findProperty("phandle");
        if (!ph) ph = static_cast<const DeviceTreeNode&>(*baseNode).findProperty("linux,phandle");
        const bool same = ph && ph->isCells() && ph->getValueAsCells() == std::vector<uint32_t>{ref.phandle};
        if (!same && changedOrNew(ref)) {
            warnings_.push_back(ref.source->getFullPath() + ":" + ref.property + " refers to " +
                                ref.target->getFullPath() + ", which has " +
                                (ph ? std::string("a different phandle") : std::string("no phandle")) +
                                " in the base; the reference is written as a plain number");
        }
    }

    // Only references in properties the overlay writes matter
    bool changedOrNew(const PhandleReference& ref) const {
        auto baseNode = base_.findNodeByPath(ref.source->getFullPath());
        if (!baseNode) return true;
        const auto* before = static_cast<const DeviceTreeNode&>(*baseNode).findProperty(ref.property);
        const auto* after = static_cast<const DeviceTreeNode&>(*ref.source).findProperty(ref.property);
        return !before || !after || !(*before == *after);
    }

    std::string valueText(const DeviceTreeNode& node, const DeviceTreeProperty& prop) const {
        auto it = labelCells_.find({&node, prop.getName()});
        if (it == labelCells_.end() || !prop.isCells()) return prop.getValueAsString();
        std::ostringstream os;
        os << "<";
        const auto cells = prop.getValueAsCells();
        for (size_t i = 0; i < cells.size(); ++i) {
            if (i) os << " ";
            auto label = it->second.find(i);
            if (label != it->second.end()) {
                os << "&" << label->second;
            } else {
                os << "0x" << std::hex << cells[i] << std::dec;
            }
        }
        os << ">";
        return os.str();
    }

    void writeProperty(const DeviceTreeNode& node, const DeviceTreeProperty& prop, const std::string& indent) {
        out_ << indent << prop.getName();
        if (!prop.isEmpty()) out_ << " = " << valueText(node, prop);
        out_ << ";\n";
    }

    void writeNewNode(const std::shared_ptr<DeviceTreeNode>& node, const std::string& indent) {
        out_ << indent;
        if (auto it = labels_.find(node.get()); it != labels_.end()) out_ << it->second << ": ";
        out_ << node->getName() << " {\n";
        for (const auto& prop : node->getProperties()) {
            if (isPhandleProp(prop.getName())) continue; // dtc assigns new ones
            writeProperty(*node, prop, indent + "\t");
        }
        for (const auto& child : node->getChildren()) writeNewNode(child, indent + "\t");
        out_ << indent << "};\n";
    }

    void visit(const std::shared_ptr<DeviceTreeNode>& target, const std::shared_ptr<DeviceTreeNode>& base) {
        const auto& t = static_cast<const DeviceTreeNode&>(*target);
        const auto& b = static_cast<const DeviceTreeNode&>(*base);
        const std::string path = target->getFullPath();

        std::vector<const DeviceTreeProperty*> changed;
        for (const auto& prop : t.getProperties()) {
            const auto* old = b.findProperty(prop.getName());
            if (old && *old == prop) continue;
            if (isPhandleProp(prop.getName())) {
                warnings_.push_back(path + ": phandle changed or added; overlays cannot set it for existing nodes");
                continue;
            }
            changed.push_back(&prop);
        }
        for (const auto& prop : b.getProperties()) {
            if (!t.findProperty(prop.getName())) {
                warnings_.push_back(path + ":" + prop.getName() + " was removed; overlays cannot delete properties");
            }
        }
        std::vector<std::shared_ptr<DeviceTreeNode>> added;
        for (const auto& child : t.getChildren()) {
            if (!b.findChild(child->getName())) added.push_back(child);
        }
        for (const auto& child : b.getChildren()) {
            if (!t.findChild(child->getName())) {
                warnings_.push_back(child->getFullPath() + " was removed; overlays cannot delete nodes");
            }
        }

        if (!changed.empty() || !added.empty()) {
            out_ << "\n&{" << path << "} {\n";
            for (const auto* prop : changed) writeProperty(t, *prop, "\t");
            for (const auto& child : added) {
                if (!changed.empty() || child != added.front()) out_ << "\n";
                writeNewNode(child, "\t");
            }
            out_ << "};\n";
        }

        for (const auto& child : t.getChildren()) {
            if (auto baseChild = b.findChild(child->getName())) visit(child, baseChild);
        }
    }
};

} // namespace

std::string generateOverlay(const DeviceTree& base, const DeviceTree& target, std::vector<std::string>* warnings) {
    std::vector<std::string> local;
    auto& w = warnings ? *warnings : local;
    if (!base.getRoot() || !target.getRoot()) return "/dts-v1/;\n/plugin/;\n";
    return OverlayWriter(base, target, w).run();
}

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

bool applyOverlay(DeviceTree& base, const DeviceTree& overlay, std::string* error, SymbolMap* baseSymbols) {
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

        mergeSymbols(ov, work, fragmentTargets, baseSymbols);
        base.setRoot(work);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

SymbolMap baseSymbolsFor(const DeviceTree& base, const std::string& filename, const LoadOptions& options) {
    if (base.findNodeByPath("/__symbols__")) return symbolsOf(base);
    if (filename.empty() || !DTSParser().canParse(filename)) return {};
    DeviceTree withSymbols;
    LoadOptions o = options;
    o.symbols = true;
    if (!withSymbols.loadFromFile(filename, o)) return {};
    return symbolsOf(withSymbols);
}

bool loadOverlay(DeviceTree& overlay, const std::string& filename, const LoadOptions& options, std::string* error) {
    LoadOptions o = options;
    o.symbols = true; // so labels defined by the overlay can be exported
    DeviceTree loaded;
    if (!loaded.loadFromFile(filename, o)) {
        if (error) *error = loaded.getLoadError();
        return false;
    }
    if (!isOverlay(loaded)) {
        if (error) *error = filename + " is not an overlay (compile overlay sources with /plugin/;)";
        return false;
    }
    overlay = loaded;
    return true;
}

} // namespace DTE
