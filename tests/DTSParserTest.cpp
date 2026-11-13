// DTS parsing goes through dtc (and cpp when needed); these tests run the
// real tools and are skipped when dtc was not found at configure time.

#include "DeviceTreeDiff.h"
#include "DeviceTreeParser.h"
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <variant>

using DTE::DeviceTree;
using DTE::DeviceTreeDiff;
using DTE::DeviceTreeParserFactory;
using DTE::DTBParser;
using DTE::DTSParser;
using DTE::MemoryReservation;
using DTE::test::countNodes;
using DTE::test::findProp;
using DTE::test::TempDir;

namespace {

const std::string kData = TEST_DATA_DIR;

class DTSParserTest : public ::testing::Test {
protected:
    void SetUp() override {
#ifndef DTC_PATH
        GTEST_SKIP() << "dtc not available";
#else
        ::setenv("DTE_DTC", DTC_PATH, 1);
        ::unsetenv("DTE_CPP");
#endif
    }

    void TearDown() override {
        ::unsetenv("DTE_DTC");
        ::unsetenv("DTE_CPP");
    }

    // Parses DTS text written to a temporary file; fails the test on error.
    std::shared_ptr<DeviceTree> parseText(const std::string& dts) {
        auto file = tmp_.write("input.dts", dts);
        DTSParser parser;
        auto tree = parser.parse(file);
        EXPECT_TRUE(tree) << parser.lastError();
        return tree;
    }

    std::vector<uint32_t> cells(const DeviceTree& t, const std::string& path, const std::string& name) {
        auto* p = findProp(t, path, name);
        return p ? p->getValueAsCells() : std::vector<uint32_t>{};
    }

    std::string rawString(const DeviceTree& t, const std::string& path, const std::string& name) {
        auto* p = findProp(t, path, name);
        if (!p) return "<missing>";
        if (!p->isString()) return "<not a string>";
        return std::get<std::string>(p->getValue());
    }

    TempDir tmp_;
};

} // namespace

// ---------------------------------------------------------------------------
// Equivalence with the DTB path
// ---------------------------------------------------------------------------

TEST_F(DTSParserTest, DtsAndItsCompiledDtbLoadIdentically) {
    const std::string dts = kData + "/board.dts";
    const std::string dtb = (tmp_.path() / "board.dtb").string();
    ASSERT_TRUE(DTE::DeviceTreeUtils::dtsToDtb(dts, dtb));

    auto fromDts = std::make_shared<DeviceTree>();
    auto fromDtb = std::make_shared<DeviceTree>();
    ASSERT_TRUE(fromDts->loadFromFile(dts));
    ASSERT_TRUE(fromDtb->loadFromFile(dtb));

    DeviceTreeDiff diff(fromDts, fromDtb);
    EXPECT_EQ(diff.getTotalChanges(), 0u);
    EXPECT_EQ(countNodes(fromDts->getRoot()), countNodes(fromDtb->getRoot()));
    EXPECT_EQ(fromDts->getMemoryReservations(), fromDtb->getMemoryReservations());
    EXPECT_EQ(fromDts->getMemoryReservations(),
              (std::vector<MemoryReservation>{{0x88000000, 0x100000}, {0x8a000000, 0x2000}}));
}

TEST_F(DTSParserTest, FactoryRoutesSourceSuffixesToDtsParser) {
    for (const char* name : {"a.dts", "a.dtsi", "a.dtso"}) {
        auto parser = DeviceTreeParserFactory::createParser(tmp_.write(name, "/dts-v1/; / {};"));
        ASSERT_TRUE(parser) << name;
        EXPECT_TRUE(dynamic_cast<DTSParser*>(parser.get())) << name;
    }
}

TEST_F(DTSParserTest, SetsSourceFile) {
    DTSParser parser;
    auto tree = parser.parse(kData + "/board.dts");
    ASSERT_TRUE(tree);
    EXPECT_EQ(tree->getSourceFile(), kData + "/board.dts");
}

// ---------------------------------------------------------------------------
// Regressions for the old line-based parser
// ---------------------------------------------------------------------------

TEST_F(DTSParserTest, DecimalCellsAreDecimal) {
    auto t = parseText("/dts-v1/; / { f = <1843200 1200000000 0x10>; };");
    ASSERT_TRUE(t);
    EXPECT_EQ(cells(*t, "/", "f"), (std::vector<uint32_t>{1843200, 1200000000, 0x10}));
}

TEST_F(DTSParserTest, SingleLineNodesDoNotEndTheTree) {
    auto t = parseText(R"(/dts-v1/;
/ {
	a { x = <1>; };
	b { y = <2>; };
	c {
		z = <3>;
	};
};
)");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->getRoot()->getChildren().size(), 3u);
    EXPECT_EQ(cells(*t, "/a", "x"), (std::vector<uint32_t>{1}));
    EXPECT_EQ(cells(*t, "/b", "y"), (std::vector<uint32_t>{2}));
    EXPECT_EQ(cells(*t, "/c", "z"), (std::vector<uint32_t>{3}));
}

TEST_F(DTSParserTest, EscapedQuotesDoNotSwallowFollowingLines) {
    auto t = parseText(R"(/dts-v1/;
/ {
	model = "say \"hi\"";
	next = <7>;
};
)");
    ASSERT_TRUE(t);
    EXPECT_EQ(rawString(*t, "/", "model"), "say \"hi\"");
    EXPECT_EQ(cells(*t, "/", "next"), (std::vector<uint32_t>{7}));
}

TEST_F(DTSParserTest, BlockCommentsAreIgnored) {
    auto t = parseText(R"(/dts-v1/;
/ {
	/* hidden = <1>;
	   also-hidden = <2>; */
	visible = <3>; // trailing comment
};
)");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->getRoot()->getProperties().size(), 1u);
    EXPECT_EQ(cells(*t, "/", "visible"), (std::vector<uint32_t>{3}));
}

TEST_F(DTSParserTest, MultiLineCellsAndExpressions) {
    auto t = parseText(R"(/dts-v1/;
/ {
	multi = <1 2
	         3 4>;
	expr = <(1 + 2) (0x10 << 4) (7 - 3)>;
};
)");
    ASSERT_TRUE(t);
    EXPECT_EQ(cells(*t, "/", "multi"), (std::vector<uint32_t>{1, 2, 3, 4}));
    EXPECT_EQ(cells(*t, "/", "expr"), (std::vector<uint32_t>{3, 0x100, 4}));
}

TEST_F(DTSParserTest, LabelsReferencesAndOverrides) {
    auto t = parseText(R"(/dts-v1/;
/ {
	aliases { serial0 = &uart; };
	intc: interrupt-controller { phandle-target; };
	unused: scratch { };
	uart: serial@1000 {
		interrupt-parent = <&intc>;
		status = "disabled";
	};
};

&uart { status = "okay"; };
/delete-node/ &unused;
)");
    ASSERT_TRUE(t);
    EXPECT_EQ(rawString(*t, "/serial@1000", "status"), "okay");
    EXPECT_EQ(rawString(*t, "/aliases", "serial0"), "/serial@1000");
    EXPECT_FALSE(t->findNodeByPath("/scratch"));
    auto* intcPhandle = findProp(*t, "/interrupt-controller", "phandle");
    ASSERT_TRUE(intcPhandle);
    EXPECT_EQ(cells(*t, "/serial@1000", "interrupt-parent"), intcPhandle->getValueAsCells());
}

TEST_F(DTSParserTest, StringListsAndBytes) {
    auto t = parseText(R"(/dts-v1/;
/ {
	compatible = "vendor,a", "vendor,b";
	mac = [00 11 22 33 44 55];
	flag;
};
)");
    ASSERT_TRUE(t);
    EXPECT_EQ(findProp(*t, "/", "compatible")->getValueAsString(), "\"vendor,a\", \"vendor,b\"");
    EXPECT_EQ(findProp(*t, "/", "mac")->getValueAsBinary(),
              (std::vector<uint8_t>{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}));
    EXPECT_EQ(rawString(*t, "/", "flag"), "");
}

TEST_F(DTSParserTest, DtcIncludeDirective) {
    tmp_.write("common.dtsi", "/ { from-include = <42>; };\n");
    auto t = parseText("/dts-v1/;\n/include/ \"common.dtsi\"\n/ { own = <1>; };\n");
    ASSERT_TRUE(t);
    EXPECT_EQ(cells(*t, "/", "from-include"), (std::vector<uint32_t>{42}));
    EXPECT_EQ(cells(*t, "/", "own"), (std::vector<uint32_t>{1}));
}

// ---------------------------------------------------------------------------
// C preprocessor
// ---------------------------------------------------------------------------

TEST_F(DTSParserTest, DetectsPreprocessorDirectives) {
    EXPECT_TRUE(DTSParser::needsPreprocessing(kData + "/preprocessed/board.dts"));
    EXPECT_FALSE(DTSParser::needsPreprocessing(kData + "/board.dts"));
    // #address-cells and friends are properties, not directives
    EXPECT_FALSE(DTSParser::needsPreprocessing(
        tmp_.write("p.dts", "/dts-v1/;\n/ {\n\t#address-cells = <1>;\n\t#interrupt-cells = <3>;\n};\n")));
    EXPECT_TRUE(DTSParser::needsPreprocessing(tmp_.write("i.dts", "  #  include \"x.h\"\n")));
}

TEST_F(DTSParserTest, PreprocessesKernelStyleSource) {
    DTSParser parser;
    auto t = parser.parse(kData + "/preprocessed/board.dts");
    ASSERT_TRUE(t) << parser.lastError();

    EXPECT_EQ(rawString(*t, "/", "model"), "Preprocessed Board");
    // #include <dt-bindings/...> resolved via the source dir's include/
    EXPECT_EQ(cells(*t, "/soc/serial@10000000", "interrupts"), (std::vector<uint32_t>{0, 33, 4}));
    // #define macro
    EXPECT_EQ(cells(*t, "/soc/serial@10000000", "clock-frequency"), (std::vector<uint32_t>{1843200}));
    // #include "soc.dtsi" + &label override
    EXPECT_EQ(rawString(*t, "/soc/serial@10000000", "status"), "okay");
    EXPECT_EQ(rawString(*t, "/chosen", "stdout-path"), "/soc/serial@10000000");
}

TEST_F(DTSParserTest, ExtraIncludePaths) {
    const std::string file = kData + "/needs-extra-include.dts";
    {
        DTSParser parser;
        EXPECT_FALSE(parser.parse(file));
        EXPECT_NE(parser.lastError().find("board-config.h"), std::string::npos) << parser.lastError();
    }
    {
        DTSParser parser;
        parser.setIncludePaths({kData + "/extra-include"});
        auto t = parser.parse(file);
        ASSERT_TRUE(t) << parser.lastError();
        EXPECT_EQ(rawString(*t, "/", "model"), "From Extra Include Path");
    }
}

TEST_F(DTSParserTest, PlainFilesDoNotNeedCpp) {
    ::setenv("DTE_CPP", "/nonexistent/cpp", 1);
    DTSParser parser;
    EXPECT_TRUE(parser.parse(kData + "/board.dts")) << parser.lastError();
}

TEST_F(DTSParserTest, MissingCppIsReported) {
    ::setenv("DTE_CPP", "/nonexistent/cpp", 1);
    DTSParser parser;
    EXPECT_FALSE(parser.parse(kData + "/preprocessed/board.dts"));
    EXPECT_NE(parser.lastError().find("C preprocessor"), std::string::npos) << parser.lastError();
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

TEST_F(DTSParserTest, SyntaxErrorReportsFileAndLine) {
    auto file = tmp_.write("bad.dts", "/dts-v1/;\n/ {\n\ta = <1 2;\n};\n");
    DTSParser parser;
    EXPECT_FALSE(parser.parse(file));
    EXPECT_NE(parser.lastError().find("bad.dts:3."), std::string::npos) << parser.lastError();
}

TEST_F(DTSParserTest, SyntaxErrorAfterPreprocessingReportsOriginalLine) {
    auto file = tmp_.write("bad-cpp.dts", "/dts-v1/;\n#define X 1\n/ {\n\ta = <X;\n};\n");
    DTSParser parser;
    EXPECT_FALSE(parser.parse(file));
    EXPECT_NE(parser.lastError().find("bad-cpp.dts:4."), std::string::npos) << parser.lastError();
}

TEST_F(DTSParserTest, MissingIncludeIsReported) {
    auto file = tmp_.write("inc.dts", "#include \"nope.h\"\n/dts-v1/;\n/ {};\n");
    DTSParser parser;
    EXPECT_FALSE(parser.parse(file));
    EXPECT_NE(parser.lastError().find("nope.h"), std::string::npos) << parser.lastError();
}

TEST_F(DTSParserTest, MissingFileIsReported) {
    DTSParser parser;
    EXPECT_FALSE(parser.parse("/nonexistent/x.dts"));
    EXPECT_NE(parser.lastError().find("Cannot open file"), std::string::npos);
}

TEST_F(DTSParserTest, MissingDtcIsReported) {
    ::setenv("DTE_DTC", "/nonexistent/dtc", 1);
    DTSParser parser;
    EXPECT_FALSE(parser.parse(kData + "/board.dts"));
    EXPECT_NE(parser.lastError().find("dtc"), std::string::npos) << parser.lastError();
}
