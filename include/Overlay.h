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
//     base's __symbols__, or through `baseSymbols` when given (e.g. from a
//     separate `dtc -@` compile of a DTS base). Labelled base nodes that
//     have no phandle yet get a new one;
//  3. each fragment's __overlay__ node is merged into its `target`
//     (phandle) or `target-path`: properties are added or replaced, child
//     nodes merged recursively;
//  4. if the base has __symbols__, the overlay's symbols are added with
//     their paths in the merged tree.
// On failure `base` is left unchanged and `error` (if given) says why.
bool applyOverlay(DeviceTree& base, const DeviceTree& overlay, std::string* error = nullptr,
                  const SymbolMap* baseSymbols = nullptr);

} // namespace DTE

#endif // DTE_OVERLAY_H
