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
// and a DTS file loads exactly like its compiled DTB.
class DTSParser : public DeviceTreeParser {
public:
    std::shared_ptr<DeviceTree> parse(const std::string& filename) override;
    bool canParse(const std::string& filename) const override;

    // Warnings dtc printed during the last successful parse.
    const std::string& lastWarnings() const { return lastWarnings_; }

private:
    std::string lastWarnings_;
};

// Parser factory
class DeviceTreeParserFactory {
public:
    static std::unique_ptr<DeviceTreeParser> createParser(const std::string& filename);
    static std::vector<std::unique_ptr<DeviceTreeParser>> getAllParsers();
};

// Utility functions for device tree operations
namespace DeviceTreeUtils {
    bool dtbToDts(const std::string& dtbFile, const std::string& dtsFile);
    bool dtsToDtb(const std::string& dtsFile, const std::string& dtbFile);
    bool validateDeviceTree(const std::string& filename);
    std::string getDeviceTreeInfo(const std::string& filename);
    bool extractFromKernel(const std::string& kernelFile, const std::string& outputFile);
}

} // namespace DTE

#endif // DEVICETREEPARSER_H 