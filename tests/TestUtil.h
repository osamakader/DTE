#ifndef DTE_TESTS_TESTUTIL_H
#define DTE_TESTS_TESTUTIL_H

#include "DeviceTree.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace DTE::test {

// Directory created under the system temp dir and removed on destruction.
class TempDir {
public:
    TempDir() {
        std::string tmpl = (std::filesystem::temp_directory_path() / "dte-test-XXXXXX").string();
        if (!::mkdtemp(tmpl.data())) throw std::runtime_error("mkdtemp failed");
        path_ = tmpl;
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

    // Writes `content` to a file inside the directory and returns its path.
    std::string write(const std::string& name, const std::string& content) const {
        auto p = path_ / name;
        std::ofstream(p, std::ios::binary) << content;
        return p.string();
    }

private:
    std::filesystem::path path_;
};

inline size_t countNodes(const std::shared_ptr<DeviceTreeNode>& n) {
    size_t count = 1;
    for (const auto& c : n->getChildren()) count += countNodes(c);
    return count;
}

// Looks up a property; returns nullptr if the node or property is missing.
inline const DeviceTreeProperty* findProp(const DeviceTree& tree, const std::string& path,
                                          const std::string& name) {
    auto node = tree.findNodeByPath(path);
    if (!node) return nullptr;
    return static_cast<const DeviceTreeNode&>(*node).findProperty(name);
}

} // namespace DTE::test

#endif // DTE_TESTS_TESTUTIL_H
