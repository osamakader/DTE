#include "DeviceTree.h"

#include <gtest/gtest.h>

using namespace DTE;

namespace {

std::shared_ptr<DeviceTree> sampleTree() {
    auto root = std::make_shared<DeviceTreeNode>("/");
    root->addProperty(DeviceTreeProperty("model", std::string("ACME Board")));
    auto soc = std::make_shared<DeviceTreeNode>("soc");
    auto uart = std::make_shared<DeviceTreeNode>("serial@1000");
    uart->addProperty(DeviceTreeProperty("compatible", StringList{"ns16550a", "acme,uart"}));
    uart->addProperty(DeviceTreeProperty("reg", std::vector<uint32_t>{0x1000, 0x100}));
    auto uart2 = std::make_shared<DeviceTreeNode>("Serial@2000");
    auto cpus = std::make_shared<DeviceTreeNode>("cpus");
    root->addChild(soc);
    soc->addChild(uart);
    soc->addChild(uart2);
    root->addChild(cpus);
    auto t = std::make_shared<DeviceTree>();
    t->setRoot(root);
    return t;
}

std::vector<std::string> keys(const std::vector<SearchMatch>& m) {
    std::vector<std::string> out;
    for (const auto& x : m) {
        out.push_back(x.node->getFullPath() + (x.propertyName.empty() ? "" : ":" + x.propertyName));
    }
    return out;
}

} // namespace

TEST(Search, MatchesNodeNamesCaseInsensitivelyInTreeOrder) {
    EXPECT_EQ(keys(sampleTree()->search("serial")),
              (std::vector<std::string>{"/soc/serial@1000", "/soc/Serial@2000"}));
}

TEST(Search, CaseSensitiveOption) {
    SearchOptions o;
    o.caseSensitive = true;
    EXPECT_EQ(keys(sampleTree()->search("Serial", o)), (std::vector<std::string>{"/soc/Serial@2000"}));
}

TEST(Search, QueryWithSlashMatchesFullPath) {
    EXPECT_EQ(keys(sampleTree()->search("soc/ser")),
              (std::vector<std::string>{"/soc/serial@1000", "/soc/Serial@2000"}));
    // Without a slash only the node name counts, so "soc" does not match children
    EXPECT_EQ(keys(sampleTree()->search("soc")), (std::vector<std::string>{"/soc"}));
}

TEST(Search, PropertiesAreOptIn) {
    EXPECT_TRUE(sampleTree()->search("ns16550").empty());

    SearchOptions o;
    o.properties = true;
    EXPECT_EQ(keys(sampleTree()->search("ns16550", o)), (std::vector<std::string>{"/soc/serial@1000:compatible"}));
    EXPECT_EQ(keys(sampleTree()->search("model", o)), (std::vector<std::string>{"/:model"}));
    // Values match in their DTS rendering, e.g. cells as hex
    EXPECT_EQ(keys(sampleTree()->search("0x1000", o)), (std::vector<std::string>{"/soc/serial@1000:reg"}));
}

TEST(Search, NodeAndPropertyHitsAreSeparate) {
    SearchOptions o;
    o.properties = true;
    EXPECT_EQ(keys(sampleTree()->search("acme", o)),
              (std::vector<std::string>{"/:model", "/soc/serial@1000:compatible"}));
    EXPECT_EQ(keys(sampleTree()->search("serial", o)),
              (std::vector<std::string>{"/soc/serial@1000", "/soc/Serial@2000"}));
}

TEST(Search, EmptyQueryOrTreeHasNoMatches) {
    EXPECT_TRUE(sampleTree()->search("").empty());
    DeviceTree empty;
    empty.setRoot(nullptr);
    EXPECT_TRUE(empty.search("x").empty());
}
