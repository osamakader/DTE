#include "DeviceTree.h"

#include <gtest/gtest.h>

using namespace DTE;

namespace {

std::shared_ptr<DeviceTreeNode> node(const std::string& name) {
    return std::make_shared<DeviceTreeNode>(name);
}

std::vector<std::string> childNames(const DeviceTreeNode& n) {
    std::vector<std::string> out;
    for (const auto& c : n.getChildren()) out.push_back(c->getName());
    return out;
}

std::vector<std::string> propNames(const DeviceTreeNode& n) {
    std::vector<std::string> out;
    for (const auto& p : n.getProperties()) out.push_back(p.getName());
    return out;
}

DeviceTreeProperty cell(const std::string& name, uint32_t v) {
    return DeviceTreeProperty(name, std::vector<uint32_t>{v});
}

} // namespace

TEST(Node, InsertChildAtIndexAndClamp) {
    auto root = node("/");
    root->addChild(node("a"));
    root->addChild(node("c"));
    root->insertChild(1, node("b"));
    root->insertChild(99, node("d"));
    root->insertChild(0, node("first"));
    EXPECT_EQ(childNames(*root), (std::vector<std::string>{"first", "a", "b", "c", "d"}));
    EXPECT_EQ(root->findChild("b")->getParent(), root);
    EXPECT_EQ(root->findChild("b")->getFullPath(), "/b");
    EXPECT_EQ(root->childIndex(root->findChild("c").get()), 3);
    EXPECT_EQ(root->findChild("zzz"), nullptr);
}

TEST(Node, RemoveChildClearsParent) {
    auto root = node("/");
    auto child = node("x");
    root->addChild(child);
    root->removeChild(child);
    EXPECT_TRUE(root->getChildren().empty());
    EXPECT_EQ(child->getParent(), nullptr);
    EXPECT_EQ(child->getFullPath(), "/x");
    EXPECT_EQ(root->childIndex(child.get()), -1);
}

TEST(Node, AddPropertyReplacesInPlace) {
    auto n = node("n");
    n->addProperty(cell("a", 1));
    n->addProperty(cell("b", 2));
    n->addProperty(cell("c", 3));
    n->addProperty(cell("a", 10));
    EXPECT_EQ(propNames(*n), (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(n->findProperty("a")->getValueAsCells(), std::vector<uint32_t>{10});
}

TEST(Node, InsertPropertyAndIndex) {
    auto n = node("n");
    n->addProperty(cell("a", 1));
    n->addProperty(cell("c", 3));
    n->insertProperty(1, cell("b", 2));
    n->insertProperty(99, cell("d", 4));
    EXPECT_EQ(propNames(*n), (std::vector<std::string>{"a", "b", "c", "d"}));
    EXPECT_EQ(n->propertyIndex("c"), 2);
    EXPECT_EQ(n->propertyIndex("zzz"), -1);

    // Inserting an existing name moves it
    n->insertProperty(0, cell("d", 40));
    EXPECT_EQ(propNames(*n), (std::vector<std::string>{"d", "a", "b", "c"}));
    EXPECT_EQ(n->findProperty("d")->getValueAsCells(), std::vector<uint32_t>{40});
}
