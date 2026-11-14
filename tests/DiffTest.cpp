#include "DeviceTreeDiff.h"

#include <gtest/gtest.h>

#ifdef HAVE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#endif
#ifdef HAVE_YAML_CPP
#include <yaml-cpp/yaml.h>
#endif

using namespace DTE;

namespace {

std::shared_ptr<DeviceTreeNode> node(const std::string& name) {
    return std::make_shared<DeviceTreeNode>(name);
}

std::shared_ptr<DeviceTree> treeWith(const std::shared_ptr<DeviceTreeNode>& root) {
    auto t = std::make_shared<DeviceTree>();
    t->setRoot(root);
    return t;
}

DeviceTreeProperty cellsProp(const std::string& name, std::vector<uint32_t> v) {
    return DeviceTreeProperty(name, std::move(v));
}

DeviceTreeProperty strProp(const std::string& name, const std::string& v) {
    return DeviceTreeProperty(name, v);
}

// base:                       overlay:
// / model="A"                 / model="B \"q\""
//   zeta { x=<1> }              zeta { x=<1> }
//   alpha { gone=<2>            keep { s="same" m=<2> extra="e" }
//     sub { y=<3> } }           newnode { n=<5> }
//   keep { s="same" m=<1> }
std::pair<std::shared_ptr<DeviceTree>, std::shared_ptr<DeviceTree>> samplePair() {
    auto b = node("/");
    b->addProperty(strProp("model", "A"));
    auto bz = node("zeta");
    bz->addProperty(cellsProp("x", {1}));
    auto ba = node("alpha");
    ba->addProperty(cellsProp("gone", {2}));
    auto bsub = node("sub");
    bsub->addProperty(cellsProp("y", {3}));
    auto bk = node("keep");
    bk->addProperty(strProp("s", "same"));
    bk->addProperty(cellsProp("m", {1}));
    b->addChild(bz);
    b->addChild(ba);
    ba->addChild(bsub);
    b->addChild(bk);

    auto o = node("/");
    o->addProperty(strProp("model", "B \"q\""));
    auto oz = node("zeta");
    oz->addProperty(cellsProp("x", {1}));
    auto ok = node("keep");
    ok->addProperty(strProp("s", "same"));
    ok->addProperty(cellsProp("m", {2}));
    ok->addProperty(strProp("extra", "e"));
    auto on = node("newnode");
    on->addProperty(cellsProp("n", {5}));
    o->addChild(oz);
    o->addChild(ok);
    o->addChild(on);

    return {treeWith(b), treeWith(o)};
}

std::string key(const DiffEntry& e) {
    const char* t = e.type == DiffType::Added ? "+" : e.type == DiffType::Removed ? "-" : "~";
    return t + e.path + (e.propertyName.empty() ? "" : ":" + e.propertyName);
}

} // namespace

TEST(Diff, IdenticalTreesHaveNoChanges) {
    auto [base, _] = samplePair();
    auto [copy, __] = samplePair();
    DeviceTreeDiff diff(base, copy);
    EXPECT_EQ(diff.getTotalChanges(), 0u);
    EXPECT_TRUE(diff.generateDiff().empty());
}

TEST(Diff, ReportsChangesWithSingleSlashPathsInSourceOrder) {
    auto [base, overlay] = samplePair();
    DeviceTreeDiff diff(base, overlay);

    std::vector<std::string> keys;
    for (const auto& e : diff.generateDiff()) keys.push_back(key(e));

    // Base order first (removed/modified inline), overlay-only additions after.
    EXPECT_EQ(keys, (std::vector<std::string>{
                        "~/:model",
                        "-/alpha",
                        "-/alpha:gone",
                        "-/alpha/sub",
                        "-/alpha/sub:y",
                        "~/keep:m",
                        "+/keep:extra",
                        "+/newnode",
                        "+/newnode:n",
                    }));
}

TEST(Diff, CountsSumToTotal) {
    auto [base, overlay] = samplePair();
    DeviceTreeDiff diff(base, overlay);
    EXPECT_EQ(diff.getTotalChanges(), 9u);
    EXPECT_EQ(diff.getAddedCount(), 3u);
    EXPECT_EQ(diff.getRemovedCount(), 4u);
    EXPECT_EQ(diff.getModifiedCount(), 2u);
    EXPECT_EQ(diff.getAddedCount() + diff.getRemovedCount() + diff.getModifiedCount(),
              diff.getTotalChanges());

    EXPECT_EQ(diff.getAddedNodes().size(), 1u);
    EXPECT_EQ(diff.getRemovedNodes().size(), 2u);
    EXPECT_EQ(diff.getModifiedProperties().size(), 2u);
}

TEST(Diff, CarriesOldAndNewValuesInDtsSyntax) {
    auto [base, overlay] = samplePair();
    DeviceTreeDiff diff(base, overlay);
    auto changes = diff.generateDiff();

    EXPECT_EQ(changes[0].oldValue, "\"A\"");
    EXPECT_EQ(changes[0].newValue, R"("B \"q\"")");
    EXPECT_EQ(changes[2].oldValue, "<0x2>");    // removed property of removed node
    EXPECT_EQ(changes[8].newValue, "<0x5>");    // added property of added node
}

TEST(Diff, TypeChangeIsAModification) {
    auto b = node("/");
    b->addProperty(DeviceTreeProperty("p", std::vector<uint32_t>{1}));
    auto o = node("/");
    o->addProperty(DeviceTreeProperty("p", std::vector<uint8_t>{0, 0, 0, 1}));
    DeviceTreeDiff diff(treeWith(b), treeWith(o));
    ASSERT_EQ(diff.getTotalChanges(), 1u);
    EXPECT_EQ(diff.generateDiff()[0].type, DiffType::Modified);
}

TEST(Diff, VisualizerStatsMatch) {
    auto [base, overlay] = samplePair();
    DeviceTreeDiff diff(base, overlay);
    DiffVisualizer vis(diff);
    auto stats = vis.getStats();
    EXPECT_EQ(stats.totalChanges, 9u);
    EXPECT_EQ(stats.addedNodes, 1u);
    EXPECT_EQ(stats.removedNodes, 2u);
    EXPECT_EQ(stats.addedProperties, 2u);
    EXPECT_EQ(stats.removedProperties, 2u);
    EXPECT_EQ(stats.modifiedProperties, 2u);
}

TEST(Diff, NullTreesAreInvalid) {
    DeviceTreeDiff diff(nullptr, nullptr);
    EXPECT_FALSE(diff.isValid());
    EXPECT_EQ(diff.getValidationErrors().size(), 2u);
    EXPECT_EQ(diff.getTotalChanges(), 0u);
}

#ifdef HAVE_NLOHMANN_JSON
TEST(Diff, JsonExportIsValidAndEscaped) {
    auto [base, overlay] = samplePair();
    DeviceTreeDiff diff(base, overlay);
    auto j = nlohmann::json::parse(diff.exportAsJSON());

    EXPECT_EQ(j["diff"]["total_changes"], 9);
    EXPECT_EQ(j["diff"]["added"], 3);
    ASSERT_EQ(j["diff"]["changes"].size(), 9u);
    const auto& first = j["diff"]["changes"][0];
    EXPECT_EQ(first["type"], "modified");
    EXPECT_EQ(first["path"], "/");
    EXPECT_EQ(first["property"], "model");
    EXPECT_EQ(first["old_value"], "\"A\"");
    EXPECT_EQ(first["new_value"], R"("B \"q\"")");
}

TEST(Diff, JsonExportOfEmptyDiffIsValid) {
    auto [base, _] = samplePair();
    auto [copy, __] = samplePair();
    auto j = nlohmann::json::parse(DeviceTreeDiff(base, copy).exportAsJSON());
    EXPECT_TRUE(j["diff"]["changes"].empty());
}
#endif

#ifdef HAVE_YAML_CPP
TEST(Diff, YamlExportIsValidAndEscaped) {
    auto [base, overlay] = samplePair();
    DeviceTreeDiff diff(base, overlay);
    auto y = YAML::Load(diff.exportAsYAML());

    EXPECT_EQ(y["diff"]["total_changes"].as<int>(), 9);
    ASSERT_EQ(y["diff"]["changes"].size(), 9u);
    EXPECT_EQ(y["diff"]["changes"][0]["new_value"].as<std::string>(), R"("B \"q\"")");
    EXPECT_EQ(y["diff"]["changes"][0]["path"].as<std::string>(), "/");
}

TEST(Diff, YamlExportOfEmptyDiffIsValid) {
    auto [base, _] = samplePair();
    auto [copy, __] = samplePair();
    auto y = YAML::Load(DeviceTreeDiff(base, copy).exportAsYAML());
    EXPECT_EQ(y["diff"]["changes"].size(), 0u);
}
#endif
