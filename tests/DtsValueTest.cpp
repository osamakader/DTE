#include "DtsValue.h"

#include <gtest/gtest.h>

using namespace DTE;

namespace {

PropertyValue ok(const std::string& text) {
    PropertyValue v;
    std::string error;
    EXPECT_TRUE(parseDtsValue(text, v, &error)) << "'" << text << "': " << error;
    return v;
}

std::string err(const std::string& text) {
    PropertyValue v = std::string("untouched");
    std::string error;
    EXPECT_FALSE(parseDtsValue(text, v, &error)) << "'" << text << "' should not parse";
    EXPECT_EQ(std::get<std::string>(v), "untouched") << "output must not change on failure";
    return error;
}

using Cells = std::vector<uint32_t>;
using Cells64 = std::vector<uint64_t>;
using Bytes = std::vector<uint8_t>;

} // namespace

TEST(DtsValue, Empty) {
    EXPECT_EQ(ok(""), PropertyValue(std::string()));
    EXPECT_EQ(ok("   "), PropertyValue(std::string()));
    EXPECT_EQ(ok(";"), PropertyValue(std::string()));
}

TEST(DtsValue, Strings) {
    EXPECT_EQ(ok("\"okay\""), PropertyValue(std::string("okay")));
    EXPECT_EQ(ok("  \"okay\" ; "), PropertyValue(std::string("okay")));
    EXPECT_EQ(ok(R"("a", "b")"), PropertyValue(StringList{"a", "b"}));
    EXPECT_EQ(ok(R"("a","b" ,"c")"), PropertyValue(StringList{"a", "b", "c"}));
    // "" is a single NUL byte, not an empty property
    EXPECT_EQ(ok(R"("")"), PropertyValue(Bytes{0}));
}

TEST(DtsValue, StringEscapes) {
    EXPECT_EQ(ok(R"("say \"hi\" \\ \n\t\r")"), PropertyValue(std::string("say \"hi\" \\ \n\t\r")));
    EXPECT_EQ(ok(R"("\x41\x4a\101")"), PropertyValue(std::string("AJA")));
    EXPECT_EQ(ok(R"("a,b; c=<d>")"), PropertyValue(std::string("a,b; c=<d>")));
}

TEST(DtsValue, Cells) {
    EXPECT_EQ(ok("<0x1000 0x100>"), PropertyValue(Cells{0x1000, 0x100}));
    EXPECT_EQ(ok("<1843200 017 'A'>"), PropertyValue(Cells{1843200, 017, 'A'}));
    EXPECT_EQ(ok("< 1  2 >"), PropertyValue(Cells{1, 2}));
    EXPECT_EQ(ok("<0xffffffff 10UL>"), PropertyValue(Cells{0xffffffff, 10}));
    EXPECT_EQ(ok("<>"), PropertyValue(Cells{}));
    // Several cell groups are concatenated, as in dtc
    EXPECT_EQ(ok("<0x1000 0x1000>, <0x2000 0x2000>"), PropertyValue(Cells{0x1000, 0x1000, 0x2000, 0x2000}));
}

TEST(DtsValue, SixtyFourBitCells) {
    EXPECT_EQ(ok("/bits/ 64 <0x100000000 1>"), PropertyValue(Cells64{0x100000000ULL, 1}));
    EXPECT_EQ(ok("/bits/ 32 <5>"), PropertyValue(Cells{5}));
}

TEST(DtsValue, Bytes) {
    EXPECT_EQ(ok("[00 11 aa FF]"), PropertyValue(Bytes{0x00, 0x11, 0xaa, 0xff}));
    EXPECT_EQ(ok("[0011aaFF]"), PropertyValue(Bytes{0x00, 0x11, 0xaa, 0xff}));
    EXPECT_EQ(ok("[01], [02 03]"), PropertyValue(Bytes{1, 2, 3}));
    EXPECT_EQ(ok("[]"), PropertyValue(Bytes{}));
}

TEST(DtsValue, ErrorsAreSpecific) {
    EXPECT_NE(err("okay").find("Expected a string"), std::string::npos);
    EXPECT_NE(err("\"open").find("Unterminated string"), std::string::npos);
    EXPECT_NE(err("<1 2").find("Missing '>'"), std::string::npos);
    EXPECT_NE(err("<0x1g>").find("Invalid number '0x1g'"), std::string::npos);
    EXPECT_NE(err("<0x100000000>").find("does not fit in 32 bits"), std::string::npos);
    EXPECT_NE(err("<&uart0>").find("Label references"), std::string::npos);
    EXPECT_NE(err("&uart0").find("Label references"), std::string::npos);
    EXPECT_NE(err("<(1 + 2)>").find("Expressions are not supported"), std::string::npos);
    EXPECT_NE(err("<-1>").find("Negative numbers"), std::string::npos);
    EXPECT_NE(err("[0 1]").find("even number of hex digits"), std::string::npos);
    EXPECT_NE(err("[001]").find("even number of hex digits"), std::string::npos);
    EXPECT_NE(err("[zz]").find("Invalid character 'z'"), std::string::npos);
    EXPECT_NE(err("\"a\" \"b\"").find("Expected ','"), std::string::npos);
    EXPECT_NE(err("\"a\",").find("Expected a value after ','"), std::string::npos);
    EXPECT_NE(err("\"a\", <1>").find("Cannot mix strings and 32-bit cells"), std::string::npos);
    EXPECT_NE(err("/bits/ 16 <1>").find("Only /bits/ 32 and /bits/ 64"), std::string::npos);
    EXPECT_NE(err(R"("\q")").find("Unknown escape \\q"), std::string::npos);
}

TEST(DtsValue, ErrorsReportColumn) {
    EXPECT_NE(err("<1 2 xyz>").find("column 6"), std::string::npos) << err("<1 2 xyz>");
}

TEST(DtsValue, RenderedValuesParseBack) {
    const std::vector<PropertyValue> values = {
        std::string(),
        std::string("okay"),
        std::string("quote \" backslash \\ newline \n tab \t ctrl \x01 high \xff"),
        StringList{"vendor,a", "b\", \"c", "d"},
        Cells{},
        Cells{0, 1, 0xdeadbeef, 0xffffffff},
        Cells64{0, 0xffffffffffffffffULL},
        Bytes{},
        Bytes{0x00, 0x0a, 0xff},
    };
    for (const auto& v : values) {
        const std::string text = DeviceTreeProperty("p", v).getValueAsString();
        PropertyValue back;
        std::string error;
        ASSERT_TRUE(parseDtsValue(text, back, &error)) << text << ": " << error;
        EXPECT_EQ(back, v) << "text: " << text;
    }
}

TEST(NameValidation, NodeNames) {
    for (const char* good : {"cpus", "cpu@0", "serial@10000000", "i2c-mux@70", "a,b_c.d+e-f@1,2"}) {
        EXPECT_TRUE(isValidNodeName(good)) << good;
    }
    std::string error;
    EXPECT_FALSE(isValidNodeName("", &error));
    EXPECT_FALSE(isValidNodeName("@1", &error));
    EXPECT_NE(error.find("before '@'"), std::string::npos);
    EXPECT_FALSE(isValidNodeName("cpu@", &error));
    EXPECT_NE(error.find("Unit address"), std::string::npos);
    EXPECT_FALSE(isValidNodeName("a@1@2", &error));
    EXPECT_FALSE(isValidNodeName("has space", &error));
    EXPECT_NE(error.find("invalid character ' '"), std::string::npos);
    EXPECT_FALSE(isValidNodeName("a/b", &error));
    EXPECT_FALSE(isValidNodeName("#cells", &error));
}

TEST(NameValidation, PropertyNames) {
    for (const char* good : {"reg", "#address-cells", "linux,phandle", "a?b", "x.y+z_w"}) {
        EXPECT_TRUE(isValidPropertyName(good)) << good;
    }
    std::string error;
    EXPECT_FALSE(isValidPropertyName("", &error));
    EXPECT_FALSE(isValidPropertyName("has space", &error));
    EXPECT_FALSE(isValidPropertyName("a@b", &error));
    EXPECT_FALSE(isValidPropertyName("a=b", &error));
}

#ifdef DTC_PATH
#include "DeviceTreeParser.h"
#include "TestUtil.h"

#include <cstdlib>

// The editor's parser must agree with dtc: for each text, the bytes we would
// write equal the bytes dtc produces for `p = <text>;`.
TEST(DtsValue, AgreesWithDtc) {
    ::setenv("DTE_DTC", DTC_PATH, 1);
    DTE::test::TempDir tmp;
    const std::vector<std::string> texts = {
        R"("okay")",
        R"("a", "b", "c")",
        R"("")",
        R"("esc \" \\ \n \t \x41 \101")",
        "<0x1000 0x100>",
        "<1843200 017 'A'>",
        "<1>, <2 3>",
        "/bits/ 64 <0x100000000 1>",
        "[00 11 aa FF]",
        "[0011], [22]",
    };
    for (const auto& text : texts) {
        PropertyValue ours;
        std::string error;
        ASSERT_TRUE(parseDtsValue(text, ours, &error)) << text << ": " << error;

        // Our side: encode via the DTB writer and read back
        auto root = std::make_shared<DeviceTreeNode>("/");
        root->addProperty(DeviceTreeProperty("p", ours));
        DeviceTree mine;
        mine.setRoot(root);
        DTBParser parser;
        auto ourTree = parser.parseBuffer(mine.exportAsDTB());
        ASSERT_TRUE(ourTree) << parser.lastError();

        // dtc's side
        DeviceTree theirs;
        ASSERT_TRUE(theirs.loadFromFile(tmp.write("v.dts", "/dts-v1/;\n/ { p = " + text + "; };\n")))
            << text << ": " << theirs.getLoadError();

        EXPECT_EQ(ourTree->getRoot()->findProperty("p")->getValue(),
                  theirs.getRoot()->findProperty("p")->getValue())
            << "text: " << text;
    }
    ::unsetenv("DTE_DTC");
}
#endif
