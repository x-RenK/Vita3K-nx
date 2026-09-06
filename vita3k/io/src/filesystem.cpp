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

#include <io/filesystem.h>
#include <io/util.h>

#ifdef __SWITCH__
SharedFilePtr open_shared_file(const fs::path &path, int flags) {
    // One writable host stream permits later guest writers without reopening a live reader.
    const auto host_path = path.generic_path().string();
    FILE *file = fopen(host_path.c_str(), "rb+");
    const bool writable = file != nullptr;
    if (!file && !(flags & SCE_O_WRONLY))
        file = fopen(host_path.c_str(), "rb");
    if (!file)
        return {};
    setvbuf(file, nullptr, _IOFBF, 256 * 1024);
    return std::make_shared<SharedFile>(SharedFile{ FilePtr(file, std::fclose), writable });
}
#endif

#ifdef _WIN32
// To open wide files for Boost.Filesystem, we also need the appropriate wide mode flags for Windows, and normal flags for other OS
const wchar_t *translate_open_mode(const int flags) {
    if (flags & SCE_O_WRONLY) {
        if (flags & SCE_O_RDONLY) {
            if (flags & SCE_O_APPEND) {
                return L"ab+";
            }
            return L"rb+";
        }
        if (flags & SCE_O_APPEND) {
            return L"ab";
        }
        return L"rb+";
    }
    return L"rb";
}
#else
const char *translate_open_mode(const int flags) {
    if (flags & SCE_O_WRONLY) {
        if (flags & SCE_O_RDONLY) {
            if (flags & SCE_O_APPEND) {
                return "ab+";
            }
            return "rb+";
        }
        if (flags & SCE_O_APPEND) {
            return "ab";
        }
        return "rb+";
    }
    return "rb";
}
#endif
