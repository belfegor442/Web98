#pragma once
// Native port of src/main/menus.js: context-menu items, the command
// executor and the global keyboard accelerators.
#include <string>

#include "json.h"

namespace web98 {

class MainWindow;
class TabManager;

// Everything a command needs to act (Electron's runCommand ctx).
struct CommandCtx {
  MainWindow* win = nullptr;
  TabManager* tabs = nullptr;
};

// menus.js contextTemplate(params, tab): array of {id,label,enabled} / {sep}.
Json contextTemplate(const Json& params, bool canBack, bool canForward,
                     bool loading);

// menus.js runCommand(name, arg, ctx). Returns true when recognized.
bool runCommand(const std::string& name, const Json& arg,
                const CommandCtx& ctx);

// menus.js handleAccelerator(input, ctx). `code` is the Windows virtual key,
// modifiers mirror cef_event_flags_t. Returns true when the key was consumed.
bool handleAccelerator(int code, bool ctrl, bool alt, bool shift,
                       const CommandCtx& ctx);

// ipc.js runContextCommand(id, params, ...).
void runContextCommand(const std::string& id, const Json& params,
                       const CommandCtx& ctx);

// menus.js viewSource(tab, ctx) / savePage(tab): async, reply via callback.
void viewSourceAsync(const CommandCtx& ctx);
void savePageAsync(const CommandCtx& ctx);

}  // namespace web98
