#include "DeviceTreeParser.h"
#include "FdtBuilder.h"

#include <gtest/gtest.h>

#include <variant>

using DTE::DTBParser;
using DTE::DeviceTree;
using DTE::DeviceTreeNode;
using DTE::DeviceTreeProperty;
using DTE::MemoryReservation;
using DTE::test::FdtBuilder;

namespace {

// Smallest valid tree: an empty root node.
FdtBuilder emptyRoot() {
    FdtBuilder b;
    b.beginNode("").endNode().end();
    return b;
}

// A small but representative tree used by several tests.
FdtBuilder sampleTree() {
    FdtBuilder b;
    b.beginNode("")
        .propString("compatible", {"vendor,board", "vendor,soc"})
        .propString("model", {"Test Board"})
        .propCells("#address-cells", {1})
        .propCells("#size-cells", {1})
        .beginNode("cpus")
            .propCells("#address-cells", {1})
            .propCells("#size-cells", {0})
            .beginNode("cpu@0")
                .propString("device_type", {"cpu"})
                .propCells("reg", {0})
                .propCells("clock-frequency", {1200000000})
            .endNode()
        .endNode()
        .beginNode("memory@80000000")
            .propString("device_type", {"memory"})
            .propCells("reg", {0x80000000, 0x40000000})
        .endNode()
        .beginNode("ethernet@1000")
            .prop("local-mac-address", {0x00, 0x11, 0x22, 0x33, 0x44, 0x55})
            .propEmpty("dma-coherent")
        .endNode()
    .endNode()
    .end();
    return b;
}

std::shared_ptr<DeviceTree> parseOk(const std::vector<uint8_t>& blob) {
    DTBParser parser;
    auto tree = parser.parseBuffer(blob);
    EXPECT_TRUE(tree) << "unexpected parse error: " << parser.lastError();
    EXPECT_TRUE(parser.lastError().empty());
    return tree;
}

// Asserts that parsing fails and the error mentions `fragment`.
void expectError(const std::vector<uint8_t>& blob, const std::string& fragment) {
    DTBParser parser;
    auto tree = parser.parseBuffer(blob);
    EXPECT_FALSE(tree);
    EXPECT_NE(parser.lastError().find(fragment), std::string::npos)
        << "error was: \"" << parser.lastError() << "\", expected to contain \"" << fragment << "\"";
}

size_t countNodes(const std::shared_ptr<DeviceTreeNode>& n) {
    size_t count = 1;
    for (const auto& c : n->getChildren()) count += countNodes(c);
    return count;
}

DeviceTreeProperty prop(const std::shared_ptr<DeviceTree>& tree,
                               const std::string& path, const std::string& name) {
    auto node = tree->findNodeByPath(path);
    if (!node) throw std::runtime_error("no node " + path);
    auto* p = static_cast<const DeviceTreeNode&>(*node).findProperty(name);
    if (!p) throw std::runtime_error("no property " + path + ":" + name);
    return *p;
}

} // namespace

// ---------------------------------------------------------------------------
// Valid blobs
// ---------------------------------------------------------------------------

TEST(DTBParser, ParsesEmptyRoot) {
    auto tree = parseOk(emptyRoot().build());
    ASSERT_TRUE(tree);
    ASSERT_TRUE(tree->getRoot());
    EXPECT_EQ(tree->getRoot()->getName(), "/");
    EXPECT_TRUE(tree->getRoot()->getChildren().empty());
    EXPECT_TRUE(tree->getRoot()->getProperties().empty());
    EXPECT_TRUE(tree->getMemoryReservations().empty());
}

TEST(DTBParser, ParsesNestedNodesInOrder) {
    auto tree = parseOk(sampleTree().build());
    ASSERT_TRUE(tree);

    auto root = tree->getRoot();
    EXPECT_EQ(countNodes(root), 5u);
    ASSERT_EQ(root->getChildren().size(), 3u);
    EXPECT_EQ(root->getChildren()[0]->getName(), "cpus");
    EXPECT_EQ(root->getChildren()[1]->getName(), "memory@80000000");
    EXPECT_EQ(root->getChildren()[2]->getName(), "ethernet@1000");

    auto cpu = tree->findNodeByPath("/cpus/cpu@0");
    ASSERT_TRUE(cpu);
    EXPECT_EQ(cpu->getFullPath(), "/cpus/cpu@0");
    EXPECT_EQ(cpu->getParent()->getName(), "cpus");
}

TEST(DTBParser, PreservesPropertyOrder) {
    auto tree = parseOk(sampleTree().build());
    ASSERT_TRUE(tree);
    const auto& props = tree->getRoot()->getProperties();
    ASSERT_EQ(props.size(), 4u);
    EXPECT_EQ(props[0].getName(), "compatible");
    EXPECT_EQ(props[1].getName(), "model");
    EXPECT_EQ(props[2].getName(), "#address-cells");
    EXPECT_EQ(props[3].getName(), "#size-cells");
}

TEST(DTBParser, DecodesStringProperty) {
    auto tree = parseOk(sampleTree().build());
    ASSERT_TRUE(tree);
    const auto model = prop(tree, "/", "model");
    ASSERT_TRUE(model.isString());
    EXPECT_EQ(std::get<std::string>(model.getValue()), "Test Board");
    EXPECT_EQ(model.getValueAsString(), "\"Test Board\"");
}

TEST(DTBParser, DecodesStringListLikeDtsSource) {
    auto tree = parseOk(sampleTree().build());
    ASSERT_TRUE(tree);
    const auto compat = prop(tree, "/", "compatible");
    ASSERT_TRUE(compat.isString());
    EXPECT_EQ(compat.getValueAsString(), "\"vendor,board\", \"vendor,soc\"");
}

TEST(DTBParser, DecodesCellsBigEndian) {
    auto tree = parseOk(sampleTree().build());
    ASSERT_TRUE(tree);

    const auto reg = prop(tree, "/memory@80000000", "reg");
    ASSERT_TRUE(reg.isCells());
    EXPECT_EQ(reg.getValueAsCells(), (std::vector<uint32_t>{0x80000000, 0x40000000}));

    const auto freq = prop(tree, "/cpus/cpu@0", "clock-frequency");
    ASSERT_TRUE(freq.isCells());
    EXPECT_EQ(freq.getValueAsCells(), (std::vector<uint32_t>{1200000000u}));
}

TEST(DTBParser, DecodesOddLengthAsBytes) {
    auto tree = parseOk(sampleTree().build());
    ASSERT_TRUE(tree);
    const auto mac = prop(tree, "/ethernet@1000", "local-mac-address");
    ASSERT_TRUE(mac.isBinary());
    EXPECT_EQ(mac.getValueAsBinary(), (std::vector<uint8_t>{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}));
}

TEST(DTBParser, DecodesEmptyProperty) {
    auto tree = parseOk(sampleTree().build());
    ASSERT_TRUE(tree);
    const auto p = prop(tree, "/ethernet@1000", "dma-coherent");
    ASSERT_TRUE(p.isString());
    EXPECT_EQ(std::get<std::string>(p.getValue()), "");
}

TEST(DTBParser, NonPrintableNulTerminatedDataIsNotAString) {
    FdtBuilder b;
    // 8 bytes ending in NUL but containing control characters -> cells
    b.beginNode("").prop("blob", {0x01, 0x02, 0x03, 0x00, 0x05, 0x06, 0x07, 0x00}).endNode().end();
    auto tree = parseOk(b.build());
    ASSERT_TRUE(tree);
    const auto p = prop(tree, "/", "blob");
    ASSERT_TRUE(p.isCells());
    EXPECT_EQ(p.getValueAsCells(), (std::vector<uint32_t>{0x01020300, 0x05060700}));
}

TEST(DTBParser, EmptyStringSegmentIsNotAStringList) {
    FdtBuilder b;
    // "a\0\0\0" has an empty segment, so it is treated as one cell
    b.beginNode("").prop("x", {'a', 0, 0, 0}).endNode().end();
    auto tree = parseOk(b.build());
    ASSERT_TRUE(tree);
    EXPECT_TRUE(prop(tree, "/", "x").isCells());
}

TEST(DTBParser, SkipsNopTokens) {
    FdtBuilder b;
    b.nop().beginNode("").nop()
        .propCells("a", {1}).nop()
        .beginNode("child").nop().endNode()
        .nop().endNode().nop().end();
    auto tree = parseOk(b.build());
    ASSERT_TRUE(tree);
    EXPECT_EQ(tree->getRoot()->getProperties().size(), 1u);
    EXPECT_EQ(tree->getRoot()->getChildren().size(), 1u);
}

TEST(DTBParser, ParsesMemoryReservationsAndBootCpu) {
    FdtBuilder b = emptyRoot();
    b.reserve(0x80000000, 0x100000).reserve(0x1'0000'0000ULL, 0x2000).bootCpu(3);
    auto tree = parseOk(b.build());
    ASSERT_TRUE(tree);
    EXPECT_EQ(tree->getMemoryReservations(),
              (std::vector<MemoryReservation>{{0x80000000, 0x100000}, {0x100000000ULL, 0x2000}}));
    EXPECT_EQ(tree->getBootCpuId(), 3u);
}

TEST(DTBParser, ParsesVersion16Header) {
    auto tree = parseOk(sampleTree().build(16));
    ASSERT_TRUE(tree);
    EXPECT_EQ(countNodes(tree->getRoot()), 5u);
}

TEST(DTBParser, AcceptsLegacySlashRootName) {
    FdtBuilder b;
    b.beginNode("/").endNode().end();
    auto tree = parseOk(b.build());
    ASSERT_TRUE(tree);
    EXPECT_EQ(tree->getRoot()->getName(), "/");
}

TEST(DTBParser, IgnoresTrailingBytesBeyondTotalsize) {
    auto blob = sampleTree().build();
    blob.resize(blob.size() + 64, 0xAA);
    EXPECT_TRUE(parseOk(blob));
}

TEST(DTBParser, DuplicatePropertyKeepsLastValue) {
    FdtBuilder b;
    b.beginNode("").propCells("x", {1}).propCells("x", {2}).endNode().end();
    auto tree = parseOk(b.build());
    ASSERT_TRUE(tree);
    ASSERT_EQ(tree->getRoot()->getProperties().size(), 1u);
    EXPECT_EQ(prop(tree, "/", "x").getValueAsCells(), (std::vector<uint32_t>{2}));
}

// ---------------------------------------------------------------------------
// Malformed header
// ---------------------------------------------------------------------------

TEST(DTBParserErrors, RejectsEmptyAndTinyBuffers) {
    expectError({}, "too small");
    expectError(std::vector<uint8_t>(35, 0), "too small");
}

TEST(DTBParserErrors, RejectsBadMagic) {
    auto blob = emptyRoot().build();
    blob[0] = 0xED; // little-endian-looking magic
    expectError(blob, "magic");
}

TEST(DTBParserErrors, RejectsTruncatedBlob) {
    auto blob = sampleTree().build();
    blob.resize(blob.size() - 1);
    expectError(blob, "truncated");
}

TEST(DTBParserErrors, RejectsTotalsizeSmallerThanHeader) {
    auto blob = emptyRoot().build();
    FdtBuilder::put32(blob, FdtBuilder::OFF_TOTALSIZE, 20);
    expectError(blob, "smaller than the header");
}

TEST(DTBParserErrors, RejectsOldVersion) {
    auto blob = emptyRoot().build();
    FdtBuilder::put32(blob, FdtBuilder::OFF_VERSION, 15);
    expectError(blob, "Unsupported DTB version 15");
}

TEST(DTBParserErrors, RejectsIncompatibleFutureVersion) {
    expectError(emptyRoot().build(18, 18), "requires reader version 18");
}

TEST(DTBParserErrors, AcceptsNewerButBackwardCompatibleVersion) {
    // version 18 that declares compatibility with 16 must still parse
    EXPECT_TRUE(parseOk(emptyRoot().build(18, 16)));
}

TEST(DTBParserErrors, RejectsStructBlockOutOfBounds) {
    auto blob = emptyRoot().build();
    FdtBuilder::put32(blob, FdtBuilder::OFF_SIZE_STRUCT, 0xFFFFFFF0);
    expectError(blob, "Structure block");
}

TEST(DTBParserErrors, RejectsMisalignedStructBlock) {
    auto blob = emptyRoot().build();
    FdtBuilder::put32(blob, FdtBuilder::OFF_DT_STRUCT,
                      FdtBuilder::get32(blob, FdtBuilder::OFF_DT_STRUCT) + 1);
    expectError(blob, "Structure block");
}

TEST(DTBParserErrors, RejectsStringsBlockOutOfBounds) {
    auto blob = sampleTree().build();
    FdtBuilder::put32(blob, FdtBuilder::OFF_SIZE_STRINGS, 0x7FFFFFFF);
    expectError(blob, "Strings block");
}

TEST(DTBParserErrors, RejectsMisalignedReservationMap) {
    auto blob = emptyRoot().build();
    FdtBuilder::put32(blob, FdtBuilder::OFF_MEM_RSVMAP, 44);
    expectError(blob, "Memory reservation block");
}

TEST(DTBParserErrors, RejectsUnterminatedReservationMap) {
    // Point the reservation map at the very end so there is no room for
    // the terminating (0, 0) entry.
    auto blob = emptyRoot().build();
    uint32_t total = FdtBuilder::get32(blob, FdtBuilder::OFF_TOTALSIZE);
    FdtBuilder::put32(blob, FdtBuilder::OFF_MEM_RSVMAP, (total - 8) & ~7u);
    expectError(blob, "not terminated");
}

// ---------------------------------------------------------------------------
// Malformed structure block
// ---------------------------------------------------------------------------

TEST(DTBParserErrors, RejectsUnknownToken) {
    FdtBuilder b;
    b.beginNode("").token(0x7).endNode().end();
    expectError(b.build(), "Unknown structure token 0x7");
}

TEST(DTBParserErrors, RejectsMissingEndToken) {
    FdtBuilder b;
    b.beginNode("").endNode();
    expectError(b.build(), "Truncated");
}

TEST(DTBParserErrors, RejectsUnclosedNode) {
    FdtBuilder b;
    b.beginNode("").beginNode("child").endNode().end();
    expectError(b.build(), "unclosed node");
}

TEST(DTBParserErrors, RejectsUnbalancedEndNode) {
    FdtBuilder b;
    b.beginNode("").endNode().endNode().end();
    expectError(b.build(), "Unbalanced FDT_END_NODE");
}

TEST(DTBParserErrors, RejectsMultipleRoots) {
    FdtBuilder b;
    b.beginNode("").endNode().beginNode("").endNode().end();
    expectError(b.build(), "Multiple root nodes");
}

TEST(DTBParserErrors, RejectsNamedRoot) {
    FdtBuilder b;
    b.beginNode("notroot").endNode().end();
    expectError(b.build(), "Root node has unexpected name");
}

TEST(DTBParserErrors, RejectsEmptyChildName) {
    FdtBuilder b;
    b.beginNode("").beginNode("").endNode().endNode().end();
    expectError(b.build(), "Empty node name");
}

TEST(DTBParserErrors, RejectsNoRoot) {
    FdtBuilder b;
    b.nop().end();
    expectError(b.build(), "no root node");
}

TEST(DTBParserErrors, RejectsPropertyOutsideNode) {
    FdtBuilder b;
    b.propCells("x", {1}).beginNode("").endNode().end();
    expectError(b.build(), "Property outside of any node");
}

TEST(DTBParserErrors, RejectsPropertyNameOffsetOutOfRange) {
    FdtBuilder b;
    b.beginNode("").propWithNameOff(0x10000, {1, 2, 3, 4}).endNode().end();
    expectError(b.build(), "outside strings block");
}

TEST(DTBParserErrors, RejectsUnterminatedPropertyName) {
    FdtBuilder b;
    b.beginNode("").propCells("name", {1}).endNode().end();
    auto blob = b.build();
    // Drop the strings block's NUL by shrinking size_dt_strings by one.
    uint32_t sz = FdtBuilder::get32(blob, FdtBuilder::OFF_SIZE_STRINGS);
    FdtBuilder::put32(blob, FdtBuilder::OFF_SIZE_STRINGS, sz - 1);
    expectError(blob, "not NUL-terminated");
}

TEST(DTBParserErrors, RejectsEmptyPropertyName) {
    FdtBuilder b;
    b.beginNode("").prop("", {1, 2, 3, 4}).endNode().end();
    expectError(b.build(), "Empty property name");
}

TEST(DTBParserErrors, RejectsPropertyLengthPastStructBlock) {
    FdtBuilder b;
    b.beginNode("").propCells("x", {1}).endNode().end();
    auto blob = b.build();
    // Patch the property's len field (struct + BEGIN_NODE(4) + name(4) + PROP(4))
    size_t lenOff = FdtBuilder::get32(blob, FdtBuilder::OFF_DT_STRUCT) + 12;
    ASSERT_EQ(FdtBuilder::get32(blob, lenOff), 4u);
    FdtBuilder::put32(blob, lenOff, 0xFFFFFFF0);
    expectError(blob, "extends past the structure block");
}

TEST(DTBParserErrors, RejectsUnterminatedNodeName) {
    FdtBuilder b;
    b.beginNode("");
    auto blob = b.build();
    // Structure block is just BEGIN_NODE + padding; overwrite the name
    // padding with non-NUL bytes so the name runs off the block.
    size_t nameOff = FdtBuilder::get32(blob, FdtBuilder::OFF_DT_STRUCT) + 4;
    FdtBuilder::put32(blob, nameOff, 0x41414141);
    expectError(blob, "Node name is not NUL-terminated");
}

// ---------------------------------------------------------------------------
// Robustness: corrupting any single byte must never crash or hang.
// Run under -DDTE_SANITIZE=ON to also catch out-of-bounds reads.
// ---------------------------------------------------------------------------

TEST(DTBParserRobustness, SurvivesEverySingleByteCorruption) {
    const auto original = sampleTree().reserve(0x1000, 0x1000).build();
    DTBParser parser;
    for (size_t i = 0; i < original.size(); ++i) {
        for (uint8_t v : {uint8_t(0x00), uint8_t(0xFF), uint8_t(original[i] ^ 0x01)}) {
            auto blob = original;
            blob[i] = v;
            auto tree = parser.parseBuffer(blob);
            if (!tree) {
                EXPECT_FALSE(parser.lastError().empty()) << "byte " << i;
            }
        }
    }
}

TEST(DTBParserRobustness, SurvivesEveryTruncation) {
    const auto original = sampleTree().build();
    DTBParser parser;
    for (size_t len = 0; len < original.size(); ++len) {
        std::vector<uint8_t> blob(original.begin(), original.begin() + len);
        EXPECT_FALSE(parser.parseBuffer(blob)) << "length " << len;
    }
}
