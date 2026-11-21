// Reading device trees exposed as directories, like /proc/device-tree.
// Tests build such directories from known trees.

#include "DeviceTreeDiff.h"
#include "DeviceTreeParser.h"
#include "Process.h"
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace DTE;
using DTE::test::TempDir;

namespace {

// Writes `node` the way the kernel exposes it: a directory per node, a file
// per property with its raw bytes, plus a synthesized `name` file.
void writeAsDirectory(const DeviceTreeNode& node, const fs::path& dir) {
    fs::create_directories(dir);
    for (const auto& prop : node.getProperties()) {
        const auto bytes = propertyValueToBytes(prop.getValue());
        std::ofstream(dir / prop.getName(), std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const std::string base = node.getName() == "/" ? "" : node.getName().substr(0, node.getName().find('@'));
    std::ofstream(dir / "name", std::ios::binary) << base << '\0';
    for (const auto& child : node.getChildren()) {
        writeAsDirectory(*child, dir / child->getName());
    }
}

std::shared_ptr<DeviceTree> sampleTree() {
    auto t = std::make_shared<DeviceTree>();
    auto root = t->getRoot();
    root->addProperty(DeviceTreeProperty("model", std::string("Sample")));
    root->addProperty(DeviceTreeProperty("compatible", StringList{"acme,a", "acme,b"}));
    root->addProperty(DeviceTreeProperty("#address-cells", std::vector<uint32_t>{1}));
    auto soc = std::make_shared<DeviceTreeNode>("soc");
    soc->addProperty(DeviceTreeProperty("ranges", std::string()));
    auto uart = std::make_shared<DeviceTreeNode>("serial@1000");
    uart->addProperty(DeviceTreeProperty("reg", std::vector<uint32_t>{0x1000, 0x100}));
    uart->addProperty(DeviceTreeProperty("local-mac-address", std::vector<uint8_t>{1, 2, 3, 4, 5, 6}));
    root->addChild(soc);
    soc->addChild(uart);
    root->addChild(std::make_shared<DeviceTreeNode>("aliases"));
    return t;
}

std::string diffReport(const std::shared_ptr<DeviceTree>& a, const std::shared_ptr<DeviceTree>& b) {
    DeviceTreeDiff diff(a, b);
    return diff.getTotalChanges() == 0 ? std::string() : DiffVisualizer(diff).getFormattedDiff();
}

} // namespace

TEST(SystemTree, ReadsDirectoryLikeProcDeviceTree) {
    TempDir tmp;
    const auto dir = tmp.path() / "device-tree";
    auto original = sampleTree();
    writeAsDirectory(*original->getRoot(), dir);

    auto loaded = std::make_shared<DeviceTree>();
    ASSERT_TRUE(loaded->loadFromFile(dir.string())) << loaded->getLoadError();
    EXPECT_EQ(loaded->getSourceFile(), dir.string());
    EXPECT_EQ(diffReport(original, loaded), "");

    // Kernel-synthesized name files are not properties
    EXPECT_FALSE(loaded->getRoot()->findProperty("name"));
    EXPECT_FALSE(loaded->findNodeByPath("/soc/serial@1000")->findProperty("name"));
    // Entries are sorted, since directories have no order
    const auto& children = loaded->getRoot()->getChildren();
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0]->getName(), "aliases");
    EXPECT_EQ(children[1]->getName(), "soc");
}

TEST(SystemTree, FactoryChoosesDirectoryParser) {
    TempDir tmp;
    auto parser = DeviceTreeParserFactory::createParser(tmp.path().string());
    ASSERT_TRUE(parser);
    EXPECT_TRUE(dynamic_cast<DirectoryParser*>(parser.get()));
    EXPECT_FALSE(DirectoryParser().canParse((tmp.path() / "missing").string()));
}

TEST(SystemTree, SymlinksAreNotFollowed) {
    TempDir tmp;
    const auto dir = tmp.path() / "dt";
    writeAsDirectory(*sampleTree()->getRoot(), dir);
    fs::create_directory_symlink(dir, dir / "soc" / "loop");
    fs::create_symlink(dir / "model", dir / "model-link");

    DeviceTree loaded;
    ASSERT_TRUE(loaded.loadFromFile(dir.string())) << loaded.getLoadError();
    EXPECT_FALSE(loaded.findNodeByPath("/soc/loop"));
    EXPECT_FALSE(loaded.getRoot()->findProperty("model-link"));
}

TEST(SystemTree, UnreadableEntriesAreReported) {
    if (::geteuid() == 0) GTEST_SKIP() << "permissions are not enforced for root";
    TempDir tmp;
    const auto dir = tmp.path() / "dt";
    writeAsDirectory(*sampleTree()->getRoot(), dir);
    fs::permissions(dir / "soc" / "serial@1000" / "reg", fs::perms::none);

    DeviceTree loaded;
    EXPECT_FALSE(loaded.loadFromFile(dir.string()));
    EXPECT_NE(loaded.getLoadError().find("serial@1000/reg"), std::string::npos) << loaded.getLoadError();

    // An unreadable blob (like /sys/firmware/fdt for normal users)
    const auto blob = tmp.path() / "fdt";
    std::ofstream(blob) << "x";
    fs::permissions(blob, fs::perms::none);
    EXPECT_FALSE(loaded.loadFromFile(blob.string()));
    EXPECT_NE(loaded.getLoadError().find("Permission denied"), std::string::npos) << loaded.getLoadError();
}

TEST(SystemTree, SystemPathIsEmptyOrADirectory) {
    const std::string path = systemDeviceTreePath();
    if (!path.empty()) {
        EXPECT_TRUE(fs::is_directory(path));
    }
}

#ifdef DTC_PATH
// dtc reads the same layout with `-I fs`; both must agree.
TEST(SystemTree, AgreesWithDtcFsInput) {
    ::setenv("DTE_DTC", DTC_PATH, 1);
    TempDir tmp;
    auto board = std::make_shared<DeviceTree>();
    ASSERT_TRUE(board->loadFromFile(std::string(TEST_DATA_DIR) + "/board.dts")) << board->getLoadError();
    const auto dir = tmp.path() / "device-tree";
    writeAsDirectory(*board->getRoot(), dir);

    const auto viaDtc = (tmp.path() / "via-dtc.dtb").string();
    auto r = runProcess({DTC_PATH, "-q", "-I", "fs", "-O", "dtb", "-o", viaDtc, dir.string()});
    ASSERT_TRUE(r.ok()) << r.stderrData;

    auto ours = std::make_shared<DeviceTree>();
    ASSERT_TRUE(ours->loadFromFile(dir.string())) << ours->getLoadError();
    auto theirs = std::make_shared<DeviceTree>();
    ASSERT_TRUE(theirs->loadFromFile(viaDtc)) << theirs->getLoadError();

    EXPECT_EQ(diffReport(theirs, ours), "");
    EXPECT_EQ(diffReport(board, ours), "");
    ::unsetenv("DTE_DTC");
}
#endif
