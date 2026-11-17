#ifndef DTE_DTSVALUE_H
#define DTE_DTSVALUE_H

// Parsing of property values and validation of names typed by the user,
// using DTS syntax. Used by the GUI editor.

#include "DeviceTree.h"

#include <string>

namespace DTE {

// Parses the value part of a DTS property assignment (the text after `=`),
// e.g. `"okay"`, `"a", "b"`, `<0x1000 0x100>`, `/bits/ 64 <1>`, `[00 11]`.
// Empty text means an empty property (`name;`). Several items of the same
// kind are concatenated, as dtc does (`<1>, <2>` == `<1 2>`). A trailing
// `;` is ignored.
//
// Not supported, with an explanatory error: label references (`&uart0`),
// arithmetic expressions, and mixing strings, cells and bytes in one value.
//
// Returns false and sets *error (if given) on failure.
bool parseDtsValue(const std::string& text, PropertyValue& out, std::string* error = nullptr);

// Node name per the Devicetree Specification: name[@unit-address], both
// parts non-empty and made of [A-Za-z0-9,._+-].
bool isValidNodeName(const std::string& name, std::string* error = nullptr);

// Property name: non-empty, made of [A-Za-z0-9,._+?#-].
bool isValidPropertyName(const std::string& name, std::string* error = nullptr);

} // namespace DTE

#endif // DTE_DTSVALUE_H
