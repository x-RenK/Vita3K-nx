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

#include <renderer/gl/types.h>
#include <renderer/types.h>
#include <util/log.h>

#include <algorithm>

namespace renderer::gl {
bool set_uniform_buffer(GLContext &context, const ShaderProgram *program, const bool vertex_shader, const int block_num, const int size, uint8_t *data) {
    auto offset = program->uniform_buffer_data_offsets.at(block_num);
    if (offset == static_cast<std::uint32_t>(-1)) {
        return true;
    }

    const size_t data_size_upload = std::min<size_t>(size, program->uniform_buffer_sizes.at(block_num) * 4ull);
    const size_t offset_start_upload = offset * 4ull;
    const size_t dirty_size = program->buffer_store ? ((program->max_total_uniform_buffer_storage + 31) / 32) * 4 : 0;
    const size_t storage_size = (program->max_total_uniform_buffer_storage * 4 + dirty_size + 15) & ~size_t(15);
    auto &storage = vertex_shader ? context.vertex_uniform_buffer_storage_ptr : context.fragment_uniform_buffer_storage_ptr;
    auto &ring = vertex_shader ? context.vertex_uniform_stream_ring_buffer : context.fragment_uniform_stream_ring_buffer;
    if (!storage.first) {
        storage = ring.allocate(storage_size);
        if (!storage.first) {
            LOG_ERROR("Unable to allocate shader SSBO from persistent mapped buffer");
            return false;
        }
        if (dirty_size) {
            std::memset(storage.first + program->max_total_uniform_buffer_storage * 4, 0, storage_size - program->max_total_uniform_buffer_storage * 4);
            context.uniform_buffer_destinations[vertex_shader ? 0 : 1] = {};
        }
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, vertex_shader ? 0 : 1, ring.handle(), storage.second, storage_size);
    }
    if (program->buffer_store)
        context.uniform_buffer_destinations[vertex_shader ? 0 : 1][block_num] = { data, data_size_upload };
    std::memcpy(storage.first + offset_start_upload, data, data_size_upload);

    return true;
}

void finish_buffer_stores(GLContext &context, const ShaderProgram &program, bool vertex_shader) {
    if (!program.buffer_store || !program.max_total_uniform_buffer_storage)
        return;
    const auto &storage = vertex_shader ? context.vertex_uniform_buffer_storage_ptr : context.fragment_uniform_buffer_storage_ptr;
    if (!storage.first)
        return;
    const auto &ring = vertex_shader ? context.vertex_uniform_stream_ring_buffer : context.fragment_uniform_stream_ring_buffer;
    const size_t words = program.max_total_uniform_buffer_storage;
    auto &readback = context.buffer_store_readback;
    readback.resize(words + (words + 31) / 32);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_COPY_READ_BUFFER, ring.handle());
    glGetBufferSubData(GL_COPY_READ_BUFFER, storage.second, readback.size() * sizeof(uint32_t), readback.data());
    const auto *dirty = readback.data() + words;
    for (size_t buffer = 0; buffer < program.uniform_buffer_sizes.size(); ++buffer) {
        const auto [destination, size] = context.uniform_buffer_destinations[vertex_shader ? 0 : 1][buffer];
        const size_t offset = program.uniform_buffer_data_offsets[buffer];
        if (!destination || offset >= words)
            continue;
        const size_t count = std::min({ size / 4, size_t(program.uniform_buffer_sizes[buffer]), words - offset });
        for (size_t word = 0; word < count; ++word) {
            const size_t index = offset + word;
            if (dirty[index / 32] & (1u << (index % 32))) {
                std::memcpy(destination + word * 4, &readback[index], sizeof(uint32_t));
            }
        }
    }
}
} // namespace renderer::gl
