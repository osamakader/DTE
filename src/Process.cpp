#include "Process.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace DTE {

namespace {

void closeFd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

struct Pipe {
    int fds[2] = {-1, -1};
    bool open() { return ::pipe2(fds, O_CLOEXEC) == 0; }
    int& readEnd() { return fds[0]; }
    int& writeEnd() { return fds[1]; }
    ~Pipe() {
        closeFd(fds[0]);
        closeFd(fds[1]);
    }
};

} // namespace

ProcessResult runProcess(const std::vector<std::string>& argv) {
    ProcessResult result;
    if (argv.empty()) {
        result.error = "empty command";
        return result;
    }

    // execPipe reports exec() failure from the child: it is close-on-exec,
    // so a successful exec closes it and the parent reads EOF.
    Pipe outPipe, errPipe, execPipe;
    if (!outPipe.open() || !errPipe.open() || !execPipe.open()) {
        result.error = std::string("pipe failed: ") + std::strerror(errno);
        return result;
    }

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    pid_t pid = ::fork();
    if (pid < 0) {
        result.error = std::string("fork failed: ") + std::strerror(errno);
        return result;
    }

    if (pid == 0) {
        // Child: only async-signal-safe calls from here on.
        int devnull = ::open("/dev/null", O_RDONLY);
        if (devnull >= 0) ::dup2(devnull, STDIN_FILENO);
        ::dup2(outPipe.writeEnd(), STDOUT_FILENO);
        ::dup2(errPipe.writeEnd(), STDERR_FILENO);
        ::execvp(cargv[0], cargv.data());
        int err = errno;
        ssize_t n = ::write(execPipe.writeEnd(), &err, sizeof(err));
        (void)n;
        ::_exit(127);
    }

    closeFd(outPipe.writeEnd());
    closeFd(errPipe.writeEnd());
    closeFd(execPipe.writeEnd());

    int execErr = 0;
    ssize_t n;
    do {
        n = ::read(execPipe.readEnd(), &execErr, sizeof(execErr));
    } while (n < 0 && errno == EINTR);
    const bool execFailed = n == static_cast<ssize_t>(sizeof(execErr));

    // Drain stdout and stderr together so neither pipe can fill and block.
    struct pollfd pfds[2] = {{outPipe.readEnd(), POLLIN, 0}, {errPipe.readEnd(), POLLIN, 0}};
    std::string* sinks[2] = {&result.stdoutData, &result.stderrData};
    int open = 2;
    char buf[65536];
    while (open > 0) {
        if (::poll(pfds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < 2; ++i) {
            if (pfds[i].fd < 0 || !(pfds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            ssize_t r = ::read(pfds[i].fd, buf, sizeof(buf));
            if (r > 0) {
                sinks[i]->append(buf, static_cast<size_t>(r));
            } else if (r == 0 || errno != EINTR) {
                pfds[i].fd = -1;
                --open;
            }
        }
    }

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }

    if (execFailed) {
        result.error = "cannot execute '" + argv[0] + "': " + std::strerror(execErr);
        return result;
    }

    result.launched = true;
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

std::string dtcExecutable() {
    const char* env = std::getenv("DTE_DTC");
    return (env && *env) ? env : "dtc";
}

} // namespace DTE
