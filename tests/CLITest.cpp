// End-to-end tests that run the dte-cli binary.

#include "Process.h"
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <cstdlib>

using DTE::runProcess;
using DTE::test::TempDir;

namespace {

const std::string kData = TEST_DATA_DIR;

class CLITest : public ::testing::Test {
protected:
    void SetUp() override {
#ifndef DTC_PATH
        GTEST_SKIP() << "dtc not available";
#else
        ::setenv("DTE_DTC", DTC_PATH, 1);
#endif
    }
    void TearDown() override { ::unsetenv("DTE_DTC"); }

    DTE::ProcessResult cli(std::vector<std::string> args) {
        args.insert(args.begin(), DTE_CLI_PATH);
        return runProcess(args);
    }

    TempDir tmp_;
};

} // namespace

TEST_F(CLITest, PipedOutputHasNoColorsAndStatusGoesToStderr) {
    auto r = cli({"list", kData + "/board.dts"});
    ASSERT_EQ(r.exitCode, 0) << r.stderrData;
    EXPECT_EQ(r.stdoutData.find('\033'), std::string::npos);
    EXPECT_EQ(r.stderrData.find('\033'), std::string::npos);
    EXPECT_EQ(r.stdoutData.find("INFO:"), std::string::npos);
    EXPECT_NE(r.stderrData.find("INFO:"), std::string::npos);
    EXPECT_NE(r.stdoutData.find("clock-frequency = <0x47868c00>"), std::string::npos);
}

TEST_F(CLITest, DiffExitStatusFollowsDiffConvention) {
    const std::string dts = kData + "/board.dts";
    const std::string dtb = (tmp_.path() / "board.dtb").string();
    ASSERT_EQ(cli({"convert", dts, dtb}).exitCode, 0);

    EXPECT_EQ(cli({"diff", dts, dtb}).exitCode, 0);

    auto changed = tmp_.write("changed.dts", "/dts-v1/;\n/ { model = \"other\"; };\n");
    auto r = cli({"diff", dts, changed});
    EXPECT_EQ(r.exitCode, 1);
    EXPECT_NE(r.stdoutData.find("[MOD] /:model"), std::string::npos) << r.stdoutData;
    EXPECT_NE(r.stdoutData.find("+ \"other\""), std::string::npos) << r.stdoutData;

    EXPECT_EQ(cli({"diff", dts, "/nonexistent.dts"}).exitCode, 2);
}

TEST_F(CLITest, IncludeOptionInAnyPosition) {
    const std::string file = kData + "/needs-extra-include.dts";
    const std::string inc = kData + "/extra-include";

    auto without = cli({"list", file});
    EXPECT_NE(without.exitCode, 0);
    EXPECT_NE(without.stderrData.find("board-config.h"), std::string::npos) << without.stderrData;

    for (const auto& args : std::vector<std::vector<std::string>>{
             {"-I", inc, "list", file},
             {"list", file, "--include", inc},
             {"-I" + inc, "list", file},
             {"list", "--include=" + inc, file}}) {
        auto r = cli(args);
        EXPECT_EQ(r.exitCode, 0) << r.stderrData;
        EXPECT_NE(r.stdoutData.find("From Extra Include Path"), std::string::npos);
    }

    EXPECT_NE(cli({"list", file, "-I"}).exitCode, 0);
}

TEST_F(CLITest, ConvertDetectsInputByContent) {
    const std::string blob = (tmp_.path() / "firmware.bin").string();
    ASSERT_EQ(cli({"convert", kData + "/board.dts", (tmp_.path() / "b.dtb").string()}).exitCode, 0);
    std::filesystem::copy_file(tmp_.path() / "b.dtb", blob);

    const std::string out = (tmp_.path() / "back.dts").string();
    auto r = cli({"convert", blob, out});
    ASSERT_EQ(r.exitCode, 0) << r.stderrData;
    EXPECT_EQ(cli({"diff", kData + "/board.dts", out}).exitCode, 0);
}

TEST_F(CLITest, ConvertReportsDtcErrors) {
    auto bad = tmp_.write("bad.dts", "/dts-v1/;\n/ { a = <1; };\n");
    auto r = cli({"convert", bad, (tmp_.path() / "bad.dtb").string()});
    EXPECT_NE(r.exitCode, 0);
    EXPECT_NE(r.stderrData.find("bad.dts:2."), std::string::npos) << r.stderrData;
}

TEST_F(CLITest, ExportWritesParseableJson) {
    const std::string out = (tmp_.path() / "o.json").string();
    auto r = cli({"export", kData + "/board.dts", "json", out});
    ASSERT_EQ(r.exitCode, 0) << r.stderrData;
    std::ifstream in(out);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("\"model\": \"ACME Widget Board\""), std::string::npos);
}

TEST_F(CLITest, LoadErrorsIncludeReason) {
    auto r = cli({"info", tmp_.write("x.txt", "nope")});
    EXPECT_NE(r.exitCode, 0);
    EXPECT_NE(r.stderrData.find("Unrecognized file format"), std::string::npos) << r.stderrData;
}

TEST_F(CLITest, ExportWritesDtsAndDtb) {
    const std::string dts = (tmp_.path() / "o.dts").string();
    const std::string dtb = (tmp_.path() / "o.dtb").string();
    ASSERT_EQ(cli({"export", kData + "/board.dts", "dts", dts}).exitCode, 0);
    ASSERT_EQ(cli({"export", kData + "/board.dts", "dtb", dtb}).exitCode, 0);
    EXPECT_EQ(cli({"diff", kData + "/board.dts", dts}).exitCode, 0);
    EXPECT_EQ(cli({"diff", kData + "/board.dts", dtb}).exitCode, 0);

    auto r = cli({"export", kData + "/board.dts", "xml", dts});
    EXPECT_NE(r.exitCode, 0);
    EXPECT_NE(r.stderrData.find("Unsupported format"), std::string::npos);
}

TEST_F(CLITest, ValidateReportsIssuesAndExitStatus) {
    auto clean = cli({"validate", kData + "/board.dts"});
    EXPECT_EQ(clean.exitCode, 0) << clean.stdoutData << clean.stderrData;
    EXPECT_NE(clean.stderrData.find("No problems found"), std::string::npos);

    auto warn = cli({"validate", tmp_.write("w.dts", "/dts-v1/;\n/ { compatible = \"a\"; model = \"m\"; "
                                                     "#address-cells = <1>; #size-cells = <1>; "
                                                     "dev@100 { reg = <0x200 0x10>; }; };\n")});
    EXPECT_EQ(warn.exitCode, 0); // warnings only
    EXPECT_NE(warn.stdoutData.find("warning: /dev@100: unit address '100' does not match the first reg address (0x200)"),
              std::string::npos) << warn.stdoutData;

    auto bad = cli({"validate", tmp_.write("e.dts", "/dts-v1/;\n/ { compatible = \"a\"; model = \"m\"; "
                                                    "#address-cells = <1>; #size-cells = <1>; "
                                                    "dev@100 { reg = <0x100>; status = \"on\"; }; };\n")});
    EXPECT_EQ(bad.exitCode, 1);
    EXPECT_NE(bad.stdoutData.find("error: /dev@100:reg: has 1 cells"), std::string::npos) << bad.stdoutData;
    EXPECT_NE(bad.stderrData.find("1 error(s), 1 warning(s)"), std::string::npos) << bad.stderrData;
}

// The GUI binary's headless modes must work without any display.
class GuiBinaryHeadlessTest : public CLITest {
protected:
    DTE::ProcessResult gui(std::vector<std::string> args) {
        std::vector<std::string> argv{"env", "-u", "DISPLAY", "-u", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM=",
                                      DTE_GUI_PATH};
        argv.insert(argv.end(), args.begin(), args.end());
        return runProcess(argv);
    }
};

TEST_F(GuiBinaryHeadlessTest, CliModeForwardsToDteCli) {
    auto r = gui({"--cli", "list", kData + "/board.dts"});
    EXPECT_EQ(r.exitCode, 0) << r.stderrData;
    EXPECT_NE(r.stdoutData.find("compatible = \"acme,widget-board\", \"acme,widget\""), std::string::npos);

    EXPECT_EQ(gui({"--cli", "diff", kData + "/board.dts", "/nonexistent.dts"}).exitCode, 2);
}

TEST_F(GuiBinaryHeadlessTest, ExportAndValidate) {
    const std::string out = (tmp_.path() / "o.json").string();
    auto r = gui({"-f", kData + "/board.dts", "-e", "json", "-o", out});
    ASSERT_EQ(r.exitCode, 0) << r.stderrData;
    std::ifstream in(out);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("\"model\": \"ACME Widget Board\""), std::string::npos);

    // Positional file and include paths
    const std::string dts = (tmp_.path() / "flat.dts").string();
    r = gui({"-I", kData + "/extra-include", kData + "/needs-extra-include.dts", "--export", "dts", "-o", dts});
    ASSERT_EQ(r.exitCode, 0) << r.stderrData;

    r = gui({kData + "/board.dts", "--validate"});
    EXPECT_EQ(r.exitCode, 0) << r.stderrData;
    EXPECT_NE(r.stderrData.find("No problems found"), std::string::npos);

    auto bad = tmp_.write("e.dts", "/dts-v1/;\n/ { dev@1 { reg = <1>; }; };\n");
    EXPECT_EQ(gui({"-f", bad, "--validate"}).exitCode, 1);
}

TEST_F(GuiBinaryHeadlessTest, ArgumentErrors) {
    auto r = gui({"-e", "json"});
    EXPECT_EQ(r.exitCode, 1);
    EXPECT_NE(r.stderrData.find("need a file"), std::string::npos) << r.stderrData;

    r = gui({"-f", kData + "/board.dts", "-e", "json"});
    EXPECT_EQ(r.exitCode, 1);
    EXPECT_NE(r.stderrData.find("needs an output file"), std::string::npos) << r.stderrData;

    r = gui({"--version"});
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_NE(r.stdoutData.find("Device Tree Explorer 1.0.0"), std::string::npos);
}
