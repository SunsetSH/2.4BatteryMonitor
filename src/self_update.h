#pragma once
// Updates a portable copy in place (docs/adr/0008-portable-self-update.md). The caller verifies the
// signed executable first; this module only swaps files and restarts.
#include <windows.h>

#include <vector>

namespace self_update {

// Puts `image` in place of the running executable and starts it with --updated-from <this pid>; the
// caller then quits. False (folder not writable, file in use ...) leaves this copy exactly as it was.
bool ReplaceAndRestart(const std::vector<BYTE>& image, bool wlmouseOnly);

// Run by the new copy (--updated-from <pid>): waits for the old process to exit, then removes its file.
void FinishUpdate(DWORD oldPid);

// Removes "<exe>.old" left by an update whose cleanup could not run (one try, no waiting).
void RemoveLeftover();

}  // namespace self_update
