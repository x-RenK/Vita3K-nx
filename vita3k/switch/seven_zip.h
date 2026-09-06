// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <util/fs.h>

#include <functional>
#include <cstdint>
#include <memory>
#include <vector>

struct ExtractedSevenZip {
    fs::path directory;
    std::vector<fs::path> files;
    ~ExtractedSevenZip();
};

std::unique_ptr<ExtractedSevenZip> extract_seven_zip(const fs::path &source, const fs::path &temporary_parent,
    const std::function<void(const std::string &, uint64_t, uint64_t)> &progress);
