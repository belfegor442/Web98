#pragma once
// Download destination safety: filename sanitization, unique names and
// directory containment. Ports validate.sanitizeFilename + downloads.js
// uniquePath/containment logic (unit-tested).
#include <string>

namespace web98 {

// Filesystem-safe download filename (no dirs, no reserved chars, no
// reserved device names, no trailing dots/spaces, <=120 chars).
std::string sanitizeFilename(const std::string& name);

// dir/name if free, else "stem (2).ext", "stem (3).ext", ...
std::string uniquePath(const std::string& dir, const std::string& name);

// Ensures `candidate` resolves inside `dir`; falls back to dir/basename.
std::string containPath(const std::string& dir, const std::string& candidate);

// Full pipeline used when CEF suggests a filename.
std::string resolveDownloadPath(const std::string& dir, const std::string& suggestedName);

}  // namespace web98
