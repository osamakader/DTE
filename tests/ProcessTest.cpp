#include "Process.h"

#include <gtest/gtest.h>

#include <cstdlib>

using DTE::runProcess;

TEST(Process, CapturesStdoutAndExitCode) {
    auto r = runProcess({"printf", "hello"});
    ASSERT_TRUE(r.launched) << r.error;
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.stdoutData, "hello");
    EXPECT_EQ(r.stderrData, "");
}

TEST(Process, CapturesStderrAndNonZeroExit) {
    auto r = runProcess({"sh", "-c", "echo oops >&2; exit 3"});
    ASSERT_TRUE(r.launched);
    EXPECT_EQ(r.exitCode, 3);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.stdoutData, "");
    EXPECT_EQ(r.stderrData, "oops\n");
}

TEST(Process, PassesArgumentsLiterallyWithoutAShell) {
    const std::string tricky = "a b;echo injected $(id) `x` 'q' \"d\" *";
    auto r = runProcess({"printf", "%s", tricky});
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.stdoutData, tricky);
}

TEST(Process, ReportsMissingProgram) {
    auto r = runProcess({"/nonexistent/definitely-not-a-program"});
    EXPECT_FALSE(r.launched);
    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.error.find("cannot execute"), std::string::npos) << r.error;
}

TEST(Process, RejectsEmptyCommand) {
    auto r = runProcess({});
    EXPECT_FALSE(r.launched);
    EXPECT_FALSE(r.error.empty());
}

TEST(Process, StdinIsEmpty) {
    auto r = runProcess({"cat"});
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.stdoutData, "");
}

TEST(Process, DrainsLargeOutputOnBothStreamsWithoutDeadlock) {
    // 1 MiB on each stream, far beyond the pipe buffer size.
    auto r = runProcess({"sh", "-c",
                         "head -c 1048576 /dev/zero; head -c 1048576 /dev/zero >&2"});
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.stdoutData.size(), 1048576u);
    EXPECT_EQ(r.stderrData.size(), 1048576u);
}

TEST(Process, IsBinarySafe) {
    auto r = runProcess({"printf", "a\\000b\\377"});
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.stdoutData, std::string("a\0b\xff", 4));
}

TEST(Process, ExecutableOverridesComeFromEnvironment) {
    ::setenv("DTE_DTC", "/opt/custom/dtc", 1);
    ::setenv("DTE_CPP", "/opt/custom/cpp", 1);
    EXPECT_EQ(DTE::dtcExecutable(), "/opt/custom/dtc");
    EXPECT_EQ(DTE::cppExecutable(), "/opt/custom/cpp");
    ::unsetenv("DTE_DTC");
    ::unsetenv("DTE_CPP");
    EXPECT_EQ(DTE::dtcExecutable(), "dtc");
    EXPECT_EQ(DTE::cppExecutable(), "cpp");
}
