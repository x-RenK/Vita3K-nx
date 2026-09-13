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

#include <renderer/gl/visibility.h>
#include <util/log.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace renderer::gl {

VisibilityQueries::~VisibilityQueries() {
    end_draw();
    if (!queries.empty())
        glDeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
}

void VisibilityQueries::set_buffer(Ptr<uint32_t> address, uint32_t stride_per_core) {
    buffer = address;
    stride = stride_per_core;
}

void VisibilityQueries::set_index(bool enable, uint32_t query_index, bool is_increment) {
    enabled = enable;
    index = query_index;
    increment = is_increment;
}

void VisibilityQueries::begin_draw(float resolution) {
    assert(!active);
    if (!enabled || !buffer)
        return;
    const uint64_t address = uint64_t(buffer.address()) + uint64_t(index) * sizeof(uint32_t);
    if (index >= stride / sizeof(uint32_t) || address > std::numeric_limits<uint32_t>::max() - 3) {
        LOG_WARN_ONCE("OpenGL visibility index {} exceeds buffer stride {}", index, stride);
        return;
    }
    if (pending.size() == queries.size()) {
        GLuint query = 0;
        glGenQueries(1, &query);
        if (!query) {
            LOG_ERROR("Failed to allocate OpenGL visibility query");
            return;
        }
        queries.push_back(query);
    }
    glBeginQuery(GL_SAMPLES_PASSED, queries[pending.size()]);
    pending.push_back({ Ptr<uint32_t>(static_cast<uint32_t>(address)), increment, resolution });
    active = true;
}

void VisibilityQueries::end_draw() {
    if (active) {
        glEndQuery(GL_SAMPLES_PASSED);
        active = false;
    }
}

void VisibilityQueries::resolve(const MemState &mem) {
    assert(!active);
    if (pending.empty())
        return;

    for (size_t i = 0; i < pending.size(); ++i) {
        GLuint64 samples = 0;
        glGetQueryObjectui64v(queries[i], GL_QUERY_RESULT, &samples);
        const auto &query = pending[i];
        auto *destination = query.destination.get(mem);
        if (samples) {
            if (query.increment) {
                const double scale = double(query.resolution) * query.resolution;
                const double count = std::max(1.0, std::round(double(samples) / scale));
                const auto value = static_cast<uint32_t>(std::min(count, double(std::numeric_limits<uint32_t>::max())));
                *destination += value;
            } else {
                *destination = 1;
            }
        }
    }
    pending.clear();
}

} // namespace renderer::gl
