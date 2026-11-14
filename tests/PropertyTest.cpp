#include "DeviceTree.h"

#include <gtest/gtest.h>

using DTE::DeviceTreeProperty;
using DTE::StringList;

namespace {
std::string render(const DTE::PropertyValue& v) {
    return DeviceTreeProperty("p", v).getValueAsString();
}
} // namespace

TEST(Property, RendersStringsInDtsSyntax) {
    EXPECT_EQ(render(std::string("okay")), "\"okay\"");
    EXPECT_EQ(render(std::string("")), "\"\"");
}

TEST(Property, EscapesSpecialCharacters) {
    EXPECT_EQ(render(std::string("say \"hi\"")), R"("say \"hi\"")");
    EXPECT_EQ(render(std::string("C:\\path")), R"("C:\\path")");
    EXPECT_EQ(render(std::string("a\nb\tc")), R"("a\nb\tc")");
    EXPECT_EQ(render(std::string("\x01\x7f")), R"("\x01\x7f")");
}

TEST(Property, RendersStringLists) {
    EXPECT_EQ(render(StringList{"vendor,a", "vendor,b"}), R"("vendor,a", "vendor,b")");
    EXPECT_EQ(render(StringList{"a\", \"b", "c"}), R"("a\", \"b", "c")");
}

TEST(Property, RendersCellsAndBytesLikeDtc) {
    EXPECT_EQ(render(std::vector<uint32_t>{0x1, 0xdeadbeef}), "<0x1 0xdeadbeef>");
    EXPECT_EQ(render(std::vector<uint8_t>{0x00, 0x0a, 0xff}), "[00 0a ff]");
    EXPECT_EQ(render(std::vector<uint64_t>{0x100000000ULL}), "/bits/ 64 <0x100000000>");
}

TEST(Property, GetValueAsStrings) {
    EXPECT_EQ(DeviceTreeProperty("p", std::string("x")).getValueAsStrings(), StringList{"x"});
    EXPECT_EQ(DeviceTreeProperty("p", StringList{"a", "b"}).getValueAsStrings(), (StringList{"a", "b"}));
    EXPECT_TRUE(DeviceTreeProperty("p", std::vector<uint32_t>{1}).getValueAsStrings().empty());
}

TEST(Property, EqualityComparesNameTypeAndValue) {
    DeviceTreeProperty a("p", std::vector<uint32_t>{1});
    EXPECT_EQ(a, DeviceTreeProperty("p", std::vector<uint32_t>{1}));
    EXPECT_NE(a, DeviceTreeProperty("q", std::vector<uint32_t>{1}));
    EXPECT_NE(a, DeviceTreeProperty("p", std::vector<uint32_t>{2}));
    EXPECT_NE(a, DeviceTreeProperty("p", std::vector<uint8_t>{0, 0, 0, 1}));
}
