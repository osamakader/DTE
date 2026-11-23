#ifndef DTE_INTERRUPTS_H
#define DTE_INTERRUPTS_H

// Decoding of interrupts, following the kernel's of_irq_parse_raw(): the
// interrupt parent comes from `interrupt-parent` (inherited from ancestors,
// otherwise the nearest ancestor with #interrupt-cells); specifiers are
// #interrupt-cells long; interrupt nexus nodes are followed through
// `interrupt-map` / `interrupt-map-mask` until an `interrupt-controller`.

#include "DeviceTree.h"

#include <string>
#include <vector>

namespace DTE {

struct InterruptSpec {
    size_t index = 0;
    std::string name;                           // from interrupt-names, if any
    std::string property;                       // "interrupts" or "interrupts-extended"
    std::shared_ptr<DeviceTreeNode> controller; // the interrupt controller it ends up at
    std::vector<uint32_t> specifier;            // specifier at that controller
    std::vector<std::string> route;             // interrupt-map nexus nodes passed through
    std::string problem;                        // set if it could not be resolved
};

// The node's interrupt parent (the node whose #interrupt-cells applies to
// its `interrupts`), or nullptr.
std::shared_ptr<DeviceTreeNode> interruptParentOf(const DeviceTree& tree, const std::shared_ptr<DeviceTreeNode>& node);

// Decodes `interrupts` (or, if present, `interrupts-extended`) of `node`.
// Entries that cannot be resolved carry a `problem`. Returns false and
// sets `error` if the property itself cannot be split into specifiers.
bool decodeInterrupts(const DeviceTree& tree, const std::shared_ptr<DeviceTreeNode>& node,
                      std::vector<InterruptSpec>& out, std::string* error = nullptr);

// "SPI 33 (hwirq 65), level high" for ARM GICs, "line 5, edge falling" for
// two-cell controllers, otherwise the raw cells.
std::string describeSpecifier(const DeviceTreeNode& controller, const std::vector<uint32_t>& specifier);

// "/interrupt-controller@1000: SPI 33 (hwirq 65), level high [via /nexus@4000]"
// or the problem.
std::string describeInterrupt(const InterruptSpec& spec);

} // namespace DTE

#endif // DTE_INTERRUPTS_H
