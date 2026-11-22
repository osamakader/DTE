#include "Addresses.h"
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <cstdlib>

using namespace DTE;

namespace {

using Cells = std::vector<uint32_t>;

std::shared_ptr<DeviceTreeNode> node(const std::string& name) {
    return std::make_shared<DeviceTreeNode>(name);
}

std::vector<std::string> regOf(const std::shared_ptr<DeviceTreeNode>& n) {
    std::vector<RegEntry> entries;
    std::string error;
    if (!decodeReg(n, entries, &error)) return {"error: " + error};
    std::vector<std::string> out;
    for (const auto& e : entries) out.push_back(describeReg(e));
    return out;
}

} // namespace

#ifdef DTC_PATH
class AddressesFromDts : public ::testing::Test {
protected:
    void SetUp() override {
        ::setenv("DTE_DTC", DTC_PATH, 1);
        ASSERT_TRUE(tree_.loadFromFile(std::string(TEST_DATA_DIR) + "/addresses.dts")) << tree_.getLoadError();
    }
    void TearDown() override { ::unsetenv("DTE_DTC"); }
    std::shared_ptr<DeviceTreeNode> at(const std::string& path) {
        auto n = tree_.findNodeByPath(path);
        EXPECT_TRUE(n) << path;
        return n;
    }
    DeviceTree tree_;
};

TEST_F(AddressesFromDts, RootChildrenUseRootCells) {
    EXPECT_EQ(regOf(at("/memory@80000000")),
              (std::vector<std::string>{"0x80000000 size 0x40000000", "0x880000000 size 0x80000000"}));
}

TEST_F(AddressesFromDts, TranslatesThroughNonIdentityRanges) {
    EXPECT_EQ(regOf(at("/soc@100000000/serial@1000")),
              (std::vector<std::string>{"0x1000 size 0x100 -> CPU 0x100001000"}));
}

TEST_F(AddressesFromDts, TranslatesThroughNestedBuses) {
    EXPECT_EQ(regOf(at("/soc@100000000/bridge@20000000/timer@100")),
              (std::vector<std::string>{"0x100 size 0x20 -> CPU 0x120000100", "0x200 size 0x20 -> CPU 0x120000200"}));
}

TEST_F(AddressesFromDts, IdentityRanges) {
    EXPECT_EQ(regOf(at("/soc@100000000/bus/gpio@4000")),
              (std::vector<std::string>{"0x4000 size 0x40 -> CPU 0x100004000"}));
}

TEST_F(AddressesFromDts, OutsideRangesIsReported) {
    EXPECT_EQ(regOf(at("/soc@100000000/bridge@20000000/stray@200000")),
              (std::vector<std::string>{
                  "0x200000 size 0x10 (0x200000 is outside the 'ranges' of /soc@100000000/bridge@20000000)"}));
}

TEST_F(AddressesFromDts, BusesWithoutRangesAreNotMemoryMapped) {
    EXPECT_EQ(regOf(at("/soc@100000000/i2c@3000/eeprom@50")),
              (std::vector<std::string>{"0x50 (not memory-mapped: /soc@100000000/i2c@3000 has no 'ranges')"}));
    // The controller itself is memory-mapped
    EXPECT_EQ(regOf(at("/soc@100000000/i2c@3000")),
              (std::vector<std::string>{"0x3000 size 0x100 -> CPU 0x100003000"}));
}

TEST_F(AddressesFromDts, DecodesRanges) {
    std::vector<RangeEntry> ranges;
    bool identity = true;
    ASSERT_TRUE(decodeRanges(at("/soc@100000000"), ranges, identity));
    EXPECT_FALSE(identity);
    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(describeRange(ranges[0]), "0x0..0x3fffffff -> 0x100000000");

    ASSERT_TRUE(decodeRanges(at("/soc@100000000/bus"), ranges, identity));
    EXPECT_TRUE(identity);
    EXPECT_TRUE(ranges.empty());

    std::string error;
    EXPECT_FALSE(decodeRanges(at("/soc@100000000/i2c@3000"), ranges, identity, &error));
    EXPECT_NE(error.find("has no 'ranges'"), std::string::npos);
}

TEST_F(AddressesFromDts, CellsForChildren) {
    EXPECT_EQ(cellsForChildren(*at("/soc@100000000/i2c@3000")).size, 0u);
    EXPECT_EQ(cellsForChildren(*tree_.getRoot()).address, 2u);
}
#endif

TEST(Addresses, DefaultsWhenCellsAreMissing) {
    auto root = node("/");
    auto dev = node("dev@0");
    dev->addProperty(DeviceTreeProperty("reg", Cells{0x1, 0x0, 0x1000})); // 2 address + 1 size
    root->addChild(dev);
    EXPECT_EQ(regOf(dev), (std::vector<std::string>{"0x100000000 size 0x1000"}));
}

TEST(Addresses, MalformedAndUnsupported) {
    auto root = node("/");
    root->addProperty(DeviceTreeProperty("#address-cells", Cells{1}));
    root->addProperty(DeviceTreeProperty("#size-cells", Cells{1}));
    auto dev = node("dev");
    dev->addProperty(DeviceTreeProperty("reg", Cells{1, 2, 3}));
    root->addChild(dev);
    EXPECT_EQ(regOf(dev), (std::vector<std::string>{
                              "error: 'reg' has 3 cells, which is not a multiple of #address-cells + #size-cells (1 + 1)"}));

    auto pci = node("pci");
    pci->addProperty(DeviceTreeProperty("#address-cells", Cells{3}));
    pci->addProperty(DeviceTreeProperty("#size-cells", Cells{2}));
    pci->addProperty(DeviceTreeProperty("ranges", Cells{0x02000000, 0, 0, 0x40000000, 0, 0x10000000}));
    auto ep = node("ep@0");
    ep->addProperty(DeviceTreeProperty("reg", Cells{0, 0, 0, 0, 0}));
    root->addChild(pci);
    pci->addChild(ep);
    EXPECT_EQ(regOf(ep), (std::vector<std::string>{"error: 3-cell addresses (e.g. PCI) are not supported"}));

    std::vector<RangeEntry> ranges;
    bool identity = false;
    std::string error;
    EXPECT_FALSE(decodeRanges(pci, ranges, identity, &error));
    EXPECT_NE(error.find("3-cell"), std::string::npos) << error;

    auto bus = node("bus");
    bus->addProperty(DeviceTreeProperty("#address-cells", Cells{1}));
    bus->addProperty(DeviceTreeProperty("#size-cells", Cells{1}));
    bus->addProperty(DeviceTreeProperty("ranges", Cells{0, 0x1000}));
    root->addChild(bus);
    EXPECT_FALSE(decodeRanges(bus, ranges, identity, &error));
    EXPECT_NE(error.find("not a multiple of child #address-cells + parent #address-cells + child #size-cells (1 + 1 + 1)"),
              std::string::npos)
        << error;
}

TEST(Addresses, MissingReg) {
    auto root = node("/");
    auto dev = node("dev");
    root->addChild(dev);
    EXPECT_EQ(regOf(dev), (std::vector<std::string>{"error: no 'reg' property"}));
}

#ifdef DTC_PATH
TEST_F(AddressesFromDts, ValidationFlagsOnlyTheStrayNode) {
    std::vector<std::string> issues;
    for (const auto& i : tree_.check()) issues.push_back(i.toString());
    EXPECT_EQ(issues, (std::vector<std::string>{
                          "warning: /soc@100000000/bridge@20000000/stray@200000:reg: address 0x200000 is outside the "
                          "'ranges' of /soc@100000000/bridge@20000000",
                      }));
}
#endif
