#include "DtsValue.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <limits>

namespace DTE {

namespace {

enum class Kind { String, Cells32, Cells64, Bytes };

const char* kindName(Kind k) {
    switch (k) {
    case Kind::String: return "strings";
    case Kind::Cells32: return "32-bit cells";
    case Kind::Cells64: return "64-bit cells";
    case Kind::Bytes: return "bytes";
    }
    return "values";
}

class ValueParser {
public:
    explicit ValueParser(const std::string& text) : s_(text) {}

    bool parse(PropertyValue& out, std::string& error) {
        // Ignore surrounding whitespace and one trailing ';'
        size_t end = s_.size();
        while (end > 0 && std::isspace(static_cast<unsigned char>(s_[end - 1]))) --end;
        if (end > 0 && s_[end - 1] == ';') --end;
        end_ = end;

        skipSpace();
        if (pos_ >= end_) {
            out = std::string(); // empty property
            return true;
        }

        for (;;) {
            skipSpace();
            if (!parseItem(error)) return false;
            skipSpace();
            if (pos_ >= end_) break;
            if (s_[pos_] != ',') {
                return fail(error, "Expected ',' between values");
            }
            ++pos_;
            skipSpace();
            if (pos_ >= end_) {
                return fail(error, "Expected a value after ','");
            }
        }
        return combine(out, error);
    }

private:
    const std::string& s_;
    size_t pos_ = 0;
    size_t end_ = 0;

    Kind kind_ = Kind::String;
    bool haveKind_ = false;
    std::vector<std::string> strings_;
    std::vector<uint32_t> cells32_;
    std::vector<uint64_t> cells64_;
    std::vector<uint8_t> bytes_;

    bool fail(std::string& error, const std::string& msg) const {
        error = msg + " (at column " + std::to_string(pos_ + 1) + ")";
        return false;
    }

    void skipSpace() {
        while (pos_ < end_ && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }

    bool startsWith(const char* lit) const {
        return s_.compare(pos_, std::char_traits<char>::length(lit), lit) == 0;
    }

    bool setKind(Kind k, std::string& error) {
        if (haveKind_ && kind_ != k) {
            return fail(error, std::string("Cannot mix ") + kindName(kind_) + " and " + kindName(k) +
                                   " in one property");
        }
        kind_ = k;
        haveKind_ = true;
        return true;
    }

    bool parseItem(std::string& error) {
        const char c = s_[pos_];
        if (c == '"') return setKind(Kind::String, error) && parseString(error);
        if (c == '<') return setKind(Kind::Cells32, error) && parseCells(32, error);
        if (c == '[') return setKind(Kind::Bytes, error) && parseBytes(error);
        if (startsWith("/bits/")) {
            pos_ += 6;
            skipSpace();
            uint64_t bits = 0;
            if (!parseNumber(bits, 64, error)) return false;
            if (bits != 32 && bits != 64) {
                return fail(error, "Only /bits/ 32 and /bits/ 64 are supported");
            }
            skipSpace();
            if (pos_ >= end_ || s_[pos_] != '<') return fail(error, "Expected '<' after /bits/");
            return setKind(bits == 64 ? Kind::Cells64 : Kind::Cells32, error) &&
                   parseCells(static_cast<int>(bits), error);
        }
        if (c == '&') {
            return fail(error, "Label references (&label) are not supported here; "
                               "use the target's phandle number, e.g. <0x1>");
        }
        return fail(error, "Expected a string (\"...\"), cells (<...>) or bytes ([...])");
    }

    bool parseString(std::string& error) {
        const size_t start = pos_;
        ++pos_; // opening quote
        std::string value;
        while (pos_ < end_ && s_[pos_] != '"') {
            char c = s_[pos_++];
            if (c != '\\') {
                value += c;
                continue;
            }
            if (pos_ >= end_) break;
            char e = s_[pos_++];
            switch (e) {
            case 'n': value += '\n'; break;
            case 't': value += '\t'; break;
            case 'r': value += '\r'; break;
            case 'a': value += '\a'; break;
            case 'b': value += '\b'; break;
            case 'f': value += '\f'; break;
            case 'v': value += '\v'; break;
            case '\\': value += '\\'; break;
            case '"': value += '"'; break;
            case '\'': value += '\''; break;
            case 'x': {
                int v = 0, n = 0;
                while (n < 2 && pos_ < end_ && std::isxdigit(static_cast<unsigned char>(s_[pos_]))) {
                    v = v * 16 + hexValue(s_[pos_++]);
                    ++n;
                }
                if (n == 0) return fail(error, "\\x must be followed by hex digits");
                value += static_cast<char>(v);
                break;
            }
            default:
                if (e >= '0' && e <= '7') {
                    int v = e - '0', n = 1;
                    while (n < 3 && pos_ < end_ && s_[pos_] >= '0' && s_[pos_] <= '7') {
                        v = v * 8 + (s_[pos_++] - '0');
                        ++n;
                    }
                    if (v > 0xff) return fail(error, "Octal escape out of range");
                    value += static_cast<char>(v);
                } else {
                    --pos_;
                    return fail(error, std::string("Unknown escape \\") + e);
                }
            }
        }
        if (pos_ >= end_) {
            pos_ = start;
            return fail(error, "Unterminated string");
        }
        ++pos_; // closing quote
        strings_.push_back(std::move(value));
        return true;
    }

    static int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        return std::tolower(static_cast<unsigned char>(c)) - 'a' + 10;
    }

    // Unsigned integer: decimal, 0x hex, 0-prefixed octal or 'c' character
    // literal; optional U/L suffixes as in C.
    bool parseNumber(uint64_t& out, int bits, std::string& error) {
        const size_t start = pos_;
        if (pos_ < end_ && s_[pos_] == '\'') {
            if (pos_ + 2 < end_ && s_[pos_ + 2] == '\'' && s_[pos_ + 1] != '\\') {
                out = static_cast<unsigned char>(s_[pos_ + 1]);
                pos_ += 3;
                return true;
            }
            return fail(error, "Invalid character literal");
        }
        while (pos_ < end_ && (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_')) ++pos_;
        std::string token = s_.substr(start, pos_ - start);
        if (token.empty()) {
            if (pos_ < end_ && s_[pos_] == '(') {
                return fail(error, "Expressions are not supported here; enter the computed value");
            }
            if (pos_ < end_ && s_[pos_] == '&') {
                return fail(error, "Label references (&label) are not supported here; "
                                   "use the target's phandle number, e.g. <0x1>");
            }
            if (pos_ < end_ && s_[pos_] == '-') {
                return fail(error, "Negative numbers are not supported; enter the unsigned value");
            }
            return fail(error, "Expected a number");
        }
        std::string digits = token;
        while (!digits.empty() && (digits.back() == 'u' || digits.back() == 'U' ||
                                   digits.back() == 'l' || digits.back() == 'L')) {
            digits.pop_back();
        }
        errno = 0;
        char* endp = nullptr;
        unsigned long long v = digits.empty() ? 0 : std::strtoull(digits.c_str(), &endp, 0);
        if (digits.empty() || *endp != '\0') {
            pos_ = start;
            return fail(error, "Invalid number '" + token + "'");
        }
        const uint64_t max = bits >= 64 ? std::numeric_limits<uint64_t>::max() : ((uint64_t(1) << bits) - 1);
        if (errno == ERANGE || v > max) {
            pos_ = start;
            return fail(error, "Number '" + token + "' does not fit in " + std::to_string(bits) + " bits");
        }
        out = v;
        return true;
    }

    bool parseCells(int bits, std::string& error) {
        ++pos_; // '<'
        for (;;) {
            skipSpace();
            if (pos_ >= end_) return fail(error, "Missing '>'");
            if (s_[pos_] == '>') {
                ++pos_;
                return true;
            }
            uint64_t v = 0;
            if (!parseNumber(v, bits, error)) return false;
            if (pos_ < end_ && !std::isspace(static_cast<unsigned char>(s_[pos_])) && s_[pos_] != '>') {
                return fail(error, "Expected a space or '>' after a number");
            }
            if (bits == 64) {
                cells64_.push_back(v);
            } else {
                cells32_.push_back(static_cast<uint32_t>(v));
            }
        }
    }

    bool parseBytes(std::string& error) {
        ++pos_; // '['
        // Like dtc, each whitespace-separated group must have an even number
        // of hex digits: [0011 22] is fine, [0 1] is an error.
        std::string hex;
        size_t groupLen = 0;
        auto endGroup = [&]() {
            const bool even = groupLen % 2 == 0;
            groupLen = 0;
            return even;
        };
        while (pos_ < end_ && s_[pos_] != ']') {
            char c = s_[pos_];
            if (std::isxdigit(static_cast<unsigned char>(c))) {
                hex += c;
                ++groupLen;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                if (!endGroup()) return fail(error, "Byte string needs an even number of hex digits per group");
            } else {
                return fail(error, std::string("Invalid character '") + c + "' in byte string");
            }
            ++pos_;
        }
        if (pos_ >= end_) return fail(error, "Missing ']'");
        if (!endGroup()) return fail(error, "Byte string needs an even number of hex digits per group");
        ++pos_; // ']'
        for (size_t i = 0; i < hex.size(); i += 2) {
            bytes_.push_back(static_cast<uint8_t>(hexValue(hex[i]) * 16 + hexValue(hex[i + 1])));
        }
        return true;
    }

    bool combine(PropertyValue& out, std::string& error) {
        (void)error;
        switch (kind_) {
        case Kind::String:
            if (strings_.size() == 1) {
                // A single empty string is one NUL byte; std::string("")
                // would mean an empty property instead.
                if (strings_.front().empty()) {
                    out = std::vector<uint8_t>{0};
                } else {
                    out = strings_.front();
                }
            } else {
                out = StringList(strings_);
            }
            return true;
        case Kind::Cells32: out = cells32_; return true;
        case Kind::Cells64: out = cells64_; return true;
        case Kind::Bytes: out = bytes_; return true;
        }
        return false;
    }
};

bool checkChars(const std::string& part, const std::string& extra, const std::string& what, std::string* error) {
    for (unsigned char c : part) {
        if (!std::isalnum(c) && extra.find(static_cast<char>(c)) == std::string::npos) {
            if (error) {
                *error = what + " contains invalid character '" + std::string(1, static_cast<char>(c)) +
                         "' (allowed: letters, digits and " + extra + ")";
            }
            return false;
        }
    }
    return true;
}

} // namespace

bool parseDtsValue(const std::string& text, PropertyValue& out, std::string* error) {
    std::string err;
    PropertyValue value;
    if (!ValueParser(text).parse(value, err)) {
        if (error) *error = err;
        return false;
    }
    out = std::move(value);
    return true;
}

bool isValidNodeName(const std::string& name, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (name.empty()) return fail("Node name is empty");
    const auto at = name.find('@');
    const std::string base = name.substr(0, at);
    if (base.empty()) return fail("Node name needs a name before '@'");
    if (!checkChars(base, ",._+-", "Node name", error)) return false;
    if (at != std::string::npos) {
        const std::string unit = name.substr(at + 1);
        if (unit.empty()) return fail("Unit address after '@' is empty");
        if (unit.find('@') != std::string::npos) return fail("Node name contains more than one '@'");
        if (!checkChars(unit, ",._+-", "Unit address", error)) return false;
    }
    return true;
}

bool isValidPropertyName(const std::string& name, std::string* error) {
    if (name.empty()) {
        if (error) *error = "Property name is empty";
        return false;
    }
    return checkChars(name, ",._+?#-", "Property name", error);
}

} // namespace DTE
