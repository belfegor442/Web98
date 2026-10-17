#pragma once
// Download list + safe destination management (Electron downloads.js parity).
// CEF-agnostic: the CEF layer supplies save-path decisions and cancel hooks.
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace web98 {

struct DownloadRec {
  int id = 0;
  std::string filename;   // basename
  std::string savePath;   // absolute
  std::string url;
  std::string state;      // progressing|paused|completed|cancelled|interrupted
  int64_t received = 0;
  int64_t total = 0;
  int64_t startTime = 0;
  int64_t endTime = 0;
};

class DownloadListener {
 public:
  virtual ~DownloadListener() = default;
  virtual void OnDownloadsChanged() {}
};

class Downloads {
 public:
  explicit Downloads(std::string downloadsDirSetting);
  ~Downloads();

  void setListener(DownloadListener* l) { listener_ = l; }
  void setDownloadsDir(const std::string& dir);  // "" => OS default

  // Resolve + register a save path for a new download. Returns the assigned
  // record id (>0); `*savePath` receives the absolute destination.
  int RegisterNew(const std::string& url, const std::string& suggestedName,
                  std::string* savePath);

  // Progress/done updates (from the download handler).
  void Progress(int id, int64_t received, int64_t total, bool paused);
  void Done(int id, const std::string& state);  // completed|cancelled|interrupted

  // Hooks installed by the CEF layer for the active download.
  void SetCancelHook(int id, std::function<void()> hook);
  void SetPauseHook(int id, std::function<void()> hook);
  void SetResumeHook(int id, std::function<void()> hook);

  // UI operations.
  bool Cancel(int id);
  bool Pause(int id);
  bool Resume(int id);
  void Remove(int id);
  void ClearFinished();

  // Open/show: dangerous-extension warning is the UI's job; these only
  // return the path (empty when unavailable).
  std::string PathFor(int id) const;
  void SetPath(int id, const std::string& path);  // override the save path
  std::string StateFor(int id) const;  // "" when unknown
  const std::vector<DownloadRec>& list() const { return items_; }
  int ActiveCount() const;

  static bool IsDangerousExtension(const std::string& filename);

 private:
  DownloadRec* find(int id);
  void trimFinished();
  void notify();

  std::string downloadsDir_;
  std::vector<DownloadRec> items_;
  std::map<int, std::function<void()>> cancelers_;
  std::map<int, std::function<void()>> pausers_;
  std::map<int, std::function<void()>> resumers_;
  DownloadListener* listener_ = nullptr;
  int nextId_ = 1;
};

}  // namespace web98
