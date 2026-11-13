// End-to-end tests: compile real DTS with dtc, then load the resulting DTB
// through the public file APIs.

#include "DeviceTree.h"
#include "DeviceTreeParser.h"
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using DTE::DeviceTree;
using DTE::DeviceTreeNode;
using DTE::DeviceTreeParserFactory;
using DTE::DTBParser;
using DTE::MemoryReservation;
using DTE::test::countNodes;
using DTE::test::TempDir;

namespace {

#ifdef DTC_PATH
// Runs dtc without a shell. Returns true on success.
bool runDtc(const fs::path& in, const fs::path& out) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // Silence dtc warnings in test output
        if (!freopen("/dev/null", "w", stderr)) _exit(127);
        execlp(DTC_PATH, DTC_PATH, "-I", "dts", "-O", "dtb", "-o", out.c_str(), in.c_str(),
               static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

class CompiledBoard : public ::testing::Test {
protected:
    void SetUp() override {
#ifndef DTC_PATH
        GTEST_SKIP() << "dtc not available";
#else
        dtb_ = tmp_.path() / "board.dtb";
        ASSERT_TRUE(runDtc(fs::path(TEST_DATA_DIR) / "board.dts", dtb_)) << "dtc failed";
#endif
    }

    TempDir tmp_;
    fs::path dtb_;
};

std::vector<uint32_t> cells(const DeviceTree& tree, const std::string& path, const std::string& name) {
    auto node = tree.findNodeByPath(path);
    if (!node) return {};
    const auto* p = static_cast<const DeviceTreeNode&>(*node).findProperty(name);
    return p ? p->getValueAsCells() : std::vector<uint32_t>{};
}

std::string str(const DeviceTree& tree, const std::string& path, const std::string& name) {
    auto node = tree.findNodeByPath(path);
    if (!node) return "<no node>";
    const auto* p = static_cast<const DeviceTreeNode&>(*node).findProperty(name);
    return p ? p->getValueAsString() : "<no property>";
}

} // namespace

TEST_F(CompiledBoard, LoadsFullTreeThroughDeviceTree) {
    DeviceTree tree;
    ASSERT_TRUE(tree.loadFromFile(dtb_.string()));
    EXPECT_EQ(tree.getSourceFile(), dtb_.string());

    // / chosen aliases cpus cpu@0 cpu@1 memory soc gic serial ethernet
    EXPECT_EQ(countNodes(tree.getRoot()), 11u);

    EXPECT_EQ(str(tree, "/", "model"), "\"ACME Widget Board\"");
    EXPECT_EQ(str(tree, "/", "compatible"), "\"acme,widget-board\", \"acme,widget\"");
    EXPECT_EQ(str(tree, "/chosen", "bootargs"), "\"console=ttyS0,115200 root=/dev/mmcblk0p2\"");
    EXPECT_EQ(str(tree, "/aliases", "serial0"), "\"/soc/serial@10000000\"");
}

TEST_F(CompiledBoard, DecodesCellValuesExactly) {
    DeviceTree tree;
    ASSERT_TRUE(tree.loadFromFile(dtb_.string()));

    EXPECT_EQ(cells(tree, "/cpus/cpu@0", "clock-frequency"), (std::vector<uint32_t>{1200000000u}));
    EXPECT_EQ(cells(tree, "/cpus/cpu@1", "reg"), (std::vector<uint32_t>{1}));
    EXPECT_EQ(cells(tree, "/memory@80000000", "reg"), (std::vector<uint32_t>{0x80000000, 0x40000000}));
    EXPECT_EQ(cells(tree, "/soc/serial@10000000", "clock-frequency"), (std::vector<uint32_t>{1843200}));
    EXPECT_EQ(cells(tree, "/soc/interrupt-controller@1000", "reg"),
              (std::vector<uint32_t>{0x1000, 0x1000, 0x2000, 0x2000}));
}

TEST_F(CompiledBoard, ResolvesPhandles) {
    DeviceTree tree;
    ASSERT_TRUE(tree.loadFromFile(dtb_.string()));

    auto phandle = cells(tree, "/soc/interrupt-controller@1000", "phandle");
    ASSERT_EQ(phandle.size(), 1u);
    EXPECT_EQ(cells(tree, "/", "interrupt-parent"), phandle);
}

TEST_F(CompiledBoard, DecodesBytesAndEmptyProperties) {
    DeviceTree tree;
    ASSERT_TRUE(tree.loadFromFile(dtb_.string()));

    auto eth = tree.findNodeByPath("/soc/ethernet@20000000");
    ASSERT_TRUE(eth);
    const auto* mac = static_cast<const DeviceTreeNode&>(*eth).findProperty("local-mac-address");
    ASSERT_TRUE(mac);
    EXPECT_EQ(mac->getValueAsBinary(), (std::vector<uint8_t>{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}));

    auto gic = tree.findNodeByPath("/soc/interrupt-controller@1000");
    ASSERT_TRUE(gic);
    const auto* ic = static_cast<const DeviceTreeNode&>(*gic).findProperty("interrupt-controller");
    ASSERT_TRUE(ic);
    EXPECT_EQ(ic->getValueAsString(), "\"\"");
}

TEST_F(CompiledBoard, ParsesMemoryReservations) {
    DeviceTree tree;
    ASSERT_TRUE(tree.loadFromFile(dtb_.string()));
    EXPECT_EQ(tree.getMemoryReservations(),
              (std::vector<MemoryReservation>{{0x88000000, 0x100000}, {0x8a000000, 0x2000}}));
}

TEST_F(CompiledBoard, FactoryDetectsDtbByMagicRegardlessOfExtension) {
    fs::path renamed = tmp_.path() / "firmware.bin";
    fs::copy_file(dtb_, renamed);

    auto parser = DeviceTreeParserFactory::createParser(renamed.string());
    ASSERT_TRUE(parser);
    auto tree = parser->parse(renamed.string());
    ASSERT_TRUE(tree);
    EXPECT_EQ(countNodes(tree->getRoot()), 11u);
}

TEST(DTBFile, ReportsMissingFile) {
    DTBParser parser;
    EXPECT_FALSE(parser.parse("/nonexistent/path/x.dtb"));
    EXPECT_NE(parser.lastError().find("Cannot open file"), std::string::npos);
}

TEST(DTBFile, CorruptDtbExtensionStillRoutedToDtbParser) {
    TempDir tmp;
    fs::path bad = tmp.path() / "corrupt.dtb";
    std::ofstream(bad, std::ios::binary) << "this is not a device tree blob at all.....";

    DTBParser parser;
    EXPECT_TRUE(parser.canParse(bad.string()));
    EXPECT_FALSE(parser.parse(bad.string()));
    EXPECT_NE(parser.lastError().find("magic"), std::string::npos);
}

TEST(DTBFile, DtsFileIsNotClaimedByDtbParser) {
    DTBParser parser;
    EXPECT_FALSE(parser.canParse(std::string(TEST_DATA_DIR) + "/board.dts"));
}
