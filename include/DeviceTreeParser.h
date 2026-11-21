#ifndef DEVICETREEPARSER_H
#define DEVICETREEPARSER_H

#include "DeviceTree.h"
#include <memory>
#include <string>

namespace DTE {

// Base parser interface
class DeviceTreeParser {
public:
    virtual ~DeviceTreeParser() = default;
    virtual std::shared_ptr<DeviceTree> parse(const std::string& filename) = 0;
    virtual bool canParse(const std::string& filename) const = 0;

    // Reason for the last parse failure (empty after a successful parse).
    const std::string& lastError() const { return lastError_; }

protected:
    std::string lastError_;
};

// DTB (Flattened Device Tree blob) parser.
//
// Implements the FDT format from the Devicetree Specification (versions
// 16 and 17). All reads are bounds-checked, so malformed or truncated blobs
// produce an error instead of undefined behaviour.
class DTBParser : public DeviceTreeParser {
public:
    std::shared_ptr<DeviceTree> parse(const std::string& filename) override;
    bool canParse(const std::string& filename) const override;

    // Parse a blob that is already in memory. Returns nullptr on failure;
    // the reason is available from lastError().
    std::shared_ptr<DeviceTree> parseBuffer(const uint8_t* data, size_t size);
    std::shared_ptr<DeviceTree> parseBuffer(const std::vector<uint8_t>& blob) {
        return parseBuffer(blob.data(), blob.size());
    }
};

// DTS (Device Tree Source) parser.
//
// Compiles the source with dtc and parses the resulting blob with
// DTBParser, so the full DTS language is supported (labels, references,
// /include/, /memreserve/, overrides, /delete-node/, expressions, ...)
// and a DTS file loads exactly like its compiled DTB. Files that use C
// preprocessor directives are run through cpp first, as the Linux kernel
// build does.
class DTSParser : public DeviceTreeParser {
public:
    std::shared_ptr<DeviceTree> parse(const std::string& filename) override;
    bool canParse(const std::string& filename) const override;

    // Compiles the source to a DTB blob without parsing it (used for
    // conversion). Returns false and sets lastError() on failure.
    bool compile(const std::string& filename, std::string& blob);

    // Warnings dtc printed during the last successful parse.
    const std::string& lastWarnings() const { return lastWarnings_; }

    // Extra directories searched by #include (C preprocessor) and
    // /include/ (dtc). The source file's directory and its include/
    // subdirectory are always searched first.
    void setIncludePaths(std::vector<std::string> paths) { includePaths_ = std::move(paths); }
    // Compile with `dtc -@` (emit __symbols__; see LoadOptions::symbols).
    void setSymbols(bool symbols) { symbols_ = symbols; }
    const std::vector<std::string>& includePaths() const { return includePaths_; }

    // True if the file uses C preprocessor directives (#include, #define,
    // #if...) and so must go through cpp before dtc.
    static bool needsPreprocessing(const std::string& filename);

private:
    std::string lastWarnings_;
    std::vector<std::string> includePaths_;
    bool symbols_ = false;
};

// Device tree exposed as a directory hierarchy, like the kernel's
// /proc/device-tree (= /sys/firmware/devicetree/base): one directory per
// node and one file per property holding its raw bytes. Values are typed
// like DTB values. As with `dtc -I fs`, `name` files (synthesized by the
// kernel for every node) are skipped; entries are sorted by name because
// directories do not keep the original order; symlinks are not followed.
class DirectoryParser : public DeviceTreeParser {
public:
    std::shared_ptr<DeviceTree> parse(const std::string& path) override;
    bool canParse(const std::string& path) const override;
};

// Where a running Linux system exposes its device tree, or empty if none
// of the usual locations exists (/proc/device-tree, then
// /sys/firmware/devicetree/base).
std::string systemDeviceTreePath();

// Parser factory
class DeviceTreeParserFactory {
public:
    static std::unique_ptr<DeviceTreeParser> createParser(const std::string& filename);
    static std::vector<std::unique_ptr<DeviceTreeParser>> getAllParsers();
};

// Utility functions for device tree operations. On failure they return
// false and, if `error` is given, store the reason there.
namespace DeviceTreeUtils {
    bool dtbToDts(const std::string& dtbFile, const std::string& dtsFile, std::string* error = nullptr);
    // Compiles like DTSParser (C preprocessing when needed, include paths).
    bool dtsToDtb(const std::string& dtsFile, const std::string& dtbFile,
                  const std::vector<std::string>& includePaths = {}, std::string* error = nullptr);
    bool validateDeviceTree(const std::string& filename, std::string* error = nullptr);
    std::string getDeviceTreeInfo(const std::string& filename);
    bool extractFromKernel(const std::string& kernelFile, const std::string& outputFile,
                           std::string* error = nullptr);
}

} // namespace DTE

#endif // DEVICETREEPARSER_H 