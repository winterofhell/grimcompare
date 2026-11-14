#pragma once
// Tooltip block (PROMPT.md Phase 4). The ItemEquipment / ItemArtifact::GetUIDisplayText detours call the
// original, then append lines to the game's own line vector the way the engine does: each line is built
// by the exported GameTextLine ctor (a game-side copy of our text) and moved in with the engine's
// push_back (gdc::scan::line_push_back_call). The game owns and frees every appended line.
// GameTextLineToString is not used: nothing in Game.dll 1.3.1.1 calls it.
#include <cstdint>
#include <string>

#include "gdc/arz.h"
#include "gdc/snapshot.h"
#include "snapshot.h"

namespace gdc::adapter {

// Finds and verifies the engine functions, reads GDCompare.ini [tooltip] and starts loading the game
// database (the parent of `mod_dir`) on a background thread. false (logged): tooltip block off.
bool tooltip_init(const Resolve& resolve, const std::wstring& mod_dir);

// Any thread: the game database once loaded (tooltip_init starts it), else null.
const arz::Database* tooltip_database();

// Writer thread: the snapshot the block compares against, with the probe taken at its capture. Copied.
void tooltip_publish(const snap::Snapshot& s, const Probe& probe);

// Game thread, on the freeze hotkey (Ctrl + the snapshot key): writes GDCompare.presets\frozen-<character>.toml
// (score::freeze) from the last published snapshot and the preset that scores it now. Logged; never throws.
void tooltip_freeze();

// Game thread, from the GameEngine::Update detour: thread id, Shift edges, 60 s stats line.
void tooltip_tick();

// From the GetUIDisplayText detours, after the original filled `lines`. `caller`: the detour's return
// address (the engine code that asked for the text).
void tooltip_after_build(const void* item, void* lines, const void* caller);

}  // namespace gdc::adapter
