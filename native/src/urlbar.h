#pragma once
// Port of src/shared/urlbar.js — typed address resolution (URL vs search).
#include <string>

namespace web98 {

enum class AddressType { Empty, Url, Search, Javascript };

struct AddressResult {
  AddressType type = AddressType::Empty;
  std::string url;   // for Url / Search
  std::string term;  // for Search / Javascript
};

// `searchTemplate` must already be resolved (settings.searchTemplate()).
// `defaultScheme` is always "http" in the app.
AddressResult parseAddress(const std::string& raw, const std::string& searchTemplate,
                           const std::string& defaultScheme = "http");

// encodeURIComponent (JS semantics).
std::string encodeURIComponent(const std::string& s);

// template.split('%s')[0].replace(/[?&=]+$/,'') — base URL for the search button.
std::string searchTemplateBase(const std::string& tmpl);

// Apply the search template to a term (first %s replaced, else appended).
std::string buildSearchUrl(const std::string& tmpl, const std::string& term);

}  // namespace web98
