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

#include <glutil/gl.h>
#include <mem/ptr.h>

#include <vector>

namespace renderer::gl {

class VisibilityQueries {
    struct PendingQuery {
        Ptr<uint32_t> destination;
        bool increment;
        float resolution;
    };

    Ptr<uint32_t> buffer;
    uint32_t stride = 0;
    uint32_t index = 0;
    bool enabled = false;
    bool increment = true;
    bool active = false;
    std::vector<GLuint> queries;
    std::vector<PendingQuery> pending;

public:
    VisibilityQueries() = default;
    ~VisibilityQueries();
    VisibilityQueries(const VisibilityQueries &) = delete;
    VisibilityQueries &operator=(const VisibilityQueries &) = delete;

    void set_buffer(Ptr<uint32_t> address, uint32_t stride_per_core);
    void set_index(bool enable, uint32_t query_index, bool is_increment);
    void begin_draw(float resolution);
    void end_draw();
    void resolve(const MemState &mem);
};

} // namespace renderer::gl
