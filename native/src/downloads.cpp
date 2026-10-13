#include "downloads.h"

#include <algorithm>

#include "base.h"
#include "dlpath.h"

namespace web98 {
namespace {
constexpr size_t kMaxFinished = 200;
}  // namespace

Downloads::Downloads(std::string downloadsDirSetting)
    : downloadsDir_(std::move(downloadsDirSetting)) {}

Downloads::~Downloads() = default;

void Downloads::setDownloadsDir(const std::string& dir) { downloadsDir_ = dir; }

int Downloads::RegisterNew(const std::string& url, const std::string& suggestedName,
                           std::string* savePath) {
  std::string dir = downloadsDir_.empty() ? defaultDownloadsDir() : downloadsDir_;
  std::string full = resolveDownloadPath(dir, suggestedName);
  DownloadRec rec;
  rec.id = nextId_++;
  rec.url = url;
  rec.savePath = full;
  rec.filename = pathBaseName(full);
  rec.state = "progressing";
  rec.startTime = nowMs();
  items_.push_back(rec);
  if (savePath) *savePath = full;
  notify();
  return rec.id;
}

DownloadRec* Downloads::find(int id) {
  for (auto& r : items_)
    if (r.id == id) return &r;
  return nullptr;
}

void Downloads::Progress(int id, int64_t received, int64_t total, bool paused) {
  DownloadRec* r = find(id);
  if (!r) return;
  r->received = received;
  if (total > 0) r->total = total;
  if (paused && r->state == "progressing") r->state = "paused";
  if (!paused && r->state == "paused") r->state = "progressing";
  notify();
}

void Downloads::Done(int id, const std::string& state) {
  DownloadRec* r = find(id);
  if (!r) return;
  r->state = state;
  r->endTime = nowMs();
  cancelers_.erase(id);
  pausers_.erase(id);
  resumers_.erase(id);
  trimFinished();
  notify();
}

void Downloads::SetCancelHook(int id, std::function<void()> hook) {
  if (hook)
    cancelers_[id] = std::move(hook);
  else
    cancelers_.erase(id);
}
void Downloads::SetPauseHook(int id, std::function<void()> hook) {
  if (hook)
    pausers_[id] = std::move(hook);
  else
    pausers_.erase(id);
}
void Downloads::SetResumeHook(int id, std::function<void()> hook) {
  if (hook)
    resumers_[id] = std::move(hook);
  else
    resumers_.erase(id);
}

bool Downloads::Cancel(int id) {
  DownloadRec* r = find(id);
  if (!r) return false;
  if (r->state != "progressing" && r->state != "paused") return false;
  auto it = cancelers_.find(id);
  if (it != cancelers_.end() && it->second) it->second();
  return true;
}

bool Downloads::Pause(int id) {
  DownloadRec* r = find(id);
  if (!r || r->state != "progressing") return false;
  auto it = pausers_.find(id);
  if (it != pausers_.end() && it->second) {
    it->second();
    return true;
  }
  return false;
}

bool Downloads::Resume(int id) {
  DownloadRec* r = find(id);
  if (!r || r->state != "paused") return false;
  auto it = resumers_.find(id);
  if (it != resumers_.end() && it->second) {
    it->second();
    return true;
  }
  return false;
}

void Downloads::Remove(int id) {
  for (auto it = items_.begin(); it != items_.end(); ++it) {
    if (it->id == id) {
      if (it->state == "progressing" || it->state == "paused") {
        auto c = cancelers_.find(id);
        if (c != cancelers_.end() && c->second) c->second();
      }
      items_.erase(it);
      break;
    }
  }
  cancelers_.erase(id);
  pausers_.erase(id);
  resumers_.erase(id);
  notify();
}

void Downloads::ClearFinished() {
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [](const DownloadRec& r) {
                                return r.state != "progressing" && r.state != "paused";
                              }),
               items_.end());
  notify();
}

std::string Downloads::PathFor(int id) const {
  for (const auto& r : items_)
    if (r.id == id) return r.savePath;
  return std::string();
}

std::string Downloads::StateFor(int id) const {
  for (const auto& r : items_)
    if (r.id == id) return r.state;
  return std::string();
}

int Downloads::ActiveCount() const {
  int n = 0;
  for (const auto& r : items_)
    if (r.state == "progressing" || r.state == "paused") ++n;
  return n;
}

void Downloads::trimFinished() {
  // Keep the newest kMaxFinished finished entries; active ones always survive.
  int finished = 0;
  for (const auto& r : items_)
    if (r.state != "progressing" && r.state != "paused") ++finished;
  if (finished <= static_cast<int>(kMaxFinished)) return;
  // items_ is oldest-first: drop finished entries from the front.
  size_t toDrop = static_cast<size_t>(finished - kMaxFinished);
  size_t w = 0;
  for (size_t i = 0; i < items_.size(); ++i) {
    bool finishedItem = items_[i].state != "progressing" && items_[i].state != "paused";
    if (finishedItem && toDrop > 0) {
      --toDrop;
      continue;
    }
    items_[w++] = items_[i];
  }
  items_.resize(w);
}

void Downloads::notify() {
  if (listener_) listener_->OnDownloadsChanged();
}

bool Downloads::IsDangerousExtension(const std::string& filename) {
  static const char* kDangerous[] = {
      ".exe", ".msi",  ".bat", ".cmd", ".com",  ".scr", ".pif", ".js",
      ".jse", ".vbs",  ".vbe", ".wsf", ".ps1",  ".lnk", ".reg", ".hta",
  };
  std::string ext;
  size_t dot = filename.find_last_of('.');
  if (dot != std::string::npos) ext = asciiLower(filename.substr(dot));
  for (const char* d : kDangerous)
    if (ext == d) return true;
  return false;
}

}  // namespace web98
