// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <bitset>

#include "common/types.h"
#include "frontend/fetch_shader.h"
#include "shader_recompiler/backend/bindings.h"
#include "shader_recompiler/info.h"

namespace Shader {

struct VsAttribSpecialization {
    AmdGpu::NumberClass num_class{};

    auto operator<=>(const VsAttribSpecialization&) const = default;
};

struct BufferSpecialization {
    u16 stride : 14;
    u16 is_storage : 1;
    u32 size = 0;

    bool operator==(const BufferSpecialization& other) const {
        return stride == other.stride && is_storage == other.is_storage &&
               (size >= other.is_storage || is_storage);
    }
};

struct TextureBufferSpecialization {
    bool is_integer = false;
    AmdGpu::CompMapping dst_select{};
    AmdGpu::NumberConversion num_conversion{};

    auto operator<=>(const TextureBufferSpecialization&) const = default;
};

struct ImageSpecialization {
    AmdGpu::ImageType type = AmdGpu::ImageType::Color2D;
    bool is_integer = false;
    bool is_storage = false;
    AmdGpu::CompMapping dst_select{};
    AmdGpu::NumberConversion num_conversion{};

    auto operator<=>(const ImageSpecialization&) const = default;
};

struct FMaskSpecialization {
    u32 width;
    u32 height;

    auto operator<=>(const FMaskSpecialization&) const = default;
};

struct SamplerSpecialization {
    bool force_unnormalized = false;

    auto operator<=>(const SamplerSpecialization&) const = default;
};

/**
 * Alongside runtime information, this structure also checks bound resources
 * for compatibility. Can be used as a key for storing shader permutations.
 * Is separate from runtime information, because resource layout can only be deduced
 * after the first compilation of a module.
 */
struct StageSpecialization {
    static constexpr size_t MaxStageResources = 64;

    const Shader::Info* info;
    RuntimeInfo runtime_info;
    std::optional<Gcn::FetchShaderData> fetch_shader_data{};
    boost::container::small_vector<VsAttribSpecialization, 32> vs_attribs;
    std::bitset<MaxStageResources> bitset{};
    boost::container::small_vector<BufferSpecialization, 16> buffers;
    boost::container::small_vector<TextureBufferSpecialization, 8> tex_buffers;
    boost::container::small_vector<ImageSpecialization, 16> images;
    boost::container::small_vector<FMaskSpecialization, 8> fmasks;
    boost::container::small_vector<SamplerSpecialization, 16> samplers;
    Backend::Bindings start{};

    explicit StageSpecialization(const Info& info_, RuntimeInfo runtime_info_,
                                 const Profile& profile_, Backend::Bindings start_)
        : info{&info_}, runtime_info{runtime_info_}, start{start_} {
        fetch_shader_data = Gcn::ParseFetchShader(info_);
        if (info_.stage == Stage::Vertex && fetch_shader_data &&
            !profile_.support_legacy_vertex_attributes) {
            // Specialize shader on VS input number types to follow spec.
            ForEachSharp(vs_attribs, fetch_shader_data->attributes,
                         [](auto& spec, const auto& desc, AmdGpu::Buffer sharp) {
                             spec.num_class = AmdGpu::GetNumberClass(sharp.GetNumberFmt());
                         });
        }
        u32 binding{};
        if (info->has_readconst) {
            binding++;
        }
        ForEachSharp(binding, buffers, info->buffers,
                     [](auto& spec, const auto& desc, AmdGpu::Buffer sharp) {
                         spec.stride = sharp.GetStride();
                         spec.is_storage = desc.IsStorage(sharp);
                         if (!spec.is_storage) {
                             spec.size = sharp.GetSize();
                         }
                     });
        ForEachSharp(binding, tex_buffers, info->texture_buffers,
                     [](auto& spec, const auto& desc, AmdGpu::Buffer sharp) {
                         spec.is_integer = AmdGpu::IsInteger(sharp.GetNumberFmt());
                         spec.dst_select = sharp.DstSelect();
                         spec.num_conversion = sharp.GetNumberConversion();
                     });
        ForEachSharp(binding, images, info->images,
                     [](auto& spec, const auto& desc, AmdGpu::Image sharp) {
                         spec.type = sharp.GetBoundType();
                         spec.is_integer = AmdGpu::IsInteger(sharp.GetNumberFmt());
                         spec.is_storage = desc.IsStorage(sharp);
                         if (spec.is_storage) {
                             spec.dst_select = sharp.DstSelect();
                         }
                         spec.num_conversion = sharp.GetNumberConversion();
                     });
        ForEachSharp(binding, fmasks, info->fmasks,
                     [](auto& spec, const auto& desc, AmdGpu::Image sharp) {
                         spec.width = sharp.width;
                         spec.height = sharp.height;
                     });
        ForEachSharp(samplers, info->samplers,
                     [](auto& spec, const auto& desc, AmdGpu::Sampler sharp) {
                         spec.force_unnormalized = sharp.force_unnormalized;
                     });

        // Initialize runtime_info fields that rely on analysis in tessellation passes
        if (info->l_stage == LogicalStage::TessellationControl ||
            info->l_stage == LogicalStage::TessellationEval) {
            Shader::TessellationDataConstantBuffer tess_constants;
            info->ReadTessConstantBuffer(tess_constants);
            if (info->l_stage == LogicalStage::TessellationControl) {
                runtime_info.hs_info.InitFromTessConstants(tess_constants);
            } else {
                runtime_info.vs_info.InitFromTessConstants(tess_constants);
            }
        }
    }

    void ForEachSharp(auto& spec_list, auto& desc_list, auto&& func) {
        for (const auto& desc : desc_list) {
            auto& spec = spec_list.emplace_back();
            const auto sharp = desc.GetSharp(*info);
            if (!sharp) {
                continue;
            }
            func(spec, desc, sharp);
        }
    }

    void ForEachSharp(u32& binding, auto& spec_list, auto& desc_list, auto&& func) {
        for (const auto& desc : desc_list) {
            auto& spec = spec_list.emplace_back();
            const auto sharp = desc.GetSharp(*info);
            if (!sharp) {
                binding++;
                continue;
            }
            bitset.set(binding++);
            func(spec, desc, sharp);
        }
    }

    bool operator==(const StageSpecialization& other) const {
        if (start != other.start) {
            return false;
        }
        if (runtime_info != other.runtime_info) {
            return false;
        }
        if (fetch_shader_data != other.fetch_shader_data) {
            return false;
        }
        for (u32 i = 0; i < vs_attribs.size(); i++) {
            if (vs_attribs[i] != other.vs_attribs[i]) {
                return false;
            }
        }
        u32 binding{};
        if (info->has_readconst != other.info->has_readconst) {
            return false;
        }
        if (info->has_readconst) {
            binding++;
        }
        for (u32 i = 0; i < buffers.size(); i++) {
            if (other.bitset[binding++] && buffers[i] != other.buffers[i]) {
                return false;
            }
        }
        for (u32 i = 0; i < tex_buffers.size(); i++) {
            if (other.bitset[binding++] && tex_buffers[i] != other.tex_buffers[i]) {
                return false;
            }
        }
        for (u32 i = 0; i < images.size(); i++) {
            if (other.bitset[binding++] && images[i] != other.images[i]) {
                return false;
            }
        }
        for (u32 i = 0; i < fmasks.size(); i++) {
            if (other.bitset[binding++] && fmasks[i] != other.fmasks[i]) {
                return false;
            }
        }
        for (u32 i = 0; i < samplers.size(); i++) {
            if (samplers[i] != other.samplers[i]) {
                return false;
            }
        }
        return true;
    }
};

} // namespace Shader
