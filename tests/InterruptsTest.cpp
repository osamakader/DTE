#include "Interrupts.h"

#include <gtest/gtest.h>

#include <cstdlib>

using namespace DTE;

namespace {

using Cells = std::vector<uint32_t>;

std::vector<std::string> irqsOf(const DeviceTree& tree, const std::shared_ptr<DeviceTreeNode>& n) {
    std::vector<InterruptSpec> specs;
    std::string error;
    if (!decodeInterrupts(tree, n, specs, &error)) return {"error: " + error};
    std::vector<std::string> out;
    for (const auto& s : specs) out.push_back(describeInterrupt(s));
    return out;
}

std::shared_ptr<DeviceTreeNode> node(const std::string& name) {
    return std::make_shared<DeviceTreeNode>(name);
}

} // namespace

#ifdef DTC_PATH
class InterruptsFromDts : public ::testing::Test {
protected:
    void SetUp() override {
        ::setenv("DTE_DTC", DTC_PATH, 1);
        ASSERT_TRUE(tree_.loadFromFile(std::string(TEST_DATA_DIR) + "/interrupts.dts")) << tree_.getLoadError();
    }
    void TearDown() override { ::unsetenv("DTE_DTC"); }
    std::vector<std::string> irqs(const std::string& path) {
        auto n = tree_.findNodeByPath(path);
        EXPECT_TRUE(n) << path;
        return n ? irqsOf(tree_, n) : std::vector<std::string>{};
    }
    DeviceTree tree_;
};

TEST_F(InterruptsFromDts, InheritedInterruptParentAndGicDecoding) {
    EXPECT_EQ(irqs("/serial@3000"),
              (std::vector<std::string>{"/interrupt-controller@1000 SPI 33 (hwirq 65), level high"}));
    // A controller's own interrupts go to its parent
    EXPECT_EQ(irqs("/gpio@2000"),
              (std::vector<std::string>{"/interrupt-controller@1000 SPI 10 (hwirq 42), level high"}));
}

TEST_F(InterruptsFromDts, InterruptNames) {
    EXPECT_EQ(irqs("/dual@3100"), (std::vector<std::string>{
                                      "tx: /interrupt-controller@1000 SPI 34 (hwirq 66), level high",
                                      "rx: /interrupt-controller@1000 SPI 35 (hwirq 67), edge rising",
                                  }));
}

TEST_F(InterruptsFromDts, ExplicitInterruptParentWithTwoCells) {
    EXPECT_EQ(irqs("/button"), (std::vector<std::string>{"/gpio@2000 line 5, edge falling"}));
    EXPECT_EQ(interruptParentOf(tree_, tree_.findNodeByPath("/button"))->getFullPath(), "/gpio@2000");
}

TEST_F(InterruptsFromDts, InterruptsExtended) {
    EXPECT_EQ(irqs("/ext@3200"), (std::vector<std::string>{
                                     "/interrupt-controller@1000 PPI 9 (hwirq 25), level high, CPU mask 0xf",
                                     "/gpio@2000 line 7, level low",
                                 }));
}

TEST_F(InterruptsFromDts, InterruptMapNexusWithMask) {
    EXPECT_EQ(irqs("/nexus@4000/dev@1"),
              (std::vector<std::string>{"/interrupt-controller@1000 SPI 50 (hwirq 82), level high via /nexus@4000"}));
    // unit 0x12 & 0xf = 0x2, specifier 0x9 & 0x7 = 0x1 -> second entry
    EXPECT_EQ(irqs("/nexus@4000/dev@12"),
              (std::vector<std::string>{"/interrupt-controller@1000 SPI 51 (hwirq 83), level high via /nexus@4000"}));
    EXPECT_EQ(interruptParentOf(tree_, tree_.findNodeByPath("/nexus@4000/dev@1"))->getFullPath(), "/nexus@4000");
}

TEST_F(InterruptsFromDts, NodesWithoutInterrupts) {
    EXPECT_EQ(irqs("/interrupt-controller@1000"),
              (std::vector<std::string>{"error: no 'interrupts' or 'interrupts-extended' property"}));
}
#endif

// Broken setups, built in code
class BrokenInterrupts : public ::testing::Test {
protected:
    std::shared_ptr<DeviceTreeNode> intc = node("intc");
    std::shared_ptr<DeviceTreeNode> nexus = node("nexus");
    std::shared_ptr<DeviceTreeNode> dev = node("dev");
    DeviceTree tree;

    void SetUp() override {
        intc->addProperty(DeviceTreeProperty("interrupt-controller", std::string()));
        intc->addProperty(DeviceTreeProperty("#interrupt-cells", Cells{2}));
        intc->addProperty(DeviceTreeProperty("phandle", Cells{1}));
        nexus->addProperty(DeviceTreeProperty("#interrupt-cells", Cells{1}));
        nexus->addProperty(DeviceTreeProperty("#address-cells", Cells{0}));
        nexus->addProperty(DeviceTreeProperty("interrupt-map", Cells{1, 1, 5, 4}));
        nexus->addProperty(DeviceTreeProperty("phandle", Cells{2}));
        tree.getRoot()->addChild(intc);
        tree.getRoot()->addChild(nexus);
        tree.getRoot()->addChild(dev);
    }
};

TEST_F(BrokenInterrupts, NoInterruptParent) {
    dev->addProperty(DeviceTreeProperty("interrupts", Cells{1, 2}));
    EXPECT_EQ(irqsOf(tree, dev), (std::vector<std::string>{
                                     "error: no interrupt parent found (no interrupt-parent and no ancestor with "
                                     "#interrupt-cells)"}));
}

TEST_F(BrokenInterrupts, WrongLength) {
    dev->addProperty(DeviceTreeProperty("interrupt-parent", Cells{1}));
    dev->addProperty(DeviceTreeProperty("interrupts", Cells{1, 2, 3}));
    EXPECT_EQ(irqsOf(tree, dev), (std::vector<std::string>{
                                     "error: 'interrupts' has 3 cells, which is not a multiple of #interrupt-cells = 2 "
                                     "of /intc"}));
}

TEST_F(BrokenInterrupts, NexusWithoutMatch) {
    dev->addProperty(DeviceTreeProperty("interrupt-parent", Cells{2}));
    dev->addProperty(DeviceTreeProperty("interrupts", Cells{1, 7}));
    EXPECT_EQ(irqsOf(tree, dev), (std::vector<std::string>{
                                     "/intc line 5, level high via /nexus",
                                     "(no interrupt-map entry of /nexus matches <0x7>)",
                                 }));
}

TEST_F(BrokenInterrupts, ParentThatIsNeitherControllerNorNexus) {
    nexus->removeProperty("interrupt-map");
    dev->addProperty(DeviceTreeProperty("interrupt-parent", Cells{2}));
    dev->addProperty(DeviceTreeProperty("interrupts", Cells{1}));
    EXPECT_EQ(irqsOf(tree, dev),
              (std::vector<std::string>{"(/nexus is neither an interrupt-controller nor has an interrupt-map)"}));
}

TEST_F(BrokenInterrupts, NexusLoopIsCaught) {
    // nexus maps to itself
    nexus->addProperty(DeviceTreeProperty("interrupt-map", Cells{1, 2, 1}));
    dev->addProperty(DeviceTreeProperty("interrupt-parent", Cells{2}));
    dev->addProperty(DeviceTreeProperty("interrupts", Cells{1}));
    EXPECT_EQ(irqsOf(tree, dev), (std::vector<std::string>{"(interrupt-map nesting is too deep (loop?))"}));
}

TEST(InterruptDescriptions, FallsBackToRawCells) {
    auto c = node("c");
    EXPECT_EQ(describeSpecifier(*c, {1, 2, 3}), "<0x1 0x2 0x3>");
    EXPECT_EQ(describeSpecifier(*c, {7}), "line 7");
    EXPECT_EQ(describeSpecifier(*c, {7, 0x123}), "<0x7 0x123>"); // flags not a plain trigger type
}
