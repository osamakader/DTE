#include "DeviceTree.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace DTE;

namespace {

using Cells = std::vector<uint32_t>;

std::shared_ptr<DeviceTreeNode> node(const std::string& name) {
    return std::make_shared<DeviceTreeNode>(name);
}

void set(const std::shared_ptr<DeviceTreeNode>& n, const std::string& name, PropertyValue v) {
    n->addProperty(DeviceTreeProperty(name, std::move(v)));
}

// A tree that passes every check; tests break one thing at a time.
struct Fixture {
    std::shared_ptr<DeviceTreeNode> root = node("/");
    std::shared_ptr<DeviceTreeNode> soc = node("soc");
    std::shared_ptr<DeviceTreeNode> uart = node("serial@10000000");
    std::shared_ptr<DeviceTreeNode> intc = node("interrupt-controller@1000");
    std::shared_ptr<DeviceTreeNode> aliases = node("aliases");
    DeviceTree tree;

    Fixture() {
        set(root, "compatible", std::string("acme,board"));
        set(root, "model", std::string("ACME"));
        set(root, "#address-cells", Cells{1});
        set(root, "#size-cells", Cells{1});
        set(root, "interrupt-parent", Cells{1});
        set(soc, "#address-cells", Cells{1});
        set(soc, "#size-cells", Cells{1});
        set(soc, "ranges", std::string());
        set(intc, "reg", Cells{0x1000, 0x100});
        set(intc, "phandle", Cells{1});
        set(uart, "compatible", StringList{"ns16550a", "acme,uart"});
        set(uart, "reg", Cells{0x10000000, 0x1000});
        set(uart, "status", std::string("okay"));
        set(aliases, "intc0", std::string("/soc/interrupt-controller@1000"));
        root->addChild(aliases);
        root->addChild(soc);
        soc->addChild(intc);
        soc->addChild(uart);
        tree.setRoot(root);
    }

    std::vector<std::string> issues() const {
        std::vector<std::string> out;
        for (const auto& i : tree.check()) out.push_back(i.toString());
        return out;
    }
};

} // namespace

TEST(Validation, CleanTreeHasNoIssues) {
    Fixture f;
    EXPECT_EQ(f.issues(), std::vector<std::string>{});
    EXPECT_TRUE(f.tree.validate());
}

TEST(Validation, MissingRootPropertiesAreWarnings) {
    Fixture f;
    f.root->removeProperty("model");
    f.root->removeProperty("#size-cells");
    EXPECT_EQ(f.issues(), (std::vector<std::string>{
                              "warning: /: root node is missing the required 'model' property",
                              "warning: /: root node is missing the required '#size-cells' property",
                          }));
    EXPECT_TRUE(f.tree.validate()); // warnings only
}

TEST(Validation, RegLengthMustMatchCellCounts) {
    Fixture f;
    set(f.uart, "reg", Cells{0x10000000, 0x1000, 0x20000000});
    EXPECT_EQ(f.issues(), (std::vector<std::string>{
                              "error: /soc/serial@10000000:reg: has 3 cells, which is not a multiple of the "
                              "parent's #address-cells + #size-cells (1 + 1)",
                          }));
    EXPECT_FALSE(f.tree.validate());
}

TEST(Validation, RegMustBeCells) {
    Fixture f;
    set(f.uart, "reg", std::vector<uint8_t>{1, 2, 3});
    EXPECT_EQ(f.issues(), (std::vector<std::string>{"error: /soc/serial@10000000:reg: must be a list of 32-bit cells"}));
}

TEST(Validation, UnitAddressMustMatchReg) {
    Fixture f;
    f.uart->setName("serial@10001000");
    EXPECT_EQ(f.issues(), (std::vector<std::string>{
                              "warning: /soc/serial@10001000: unit address '10001000' does not match the first "
                              "reg address (0x10000000)",
                          }));
    // Case does not matter
    f.uart->setName("serial@1000000A");
    set(f.uart, "reg", Cells{0x1000000a, 0x10});
    EXPECT_EQ(f.issues(), std::vector<std::string>{});
}

TEST(Validation, UnitAddressAndRegMustComeTogether) {
    Fixture f;
    f.uart->setName("serial");
    f.intc->removeProperty("reg");
    EXPECT_EQ(f.issues(), (std::vector<std::string>{
                              "warning: /soc/interrupt-controller@1000: node has a unit address but no 'reg' property",
                              "warning: /soc/serial: node has a 'reg' property but no unit address (expected "
                              "name@10000000)",
                          }));
}

TEST(Validation, UnitAddressWith0xPrefix) {
    Fixture f;
    f.uart->setName("serial@0x10000000");
    EXPECT_EQ(f.issues(), (std::vector<std::string>{
                              "warning: /soc/serial@0x10000000: unit address should not start with 0x"}));
}

TEST(Validation, DefaultCellCountsAndSixtyFourBitAddresses) {
    Fixture f;
    // Parent without #address-cells/#size-cells: defaults are 2 and 1
    f.soc->removeProperty("#address-cells");
    f.soc->removeProperty("#size-cells");
    set(f.uart, "reg", Cells{0x1, 0x10000000, 0x1000});
    f.uart->setName("serial@110000000");
    set(f.intc, "reg", Cells{0, 0x1000, 0x100});
    EXPECT_EQ(f.issues(), std::vector<std::string>{});
}

TEST(Validation, CellCountsMustBeSingleCells) {
    Fixture f;
    set(f.soc, "#size-cells", Cells{1, 1});
    auto issues = f.issues();
    ASSERT_FALSE(issues.empty());
    EXPECT_EQ(issues.front(), "error: /soc:#size-cells: must be a single cell");
}

TEST(Validation, Phandles) {
    Fixture f;
    set(f.uart, "phandle", Cells{1});
    EXPECT_EQ(f.issues(), (std::vector<std::string>{
                              "error: /soc/serial@10000000:phandle: duplicate phandle 0x1 (also used by "
                              "/soc/interrupt-controller@1000)",
                          }));
    set(f.uart, "phandle", Cells{0xffffffff});
    EXPECT_EQ(f.issues(), (std::vector<std::string>{
                              "error: /soc/serial@10000000:phandle: value 0xffffffff is reserved"}));
}

TEST(Validation, InterruptParentMustResolve) {
    Fixture f;
    set(f.root, "interrupt-parent", Cells{7});
    EXPECT_EQ(f.issues(), (std::vector<std::string>{
                              "error: /:interrupt-parent: refers to phandle 0x7, which no node has"}));
}

TEST(Validation, BrokenReferencesInAnyKnownProperty) {
    Fixture f;
    auto clk = node("clock-controller@2000");
    set(clk, "reg", Cells{0x2000, 0x100});
    set(clk, "phandle", Cells{2});
    set(clk, "#clock-cells", Cells{1});
    f.soc->addChild(clk);
    set(f.uart, "clocks", Cells{2}); // missing the clock specifier
    set(f.uart, "reset-gpios", Cells{9, 1, 0});
    EXPECT_EQ(f.issues(), (std::vector<std::string>{
                              "error: /soc/serial@10000000:clocks: is truncated: /soc/clock-controller@2000 needs 1 "
                              "argument cell(s) after the phandle",
                              "error: /soc/serial@10000000:reset-gpios: refers to phandle 0x9, which no node has",
                          }));
    set(f.uart, "clocks", Cells{2, 5});
    f.uart->removeProperty("reset-gpios");
    EXPECT_EQ(f.issues(), std::vector<std::string>{});
}

TEST(Validation, RangesAndTranslation) {
    Fixture f;
    // soc maps child 0x10000000.. (0x1000 long) only
    set(f.soc, "ranges", Cells{0x10000000, 0x10000000, 0x1000});
    EXPECT_EQ(f.issues(), (std::vector<std::string>{
                              "warning: /soc/interrupt-controller@1000:reg: address 0x1000 is outside the 'ranges' "
                              "of /soc",
                          }));
    set(f.soc, "ranges", Cells{0, 0});
    EXPECT_EQ(f.issues().front(),
              "error: /soc:ranges: 'ranges' has 2 cells, which is not a multiple of child #address-cells + parent "
              "#address-cells + child #size-cells (1 + 1 + 1)");
}

TEST(Validation, CompatibleAndStatusValues) {
    Fixture f;
    set(f.uart, "compatible", Cells{1});
    set(f.uart, "status", std::string("ok"));
    EXPECT_EQ(f.issues(), (std::vector<std::string>{
                              "error: /soc/serial@10000000:compatible: must be a string or a list of strings",
                              "warning: /soc/serial@10000000:status: unknown value \"ok\" (expected okay, "
                              "disabled, reserved, fail or fail-sss)",
                          }));
    set(f.uart, "compatible", std::string("ns16550a"));
    set(f.uart, "status", std::string("fail-overheat"));
    EXPECT_EQ(f.issues(), std::vector<std::string>{});
}

TEST(Validation, Aliases) {
    Fixture f;
    set(f.aliases, "Serial1", std::string("/soc/serial@10000000"));
    set(f.aliases, "serial2", std::string("/soc/missing"));
    EXPECT_EQ(f.issues(), (std::vector<std::string>{
                              "warning: /aliases:Serial1: alias names should only use lowercase letters, digits "
                              "and '-'",
                              "warning: /aliases:serial2: alias points to \"/soc/missing\", which does not exist",
                          }));
}

TEST(Validation, NamesAndDuplicateSiblings) {
    Fixture f;
    f.soc->addChild(node("serial@10000000"));
    f.uart->addProperty(DeviceTreeProperty("bad name", Cells{1}));
    auto issues = f.issues();
    EXPECT_NE(std::find(issues.begin(), issues.end(), "error: /soc: duplicate child node 'serial@10000000'"),
              issues.end());
    EXPECT_NE(std::find_if(issues.begin(), issues.end(),
                           [](const std::string& s) { return s.rfind("error: /soc/serial@10000000:bad name:", 0) == 0; }),
              issues.end());
}

TEST(Validation, NoRoot) {
    DeviceTree t;
    t.setRoot(nullptr);
    ASSERT_EQ(t.check().size(), 1u);
    EXPECT_FALSE(t.validate());
}
