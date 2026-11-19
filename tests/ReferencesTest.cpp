#include "DeviceTree.h"
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <cstdlib>

using namespace DTE;

namespace {

using Cells = std::vector<uint32_t>;

std::string describe(const PhandleReference& r) {
    std::string s = r.source->getFullPath() + ":" + r.property + "[" + std::to_string(r.cellIndex) + "] -> ";
    s += r.target ? r.target->getFullPath() : std::string("?");
    for (auto a : r.args) s += " " + std::to_string(a);
    if (!r.problem.empty()) s += " !" + r.problem;
    return s;
}

std::vector<std::string> describeAll(const std::vector<PhandleReference>& refs) {
    std::vector<std::string> out;
    for (const auto& r : refs) out.push_back(describe(r));
    return out;
}

std::shared_ptr<DeviceTreeNode> node(const std::string& name) {
    return std::make_shared<DeviceTreeNode>(name);
}

} // namespace

#ifdef DTC_PATH
class ReferencesFromDts : public ::testing::Test {
protected:
    void SetUp() override {
        ::setenv("DTE_DTC", DTC_PATH, 1);
        ASSERT_TRUE(tree_.loadFromFile(std::string(TEST_DATA_DIR) + "/refs.dts")) << tree_.getLoadError();
    }
    void TearDown() override { ::unsetenv("DTE_DTC"); }
    DeviceTree tree_;
};

TEST_F(ReferencesFromDts, DecodesAllKnownProperties) {
    EXPECT_EQ(describeAll(tree_.references()), (std::vector<std::string>{
                                                   "/:interrupt-parent[0] -> /interrupt-controller@1000",
                                                   "/serial@4000:clocks[0] -> /clock-controller@2000 3",
                                                   "/serial@4000:clocks[2] -> /oscillator",
                                                   "/serial@4000:interrupts-extended[0] -> /interrupt-controller@1000 0 33 4",
                                                   "/serial@4000:pinctrl-0[0] -> /pinmux/uart",
                                                   "/serial@4000:reset-gpios[0] -> /gpio@3000 5 1",
                                                   "/serial@4000:reset-gpios[4] -> /gpio@3000 7 0",
                                               }));
}

TEST_F(ReferencesFromDts, FindNodeByPhandle) {
    auto gic = tree_.findNodeByPath("/interrupt-controller@1000");
    ASSERT_TRUE(gic);
    const uint32_t ph = gic->findProperty("phandle")->getValueAsCells()[0];
    EXPECT_EQ(tree_.findNodeByPhandle(ph), gic);
    EXPECT_EQ(tree_.findNodeByPhandle(0xdead), nullptr);
}

TEST_F(ReferencesFromDts, ReferencesTo) {
    auto gic = tree_.findNodeByPath("/interrupt-controller@1000");
    EXPECT_EQ(describeAll(tree_.referencesTo(gic)),
              (std::vector<std::string>{"/:interrupt-parent[0] -> /interrupt-controller@1000",
                                        "/serial@4000:interrupts-extended[0] -> /interrupt-controller@1000 0 33 4"}));

    // pinmux itself is not referenced, but its child is
    auto pinmux = tree_.findNodeByPath("/pinmux");
    EXPECT_TRUE(tree_.referencesTo(pinmux).empty());
    EXPECT_EQ(tree_.referencesTo(pinmux, true).size(), 1u);
}

TEST_F(ReferencesFromDts, ReferencesInOneProperty) {
    auto serial = tree_.findNodeByPath("/serial@4000");
    EXPECT_EQ(tree_.referencesIn(serial, "clocks").size(), 2u);
    EXPECT_TRUE(tree_.referencesIn(serial, "reg").empty());
    EXPECT_TRUE(tree_.referencesIn(serial, "clock-names").empty());
    // nr-gpios is a count, not a reference
    EXPECT_TRUE(tree_.referencesIn(tree_.findNodeByPath("/gpio@3000"), "nr-gpios").empty());
}
#endif

class BrokenReferences : public ::testing::Test {
protected:
    std::shared_ptr<DeviceTreeNode> root = node("/");
    std::shared_ptr<DeviceTreeNode> clk = node("clk");
    std::shared_ptr<DeviceTreeNode> dev = node("dev");
    DeviceTree tree;

    void SetUp() override {
        clk->addProperty(DeviceTreeProperty("phandle", Cells{1}));
        clk->addProperty(DeviceTreeProperty("#clock-cells", Cells{1}));
        root->addChild(clk);
        root->addChild(dev);
        tree.setRoot(root);
    }

    std::vector<std::string> refsOf(const std::string& prop, PropertyValue v) {
        dev->addProperty(DeviceTreeProperty(prop, std::move(v)));
        return describeAll(tree.referencesIn(dev, prop));
    }
};

TEST_F(BrokenReferences, UnknownPhandleStopsDecoding) {
    EXPECT_EQ(refsOf("clocks", Cells{1, 0, 7, 0, 1, 2}),
              (std::vector<std::string>{"/dev:clocks[0] -> /clk 0",
                                        "/dev:clocks[2] -> ? !refers to phandle 0x7, which no node has"}));
}

TEST_F(BrokenReferences, MissingCellsPropertyOnTarget) {
    clk->removeProperty("#clock-cells");
    EXPECT_EQ(refsOf("clocks", Cells{1, 0}),
              (std::vector<std::string>{"/dev:clocks[0] -> /clk !/clk has no valid #clock-cells property"}));
}

TEST_F(BrokenReferences, OptionalCellsDefaultToZero) {
    EXPECT_EQ(refsOf("msi-parent", Cells{1}), (std::vector<std::string>{"/dev:msi-parent[0] -> /clk"}));
}

TEST_F(BrokenReferences, TruncatedSpecifier) {
    EXPECT_EQ(refsOf("clocks", Cells{1}),
              (std::vector<std::string>{
                  "/dev:clocks[0] -> /clk !is truncated: /clk needs 1 argument cell(s) after the phandle"}));
}

TEST_F(BrokenReferences, SinglePhandleProperties) {
    EXPECT_EQ(refsOf("interrupt-parent", Cells{1, 1}),
              (std::vector<std::string>{"/dev:interrupt-parent[0] -> ? !must be a single phandle"}));
}

TEST_F(BrokenReferences, NonCellValue) {
    EXPECT_EQ(refsOf("clocks", std::string("clk")),
              (std::vector<std::string>{"/dev:clocks[0] -> ? !must be a list of 32-bit cells holding phandles"}));
}

TEST_F(BrokenReferences, UnknownPropertiesAreNotReferences) {
    dev->addProperty(DeviceTreeProperty("vendor,thing", Cells{1}));
    dev->addProperty(DeviceTreeProperty("ngpios", Cells{1}));
    EXPECT_TRUE(tree.references().empty());
}
