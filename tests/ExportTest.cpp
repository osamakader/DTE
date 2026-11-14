// JSON/YAML export tests. The exporters are built in; when nlohmann_json or
// yaml-cpp are available the output is parsed back with them.

#include "DeviceTree.h"

#include <gtest/gtest.h>

#ifdef HAVE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#endif
#ifdef HAVE_YAML_CPP
#include <yaml-cpp/yaml.h>
#endif

using namespace DTE;

namespace {

// A tree with every value type and the characters that break naive writers.
std::shared_ptr<DeviceTree> trickyTree() {
    auto root = std::make_shared<DeviceTreeNode>("/");
    root->addProperty(DeviceTreeProperty("model", std::string("say \"hi\" \\ C:\\x\nnext\ttab")));
    root->addProperty(DeviceTreeProperty("compatible", StringList{"vendor,a", "b\", \"c"}));
    root->addProperty(DeviceTreeProperty("#address-cells", std::vector<uint32_t>{2}));
    root->addProperty(DeviceTreeProperty("reg", std::vector<uint32_t>{0x0, 0xffffffff}));
    root->addProperty(DeviceTreeProperty("mac", std::vector<uint8_t>{0x00, 0x0a, 0xff}));
    root->addProperty(DeviceTreeProperty("big", std::vector<uint64_t>{0x100000000ULL}));
    root->addProperty(DeviceTreeProperty("ranges", std::string("")));

    auto z = std::make_shared<DeviceTreeNode>("zeta@1");
    auto a = std::make_shared<DeviceTreeNode>("alpha: weird # name");
    auto leaf = std::make_shared<DeviceTreeNode>("leaf");
    leaf->addProperty(DeviceTreeProperty("status", std::string("okay")));
    root->addChild(z);
    root->addChild(a);
    a->addChild(leaf);

    auto t = std::make_shared<DeviceTree>();
    t->setRoot(root);
    t->setSourceFile("dir with \"quotes\"/board.dts");
    t->setBootCpuId(1);
    t->setMemoryReservations({{0x80000000, 0x1000}, {0x100000000ULL, 0x20}});
    return t;
}

} // namespace

TEST(Export, JsonHasNoHexLiteralsOrDoubleQuotedStrings) {
    auto json = trickyTree()->exportAsJSON();
    EXPECT_EQ(json.find("0x"), std::string::npos);
    EXPECT_EQ(json.find("\"\\\"okay"), std::string::npos);
    EXPECT_NE(json.find("\"status\": \"okay\""), std::string::npos);
}

TEST(Export, EmptyTreeExports) {
    DeviceTree t;
    t.setRoot(nullptr);
    EXPECT_NE(t.exportAsJSON().find("\"root-node\": null"), std::string::npos);
    EXPECT_NE(t.exportAsYAML().find("root-node: null"), std::string::npos);
}

#ifdef HAVE_NLOHMANN_JSON
TEST(Export, JsonRoundTripsAllValueTypes) {
    auto j = nlohmann::ordered_json::parse(trickyTree()->exportAsJSON());
    const auto& dt = j["device-tree"];

    EXPECT_EQ(dt["source-file"], "dir with \"quotes\"/board.dts");
    EXPECT_EQ(dt["boot-cpuid"], 1);
    ASSERT_EQ(dt["memory-reservations"].size(), 2u);
    EXPECT_EQ(dt["memory-reservations"][1]["address"], 0x100000000ULL);
    EXPECT_EQ(dt["memory-reservations"][1]["size"], 0x20);

    const auto& root = dt["root-node"];
    EXPECT_EQ(root["name"], "/");
    EXPECT_EQ(root["path"], "/");
    const auto& p = root["properties"];
    EXPECT_EQ(p["model"], "say \"hi\" \\ C:\\x\nnext\ttab");
    EXPECT_EQ(p["compatible"], (nlohmann::json{"vendor,a", "b\", \"c"}));
    EXPECT_EQ(p["#address-cells"], (nlohmann::json{2}));
    EXPECT_EQ(p["reg"], (nlohmann::json{0, 4294967295u}));
    EXPECT_EQ(p["mac"], (nlohmann::json{0, 10, 255}));
    EXPECT_EQ(p["big"], (nlohmann::json{4294967296ULL}));
    EXPECT_EQ(p["ranges"], "");
}

TEST(Export, JsonPreservesOrder) {
    auto j = nlohmann::ordered_json::parse(trickyTree()->exportAsJSON());
    const auto& root = j["device-tree"]["root-node"];

    std::vector<std::string> propNames;
    for (const auto& [k, _] : root["properties"].items()) propNames.push_back(k);
    EXPECT_EQ(propNames, (std::vector<std::string>{"model", "compatible", "#address-cells", "reg",
                                                   "mac", "big", "ranges"}));

    ASSERT_EQ(root["children"].size(), 2u);
    EXPECT_EQ(root["children"][0]["name"], "zeta@1");
    EXPECT_EQ(root["children"][1]["name"], "alpha: weird # name");
    EXPECT_EQ(root["children"][1]["children"][0]["path"], "/alpha: weird # name/leaf");
    EXPECT_TRUE(root["children"][0]["properties"].empty());
    EXPECT_TRUE(root["children"][0]["children"].empty());
}
#endif

#ifdef HAVE_YAML_CPP
TEST(Export, YamlRoundTripsAllValueTypes) {
    auto y = YAML::Load(trickyTree()->exportAsYAML());
    auto dt = y["device-tree"];

    EXPECT_EQ(dt["source-file"].as<std::string>(), "dir with \"quotes\"/board.dts");
    EXPECT_EQ(dt["boot-cpuid"].as<int>(), 1);
    ASSERT_EQ(dt["memory-reservations"].size(), 2u);
    EXPECT_EQ(dt["memory-reservations"][1]["address"].as<uint64_t>(), 0x100000000ULL);

    auto root = dt["root-node"];
    auto p = root["properties"];
    EXPECT_EQ(p["model"].as<std::string>(), "say \"hi\" \\ C:\\x\nnext\ttab");
    EXPECT_EQ(p["compatible"].as<std::vector<std::string>>(),
              (std::vector<std::string>{"vendor,a", "b\", \"c"}));
    EXPECT_EQ(p["#address-cells"].as<std::vector<uint32_t>>(), (std::vector<uint32_t>{2}));
    EXPECT_EQ(p["reg"].as<std::vector<uint64_t>>(), (std::vector<uint64_t>{0, 0xffffffff}));
    EXPECT_EQ(p["mac"].as<std::vector<int>>(), (std::vector<int>{0, 10, 255}));
    EXPECT_EQ(p["big"].as<std::vector<uint64_t>>(), (std::vector<uint64_t>{0x100000000ULL}));
    EXPECT_EQ(p["ranges"].as<std::string>(), "");
}

TEST(Export, YamlPreservesStructureAndOrder) {
    auto root = YAML::Load(trickyTree()->exportAsYAML())["device-tree"]["root-node"];

    std::vector<std::string> propNames;
    for (const auto& kv : root["properties"]) propNames.push_back(kv.first.as<std::string>());
    EXPECT_EQ(propNames, (std::vector<std::string>{"model", "compatible", "#address-cells", "reg",
                                                   "mac", "big", "ranges"}));

    ASSERT_EQ(root["children"].size(), 2u);
    EXPECT_EQ(root["children"][0]["name"].as<std::string>(), "zeta@1");
    EXPECT_EQ(root["children"][0]["properties"].size(), 0u);
    EXPECT_EQ(root["children"][0]["children"].size(), 0u);
    auto alpha = root["children"][1];
    EXPECT_EQ(alpha["name"].as<std::string>(), "alpha: weird # name");
    ASSERT_EQ(alpha["children"].size(), 1u);
    EXPECT_EQ(alpha["children"][0]["properties"]["status"].as<std::string>(), "okay");
}
#endif
