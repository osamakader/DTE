#ifndef DTE_ESCAPE_H
#define DTE_ESCAPE_H

// Internal helpers for writing JSON/YAML text without external libraries.

#include <cstdio>
#include <string>

namespace DTE::detail {

// Returns s as a double-quoted JSON string literal. Also valid as a YAML
// double-quoted scalar, since YAML's escapes are a superset of JSON's.
inline std::string jsonQuote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
    return out;
}

} // namespace DTE::detail

#endif // DTE_ESCAPE_H
