// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

#include <cheat/state.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct MemState;

namespace cheat {

// Lets an ARM write ($A) drop the recompiler blocks already translated for the patched range.
using JitInvalidate = std::function<void(uint32_t address, size_t size)>;

void unload(CheatState &state);

bool load(CheatState &state, const fs::path &cheats_dir, const std::string &title_id);

// The first folder holding a file for the title wins.
bool load(CheatState &state, const std::vector<fs::path> &cheats_dirs, const std::string &title_id);

bool reload(CheatState &state, const std::vector<fs::path> &cheats_dirs, const std::string &title_id, MemState &mem, const JitInvalidate &invalidate_jit);

// Module index 0 of a `$B2` code is whichever module is registered first.
void add_module(CheatState &state, const CheatModule &module);

void set_buttons(CheatState &state, uint32_t buttons);

void apply(CheatState &state, MemState &mem, const JitInvalidate &invalidate_jit);

void set_cheat_enabled(CheatState &state, size_t index, bool enabled, MemState &mem, const JitInvalidate &invalidate_jit);

void set_all_cheats_enabled(CheatState &state, bool enabled, MemState &mem, const JitInvalidate &invalidate_jit);

// Mirrors the `enable-cheats` setting, turning it off restores every ARM write.
void set_enabled(CheatState &state, bool enabled, MemState &mem, const JitInvalidate &invalidate_jit);

size_t cheat_count(const CheatState &state);

size_t enabled_cheat_count(const CheatState &state);

CheatFile snapshot(const CheatState &state);

// Writes the on/off state of every cheat back to its file, or to a copy in `cheats_dir` when the
// file lives elsewhere, see save_cheat_file().
bool save(CheatState &state, const fs::path &cheats_dir = {});

} // namespace cheat
