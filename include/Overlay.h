#ifndef DTE_OVERLAY_H
#define DTE_OVERLAY_H

// Applying device tree overlays (compiled with dtc's /plugin/ syntax) to a
// base tree, following the semantics of libfdt's fdt_overlay_apply().

#include "DeviceTree.h"

#include <map>
#include <string>

namespace DTE {

// Label -> node path, as stored in a __symbols__ node.
using SymbolMap = std::map<std::string, std::string>;

// The entries of the tree's /__symbols__ node (empty if it has none).
SymbolMap symbolsOf(const DeviceTree& tree);

// True if the tree looks like an overlay: it has fragments with an
// __overlay__ node, or __fixups__ / __local_fixups__.
bool isOverlay(const DeviceTree& tree);

// Applies `overlay` to `base`:
//  1. the overlay's phandles are renumbered above the base's highest one,
//     and references inside the overlay (__local_fixups__) adjusted;
//  2. references to base labels (__fixups__) are resolved through the
//     base's __symbols__, or else through `baseSymbols` (e.g. from
//     baseSymbolsFor()). Labelled base nodes that have no phandle yet get
//     a new one;
//  3. each fragment's __overlay__ node is merged into its `target`
//     (phandle) or `target-path`: properties are added or replaced, child
//     nodes merged recursively;
//  4. the overlay's symbols are added, with their paths in the merged
//     tree, to the base's __symbols__ (if it has one) and to `baseSymbols`
//     (if given), so later overlays can refer to them.
// On failure `base` is left unchanged and `error` (if given) says why.
bool applyOverlay(DeviceTree& base, const DeviceTree& overlay, std::string* error = nullptr,
                  SymbolMap* baseSymbols = nullptr);

// Labels of a base tree loaded from `filename`: its own __symbols__ if it
// has them, otherwise, for a DTS source, those of a separate `dtc -@`
// compile of the same file (`options` gives include paths). Empty if
// neither is available.
SymbolMap baseSymbolsFor(const DeviceTree& base, const std::string& filename, const LoadOptions& options = {});

// Loads an overlay (.dtbo, or .dtso compiled with `dtc -@`). Returns false
// and sets `error` if it cannot be loaded or is not an overlay.
bool loadOverlay(DeviceTree& overlay, const std::string& filename, const LoadOptions& options = {},
                 std::string* error = nullptr);

// Writes an overlay source (/plugin/) that turns `base` into `target`:
// one `&{/path} { ... };` per existing node with added or changed
// properties or new child nodes (new subtrees are written whole).
// References to new nodes are written as labels (&n5), so dtc creates
// __local_fixups__ and they survive phandle renumbering; references to
// existing nodes keep the base's phandle numbers.
//
// Overlays cannot delete properties or nodes; such differences, and
// references that cannot be expressed, are reported in `warnings`.
std::string generateOverlay(const DeviceTree& base, const DeviceTree& target, std::vector<std::string>* warnings = nullptr);

} // namespace DTE

#endif // DTE_OVERLAY_H
