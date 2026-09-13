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

#include <cstring>
#include <memory>

#include <renderer/functions.h>
#include <renderer/profile.h>

#include <renderer/gl/functions.h>
#include <renderer/gl/types.h>

#include <gxm/functions.h>

namespace renderer::gl {

using namespace texture;

void texture::apply_sampler_state(const SceGxmTexture &gxm_texture, const GLenum texture_bind_type, const int anisotropic_filtering, bool force_nearest) {
    const SceGxmTextureAddrMode uaddr = (SceGxmTextureAddrMode)(gxm_texture.uaddr_mode);
    const SceGxmTextureAddrMode vaddr = (SceGxmTextureAddrMode)(gxm_texture.vaddr_mode);

    const GLenum min_filter = force_nearest ? GL_NEAREST : translate_minmag_filter((SceGxmTextureFilter)gxm_texture.min_filter);
    const GLenum mag_filter = force_nearest ? GL_NEAREST : translate_minmag_filter((SceGxmTextureFilter)gxm_texture.mag_filter);

    glTexParameteri(texture_bind_type, GL_TEXTURE_WRAP_S, translate_wrap_mode(uaddr));
    glTexParameteri(texture_bind_type, GL_TEXTURE_WRAP_T, translate_wrap_mode(vaddr));
#ifndef __ANDROID__
    glTexParameterf(texture_bind_type, GL_TEXTURE_LOD_BIAS, (static_cast<float>(gxm_texture.lod_bias) - 31.f) / 8.f);
#endif
    glTexParameteri(texture_bind_type, GL_TEXTURE_MIN_LOD, gxm_texture.true_lod_min());
    glTexParameteri(texture_bind_type, GL_TEXTURE_MIN_FILTER, min_filter);
    glTexParameteri(texture_bind_type, GL_TEXTURE_MAG_FILTER, mag_filter);

    // anisotropic filtering
    // when using nearest filter, disable anisotropy as the pixels can contain data other than color
    if (anisotropic_filtering > 1)
        // we don't need to check for the existence of this extension because it is considered an ubiquitous extension
        // for now we apply anisotropic filtering to all textures
        glTexParameterf(texture_bind_type, GL_TEXTURE_MAX_ANISOTROPY_EXT,
            (min_filter != GL_NEAREST || mag_filter != GL_NEAREST) ? static_cast<float>(anisotropic_filtering) : 1.0f);
}

bool GLTextureCache::init(const bool hashless_texture_cache, const fs::path &texture_folder, const std::string_view game_id) {
    TextureCache::init(hashless_texture_cache, texture_folder, game_id);
    backend = Backend::OpenGL;

    // check for dxt support
    int total_extensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &total_extensions);
    for (int i = 0; i < total_extensions; i++) {
        auto ext_name = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
        if (strcmp(ext_name, "GL_EXT_texture_compression_s3tc") == 0) {
            support_dxt = true;
            break;
        }

        if (strcmp(ext_name, "GL_KHR_texture_compression_astc_ldr") == 0) {
            support_astc = true;
            break;
        }
    }

    return textures.init(glGenTextures, glDeleteTextures);
}

void GLTextureCache::cleanup() {
    textures.cleanup();
    texture_lookup.clear();
    texture_queue.items.clear();
    texture_queue.head = nullptr;
    sampler_lookup.clear();
    sampler_queue.items.clear();
    sampler_queue.head = nullptr;
    available_textures_hash.clear();
    exported_textures_hash.clear();
    current_info = nullptr;
    exporting_texture = false;
    importing_texture = false;
    imported_texture_raw_data.clear();
    imported_texture_decoded = nullptr;
    dds_descriptor = nullptr;
}

void GLTextureCache::select(size_t index, const SceGxmTexture &texture) {
    texture_is_srgb = texture.gamma_mode != 0;
    const GLuint gl_texture = textures[index];
    glBindTexture(get_gl_texture_type(texture), gl_texture);
}

static GLenum bcn_to_rgba8(const SceGxmTextureBaseFormat format) {
    switch (format) {
    case SCE_GXM_TEXTURE_BASE_FORMAT_UBC4:
        return GL_R8;
    case SCE_GXM_TEXTURE_BASE_FORMAT_SBC4:
        return GL_R8_SNORM;
    case SCE_GXM_TEXTURE_BASE_FORMAT_UBC5:
        return GL_RG8;
    case SCE_GXM_TEXTURE_BASE_FORMAT_SBC5:
        return GL_RG8_SNORM;
    default:
        // BC1/2/3
        return GL_RGBA8;
    }
}

void GLTextureCache::configure_texture(const SceGxmTexture &gxm_texture) {
    R_PROFILE(__func__);

    const SceGxmTextureFormat fmt = gxm::get_format(gxm_texture);
    const SceGxmTextureBaseFormat base_format = gxm::get_base_format(fmt);
    uint32_t width = gxm::get_width(gxm_texture);
    uint32_t height = gxm::get_height(gxm_texture);
    const GLint *const swizzle = translate_swizzle(fmt);
    uint32_t mip_count = renderer::texture::get_upload_mip(gxm_texture.true_mip_count(), width, height);

    const GLenum texture_bind_type = get_gl_texture_type(gxm_texture);

    // TODO Support mip-mapping.
    if (mip_count)
        glTexParameteri(texture_bind_type, GL_TEXTURE_MAX_LEVEL, mip_count - 1);

#ifdef __ANDROID__
    for (int i = 0; i < 4; i++) {
        glTexParameteri(texture_bind_type, GL_TEXTURE_SWIZZLE_R + i, swizzle[i]);
    }
#else
    glTexParameteriv(texture_bind_type, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
#endif

    apply_sampler_state(gxm_texture, texture_bind_type, anisotropic_filtering);

    const auto texture_type = gxm_texture.texture_type();
    const auto base_fmt = gxm::get_base_format(fmt);

    std::uint32_t org_width = width;
    std::uint32_t org_height = height;

    uint32_t mip_index = 0;

    bool compressed = gxm::is_bcn_format(base_fmt);

    GLenum internal_format = translate_internal_format(base_format);
    GLenum format = translate_format(base_format);
    const GLenum type = compressed ? 0 : translate_type(base_format);

    if (!support_dxt && gxm::is_block_compressed_format(base_format)) {
        // texture is decompressed on the CPU
        internal_format = bcn_to_rgba8(base_format);
        int num_comp = gxm::get_num_components(base_format);
        format = (num_comp == 4) ? GL_RGBA : (num_comp == 2 ? GL_RG : GL_RED);
    }

    if (gxm_texture.gamma_mode)
        internal_format = linear_to_srgb(internal_format);

    // GXM's cube map index is same as OpenGL: right, left, top, bottom, front, back
    GLenum upload_type = GL_TEXTURE_2D;

    std::uint32_t face_total_count = 1;
    std::uint32_t face_iterated = 0;

    if ((texture_type == SCE_GXM_TEXTURE_CUBE) || (texture_type == SCE_GXM_TEXTURE_CUBE_ARBITRARY)) {
        upload_type = GL_TEXTURE_CUBE_MAP_POSITIVE_X;
        face_total_count = 6;
    }

    while (face_iterated < face_total_count && width && height) {
        if (compressed && support_dxt) {
            size_t compressed_size = renderer::texture::get_compressed_size(base_fmt, width, height);
            glCompressedTexImage2D(upload_type, mip_index, internal_format, width, height, 0, static_cast<GLsizei>(compressed_size), nullptr);
        } else {
            glTexImage2D(upload_type, mip_index, internal_format, width, height, 0, format, type, nullptr);
        }

        mip_index++;
        width /= 2;
        height /= 2;

        if (mip_index == mip_count) {
            mip_index = 0;
            face_iterated++;

            upload_type++;

            width = org_width;
            height = org_height;
        }
    }
}

void GLTextureCache::upload_texture_impl(SceGxmTextureBaseFormat base_format, uint32_t width, uint32_t height, uint32_t mip_index, const void *pixels, int face, uint32_t pixels_per_stride) {
    R_PROFILE(__func__);
    // A client-memory upload makes glthread drain its queue and run the pending batch on this
    // thread. Copied into the unpack ring, the upload is just another queued command. The copy
    // covers exactly the rows GL will read: whole strides, and the last row only to its width.
    const auto stage = [&](std::size_t row_bytes, std::size_t stride_bytes, std::size_t rows) -> const void * {
        if (!unpack_ring_buffer)
            unpack_ring_buffer = std::make_unique<RingBuffer>(GL_PIXEL_UNPACK_BUFFER, std::size_t(64) * 1024 * 1024);
        const std::size_t size = rows ? (rows - 1) * stride_bytes + row_bytes : 0;
        const auto [destination, offset] = unpack_ring_buffer->allocate(size);
        if (!destination || !size) {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            return pixels;
        }
        std::memcpy(destination, pixels, size);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack_ring_buffer->handle());
        return reinterpret_cast<const void *>(static_cast<std::uintptr_t>(offset));
    };
    const auto unstage = [&]() {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        if (unpack_ring_buffer)
            unpack_ring_buffer->draw_call_done();
    };

    GLenum upload_type = GL_TEXTURE_2D;
    if (face > 0)
        // GXM's cube map index is same as OpenGL: right, left, top, bottom, front, back
        upload_type = GL_TEXTURE_CUBE_MAP_POSITIVE_X + (face - 1);

    if (gxm::is_bcn_format(base_format) || renderer::texture::is_astc_format(base_format)) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(pixels_per_stride));

        if (gxm::is_bcn_format(base_format)) {
            const GLint block_size = (base_format == SCE_GXM_TEXTURE_BASE_FORMAT_UBC1 || base_format == SCE_GXM_TEXTURE_BASE_FORMAT_UBC4 || base_format == SCE_GXM_TEXTURE_BASE_FORMAT_SBC4)
                ? 8
                : 16;
            glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_SIZE, block_size);
            glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_WIDTH, 4);
            glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_HEIGHT, 4);
        } else {
            // ASTC
            const auto [block_width, block_height] = gxm::get_block_size(base_format);
            // all ASTC blocks are always 16 bytes
            glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_SIZE, 16);
            glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_WIDTH, block_width);
            glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_HEIGHT, block_height);
        }

        GLenum format = translate_format(base_format);
        if (importing_texture ? current_info->is_srgb : texture_is_srgb)
            format = linear_to_srgb(format);
        size_t compressed_size = renderer::texture::get_compressed_size(base_format, width, height);
        const bool bcn = gxm::is_bcn_format(base_format);
        const auto [astc_width, astc_height] = gxm::get_block_size(base_format);
        const std::size_t block_width = bcn ? 4 : astc_width;
        const std::size_t block_height = bcn ? 4 : astc_height;
        const std::size_t block_bytes = bcn ? ((base_format == SCE_GXM_TEXTURE_BASE_FORMAT_UBC1 || base_format == SCE_GXM_TEXTURE_BASE_FORMAT_UBC4 || base_format == SCE_GXM_TEXTURE_BASE_FORMAT_SBC4) ? 8 : 16) : 16;
        const void *source = stage((width + block_width - 1) / block_width * block_bytes,
            (pixels_per_stride + block_width - 1) / block_width * block_bytes, (height + block_height - 1) / block_height);
        glCompressedTexSubImage2D(upload_type, mip_index, 0, 0, width, height, format, static_cast<GLsizei>(compressed_size), source);
        unstage();

        glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_SIZE, 0);
        glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_WIDTH, 0);
        glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_HEIGHT, 0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    } else {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(pixels_per_stride));

        const GLenum format = translate_format(base_format);
        const GLenum type = translate_type(base_format);
        const std::size_t bits = gxm::bits_per_pixel(base_format);
        const void *source = stage((width * bits + 7) / 8, (pixels_per_stride * bits + 7) / 8, height);
        glTexSubImage2D(upload_type, mip_index, 0, 0, width, height, format, type, source);
        unstage();

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
}

void GLTextureCache::import_configure_impl(SceGxmTextureBaseFormat base_format, uint32_t width, uint32_t height, bool is_srgb, uint16_t nb_components, uint16_t mipcount, bool swap_rb) {
    SceGxmTexture &gxm_texture = current_info->texture;
    GLint default_swizzle[] = { GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };
    const GLint *swizzle = default_swizzle;
    if (nb_components == 3)
        default_swizzle[3] = GL_ONE;
    else if (nb_components <= 2)
        swizzle = translate_swizzle(gxm::get_format(gxm_texture));

    if (swap_rb)
        std::swap(default_swizzle[0], default_swizzle[2]);

    const GLenum texture_bind_type = GL_TEXTURE_2D;

    glTexParameteri(texture_bind_type, GL_TEXTURE_MAX_LEVEL, mipcount - 1);
#ifdef __ANDROID__
    for (int i = 0; i < 4; i++) {
        glTexParameteri(texture_bind_type, GL_TEXTURE_SWIZZLE_R + i, swizzle[i]);
    }
#else
    glTexParameteriv(texture_bind_type, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
#endif

    apply_sampler_state(gxm_texture, texture_bind_type, anisotropic_filtering);

    bool compressed = gxm::is_bcn_format(base_format) || renderer::texture::is_astc_format(base_format);
    const GLenum internal_format = is_srgb ? linear_to_srgb(translate_internal_format(base_format)) : translate_internal_format(base_format);
    const GLenum format = translate_format(base_format);
    const GLenum type = compressed ? 0 : translate_type(base_format);

    // GXM's cube map index is same as OpenGL: right, left, top, bottom, front, back
    GLenum upload_type = GL_TEXTURE_2D;

    const bool is_cube = current_info->texture.texture_type() == SCE_GXM_TEXTURE_CUBE || current_info->texture.texture_type() == SCE_GXM_TEXTURE_CUBE_ARBITRARY;
    if (is_cube)
        upload_type = GL_TEXTURE_CUBE_MAP_POSITIVE_X;

    for (uint32_t face = 0; face < (is_cube ? 6U : 1U); face++) {
        uint32_t mip_width = width;
        uint32_t mip_height = height;
        for (uint32_t mip = 0; mip < mipcount; mip++) {
            if (compressed) {
                size_t compressed_size = renderer::texture::get_compressed_size(base_format, mip_width, mip_height);
                glCompressedTexImage2D(upload_type, mip, internal_format, mip_width, mip_height, 0, compressed_size, nullptr);
            } else {
                glTexImage2D(upload_type, mip, internal_format, mip_width, mip_height, 0, format, type, nullptr);
            }
            mip_width /= 2;
            mip_height /= 2;
        }

        upload_type++;
    }
}

namespace texture {

GLenum get_gl_texture_type(const SceGxmTexture &gxm_texture) {
    const std::uint32_t type = gxm_texture.texture_type();
    return ((type == SCE_GXM_TEXTURE_CUBE) || (type == SCE_GXM_TEXTURE_CUBE_ARBITRARY)) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
}

void bind_texture_without_cache(GLTextureCache &cache, const SceGxmTexture &gxm_texture, MemState &mem) {
    R_PROFILE(__func__);
    glBindTexture(get_gl_texture_type(gxm_texture), cache.textures[0]);
    cache.select(0, gxm_texture);
    cache.configure_texture(gxm_texture);
    cache.upload_texture(gxm_texture, mem);
}

} // namespace texture
} // namespace renderer::gl
