#pragma once
// Native browser-process dialogs + shell helpers (Electron dialog/shell
// parity). Everything runs on the browser UI thread.
#include <string>
#include <vector>

#include <windows.h>

namespace web98 {
namespace dialogs {

// MessageBox with Web98's wording; type is MB_OK/MB_YESNO/MB_ICONWARNING...
// Returns the ID* constant of the chosen button.
int messageBox(HWND owner, const std::string& text, const std::string& caption,
               UINT type);

// "Save As" single file. exts = {"html","htm"} style (empty = all files).
// Returns "" when cancelled.
std::string saveFileDialog(HWND owner, const std::string& title,
                           const std::string& defaultName,
                           const std::vector<std::string>& exts);

// "Open" single file. Returns "" when cancelled.
std::string openFileDialog(HWND owner, const std::string& title,
                           const std::vector<std::string>& exts);

// "Open" multi-select; empty vector when cancelled.
std::vector<std::string> openFilesDialog(HWND owner, const std::string& title,
                                         const std::vector<std::string>& exts);

// Folder picker (Electron: openDirectory + createDirectory). "" = cancelled.
std::string folderDialog(HWND owner, const std::string& title);

// ShellExecuteW("open") for a local path; false when it could not be opened.
bool shellOpenPath(const std::string& path);
// Reveal the file in Explorer with it selected.
bool showInFolder(const std::string& path);
// Hand an external scheme (mailto:, tel:) to the OS handler.
bool openExternalUrl(const std::string& url);

// Modal prompt used by window.prompt() (returns true on OK, text in *out).
bool promptDialog(HWND owner, const std::string& title,
                  const std::string& message, const std::string& defaultValue,
                  std::string* out);

// Electron clipboard parity (CF_UNICODETEXT).
void setClipboardText(const std::string& text);
std::string getClipboardText();

}  // namespace dialogs
}  // namespace web98
