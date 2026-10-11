#pragma once
// Small platform/util layer shared by the whole native app.
#include <cstdint>
#include <string>
#include <vector>

namespace web98 {

// --- strings ---------------------------------------------------------------
std::string toUtf8(const std::wstring& w);
std::wstring toWide(const std::string& u);
std::string trim(const std::string& s);
std::string asciiLower(const std::string& s);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);

// --- paths / files ---------------------------------------------------------
bool fileExists(const std::string& path);
bool dirExists(const std::string& path);
bool ensureDirTree(const std::string& path);
bool readFile(const std::string& path, std::string& out);
bool writeFileAtomic(const std::string& path, const std::string& data);
std::string pathJoin(const std::string& a, const std::string& b);
std::string pathDirName(const std::string& p);
std::string pathBaseName(const std::string& p);
bool isAbsolutePathNative(const std::string& p);  // "C:\", "C:/", "//srv", "/x"
std::string exeDir();
std::string envOrEmpty(const char* name);

// Native path -> file:// URL (percent-encodes % # ? space etc., UTF-8).
std::string pathToFileUrl(const std::string& path);
// file:// URL -> native path ("" when not a file URL).
std::string fileUrlToPath(const std::string& url);

// Profile directory: $WEB98_USER_DATA, else %APPDATA%\Web98.
std::string userDataDir();
// OS Downloads folder (%USERPROFILE%\Downloads).
std::string defaultDownloadsDir();
// Locate bundled res/ dir (startpage.html, error.html): next to the exe,
// then the repo checkout when running from native/build/<config>.
std::string resourceDir();
std::string homepageUrl();   // file URL of res/startpage.html
std::string errorPageUrl();  // file URL of res/error.html

// --- misc ------------------------------------------------------------------
int64_t nowMs();
std::string urlEncodeQuery(const std::string& s);  // encodeURIComponent clone

}  // namespace web98
