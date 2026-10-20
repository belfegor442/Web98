#pragma once
// DPAPI credential encryption + URL -> origin helper for the password
// manager. Pure Windows logic, unit-testable, no CEF.
#include <cstdint>
#include <string>
#include <vector>

namespace web98 {

std::vector<uint8_t> dpapiProtect(const std::vector<uint8_t>& plain);
std::vector<uint8_t> dpapiUnprotect(const std::vector<uint8_t>& blob);

// "https://Sub.Example.com:8443/x" -> "https://sub.example.com:8443".
// Default ports are stripped, userinfo is dropped. Non-http(s) or
// unparsable input -> "".
std::string originFromUrl(const std::string& url);

}  // namespace web98
