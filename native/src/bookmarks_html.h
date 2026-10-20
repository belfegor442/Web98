#pragma once
// Netscape Bookmark File Format (<!DOCTYPE NETSCAPE-ABookmark-file-1>)
// export/import. Pure logic, unit-tested; no CEF.
#include <cstdint>
#include <string>
#include <vector>

#include "storage.h"

namespace web98 {

struct BmNode {
  std::string name;
  std::string url;  // "" = folder
  std::vector<BmNode> children;
};

std::string exportNetscapeHtml(const std::vector<BookmarkRow>& rows);
bool parseNetscapeHtml(const std::string& html, std::vector<BmNode>& out);
std::string bmDecodeEntities(const std::string& s);

}  // namespace web98
