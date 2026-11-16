// DTS and DTB writer tests: exact output, round trips through our own
// parsers, and cross-checks against dtc when it is available.

#include "DeviceTreeDiff.h"
#include "DeviceTreeParser.h"
#include "Process.h"
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <cstdlib>

using namespace DTE;
using DTE::test::countNodes;
using DTE::test::findProp;
using DTE::test::TempDir;

namespace {

const std::string kData = TEST_DATA_DIR;

std::shared_ptr<DeviceTreeNode> node(const std::string& name) {
    return std::make_shared<DeviceTreeNode>(name);
}

// Every value type whose representation survives a DTB round trip, plus a
// string that needs escaping in DTS. A DTB carries no types, so strings with
// control characters read back as bytes (as with dtc); withControlChars adds
// them for tests that only look at the DTS text.
std::shared_ptr<DeviceTree> sampleTree(bool withControlChars = false) {
    auto root = node("/");
    root->addProperty(DeviceTreeProperty(
        "model", std::string(withControlChars ? "say \"hi\" \\ tab\there\nnl \x01" : "say \"hi\" \\ back")));
    root->addProperty(DeviceTreeProperty("compatible", StringList{"vendor,board", "vendor,soc"}));
    root->addProperty(DeviceTreeProperty("#address-cells", std::vector<uint32_t>{1}));
    root->addProperty(DeviceTreeProperty("#size-cells", std::vector<uint32_t>{1}));

    auto soc = node("soc");
    soc->addProperty(DeviceTreeProperty("ranges", std::string()));
    auto eth = node("ethernet@1000");
    eth->addProperty(DeviceTreeProperty("reg", std::vector<uint32_t>{0x1000, 0x100}));
    eth->addProperty(DeviceTreeProperty("local-mac-address", std::vector<uint8_t>{0, 0x11, 0x22, 0x33, 0x44, 0x55}));
    eth->addProperty(DeviceTreeProperty("status", std::string("okay")));
    auto empty = node("empty-node");

    root->addChild(soc);
    soc->addChild(eth);
    root->addChild(empty);

    auto t = std::make_shared<DeviceTree>();
    t->setRoot(root);
    t->setMemoryReservations({{0x80000000, 0x100000}, {0x1'0000'0000ULL, 0x2000}});
    t->setBootCpuId(2);
    return t;
}

void expectSameTree(const std::shared_ptr<DeviceTree>& a, const std::shared_ptr<DeviceTree>& b) {
    DeviceTreeDiff diff(a, b);
    EXPECT_EQ(diff.getTotalChanges(), 0u) << DiffVisualizer(diff).getFormattedDiff();
    EXPECT_EQ(countNodes(a->getRoot()), countNodes(b->getRoot()));
    EXPECT_EQ(a->getMemoryReservations(), b->getMemoryReservations());
}

std::shared_ptr<DeviceTree> reparseDtb(const std::vector<uint8_t>& blob) {
    DTBParser parser;
    auto t = parser.parseBuffer(blob);
    EXPECT_TRUE(t) << parser.lastError();
    return t;
}

} // namespace

// ---------------------------------------------------------------------------
// DTS writer
// ---------------------------------------------------------------------------

TEST(DtsWriter, ProducesExpectedSource) {
    EXPECT_EQ(sampleTree(true)->exportAsDTS(),
              "/dts-v1/;\n"
              "\n"
              "/memreserve/ 0x80000000 0x100000;\n"
              "/memreserve/ 0x100000000 0x2000;\n"
              "\n"
              "/ {\n"
              "\tmodel = \"say \\\"hi\\\" \\\\ tab\\there\\nnl \\x01\";\n"
              "\tcompatible = \"vendor,board\", \"vendor,soc\";\n"
              "\t#address-cells = <0x1>;\n"
              "\t#size-cells = <0x1>;\n"
              "\n"
              "\tsoc {\n"
              "\t\tranges;\n"
              "\n"
              "\t\tethernet@1000 {\n"
              "\t\t\treg = <0x1000 0x100>;\n"
              "\t\t\tlocal-mac-address = [00 11 22 33 44 55];\n"
              "\t\t\tstatus = \"okay\";\n"
              "\t\t};\n"
              "\t};\n"
              "\n"
              "\tempty-node {\n"
              "\t};\n"
              "};\n");
}

TEST(DtsWriter, OmitsMemreserveBlockWhenEmpty) {
    DeviceTree t;
    EXPECT_EQ(t.exportAsDTS(), "/dts-v1/;\n\n/ {\n};\n");
}

// ---------------------------------------------------------------------------
// DTB writer
// ---------------------------------------------------------------------------

TEST(DtbWriter, RoundTripsThroughParser) {
    auto original = sampleTree();
    auto blob = original->exportAsDTB();
    auto reparsed = reparseDtb(blob);
    ASSERT_TRUE(reparsed);
    expectSameTree(original, reparsed);
    EXPECT_EQ(reparsed->getBootCpuId(), 2u);
}

TEST(DtbWriter, WritesV17HeaderWithConsistentSizes) {
    auto blob = sampleTree()->exportAsDTB();
    auto be32 = [&](size_t off) {
        return (uint32_t(blob[off]) << 24) | (uint32_t(blob[off + 1]) << 16) |
               (uint32_t(blob[off + 2]) << 8) | uint32_t(blob[off + 3]);
    };
    EXPECT_EQ(be32(0), 0xd00dfeedu);
    EXPECT_EQ(be32(4), blob.size());
    EXPECT_EQ(be32(20), 17u);
    EXPECT_EQ(be32(24), 16u);
    EXPECT_EQ(be32(8) + be32(36), be32(12)); // struct block ends where strings begin
    EXPECT_EQ(be32(12) + be32(32), blob.size());
}

TEST(DtbWriter, DeduplicatesPropertyNames) {
    auto root = node("/");
    for (int i = 0; i < 5; ++i) {
        auto child = node("n" + std::to_string(i));
        child->addProperty(DeviceTreeProperty("status", std::string("okay")));
        root->addChild(child);
    }
    DeviceTree t;
    t.setRoot(root);
    auto blob = t.exportAsDTB();
    uint32_t stringsSize = (uint32_t(blob[32]) << 24) | (uint32_t(blob[33]) << 16) |
                           (uint32_t(blob[34]) << 8) | uint32_t(blob[35]);
    EXPECT_EQ(stringsSize, std::string("status").size() + 1);
}

TEST(DtbWriter, EmptyTreeIsValid) {
    DeviceTree t;
    t.setRoot(nullptr);
    auto reparsed = reparseDtb(t.exportAsDTB());
    ASSERT_TRUE(reparsed);
    EXPECT_TRUE(reparsed->getRoot()->getChildren().empty());
}

TEST(DtbWriter, SixtyFourBitCellsComeBackAsCellPairs) {
    auto root = node("/");
    root->addProperty(DeviceTreeProperty("big", std::vector<uint64_t>{0x1'2345'6789ULL}));
    DeviceTree t;
    t.setRoot(root);
    auto reparsed = reparseDtb(t.exportAsDTB());
    ASSERT_TRUE(reparsed);
    EXPECT_EQ(findProp(*reparsed, "/", "big")->getValueAsCells(),
              (std::vector<uint32_t>{0x1, 0x23456789}));
}

// ---------------------------------------------------------------------------
// saveToFile
// ---------------------------------------------------------------------------

TEST(SaveToFile, WritesBothFormats) {
    TempDir tmp;
    auto t = sampleTree();
    const auto dtb = (tmp.path() / "out.dtb").string();
    const auto dts = (tmp.path() / "out.dts").string();
    std::string error;
    ASSERT_TRUE(t->saveToFile(dtb, false, &error)) << error;
    ASSERT_TRUE(t->saveToFile(dts, true, &error)) << error;

    auto loaded = std::make_shared<DeviceTree>();
    ASSERT_TRUE(loaded->loadFromFile(dtb)) << loaded->getLoadError();
    expectSameTree(t, loaded);

    std::ifstream in(dts);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text, t->exportAsDTS());
}

TEST(SaveToFile, ReportsUnwritablePath) {
    std::string error;
    EXPECT_FALSE(sampleTree()->saveToFile("/nonexistent/dir/out.dtb", false, &error));
    EXPECT_NE(error.find("Cannot open file for writing"), std::string::npos) << error;
}

TEST(SaveToFile, ReportsMissingTree) {
    DeviceTree t;
    t.setRoot(nullptr);
    std::string error;
    TempDir tmp;
    EXPECT_FALSE(t.saveToFile((tmp.path() / "x.dtb").string(), false, &error));
    EXPECT_FALSE(error.empty());
}

// ---------------------------------------------------------------------------
// Cross-checks with dtc
// ---------------------------------------------------------------------------

class WriterWithDtc : public ::testing::Test {
protected:
    void SetUp() override {
#ifndef DTC_PATH
        GTEST_SKIP() << "dtc not available";
#else
        ::setenv("DTE_DTC", DTC_PATH, 1);
#endif
    }
    void TearDown() override { ::unsetenv("DTE_DTC"); }

    TempDir tmp_;
};

TEST_F(WriterWithDtc, DtcAcceptsOurDtbAndDts) {
    auto t = sampleTree(true);
    const auto dtb = (tmp_.path() / "ours.dtb").string();
    const auto dts = (tmp_.path() / "ours.dts").string();
    ASSERT_TRUE(t->saveToFile(dtb, false));
    ASSERT_TRUE(t->saveToFile(dts, true));

    auto r1 = runProcess({dtcExecutable(), "-q", "-I", "dtb", "-O", "dts", "-o", "/dev/null", dtb});
    EXPECT_TRUE(r1.ok()) << r1.stderrData;
    auto r2 = runProcess({dtcExecutable(), "-q", "-I", "dts", "-O", "dtb", "-o", "/dev/null", dts});
    EXPECT_TRUE(r2.ok()) << r2.stderrData;
}

TEST_F(WriterWithDtc, DtsRoundTripsThroughDtc) {
    auto t = sampleTree();
    const auto dts = (tmp_.path() / "ours.dts").string();
    ASSERT_TRUE(t->saveToFile(dts, true));

    auto loaded = std::make_shared<DeviceTree>();
    ASSERT_TRUE(loaded->loadFromFile(dts)) << loaded->getLoadError();
    expectSameTree(t, loaded);
}

TEST_F(WriterWithDtc, DtbIsByteIdenticalToDtcOutput) {
    // Load a source with dtc, write it back with our writer, and compare
    // with dtc's own blob for the same source.
    const auto ref = (tmp_.path() / "ref.dtb").string();
    ASSERT_TRUE(DeviceTreeUtils::dtsToDtb(kData + "/board.dts", ref));

    DeviceTree t;
    ASSERT_TRUE(t.loadFromFile(ref)) << t.getLoadError();

    std::ifstream in(ref, std::ios::binary);
    std::vector<uint8_t> expected((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(t.exportAsDTB(), expected);
}

TEST_F(WriterWithDtc, ExportedDtsOfPreprocessedSourceIsSelfContained) {
    DeviceTree t;
    ASSERT_TRUE(t.loadFromFile(kData + "/preprocessed/board.dts")) << t.getLoadError();

    // Written into a directory without the headers: must still compile.
    const auto out = (tmp_.path() / "flat.dts").string();
    ASSERT_TRUE(t.saveToFile(out, true));
    EXPECT_FALSE(DTSParser::needsPreprocessing(out));

    auto original = std::make_shared<DeviceTree>(t);
    auto reloaded = std::make_shared<DeviceTree>();
    ASSERT_TRUE(reloaded->loadFromFile(out)) << reloaded->getLoadError();
    expectSameTree(original, reloaded);
}
