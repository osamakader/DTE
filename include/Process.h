#ifndef DTE_PROCESS_H
#define DTE_PROCESS_H

#include <string>
#include <vector>

namespace DTE {

// Result of running an external program.
struct ProcessResult {
    bool launched = false;   // false if the program could not be executed at all
    int exitCode = -1;       // exit status, or -1 if killed by a signal / not launched
    std::string stdoutData;  // captured standard output (binary-safe)
    std::string stderrData;  // captured standard error
    std::string error;       // reason when launched == false

    bool ok() const { return launched && exitCode == 0; }
};

// Runs argv[0] (looked up in PATH) with the given arguments, without a shell,
// so arguments are never subject to word splitting or injection. Standard
// input is /dev/null; stdout and stderr are captured.
ProcessResult runProcess(const std::vector<std::string>& argv);

// Path or name of the dtc executable: $DTE_DTC if set, otherwise "dtc".
std::string dtcExecutable();

// Path or name of the C preprocessor: $DTE_CPP if set, otherwise "cpp".
std::string cppExecutable();

} // namespace DTE

#endif // DTE_PROCESS_H
