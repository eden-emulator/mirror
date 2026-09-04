// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <random>

#include "common/fs/fs.h"
#include "common/fs/fs_util.h"
#include "common/logging.h"
#include "video_core/post_processing/fx_chain.h"
#include "video_core/post_processing/fx_compile.h"
#include "video_core/post_processing/fx_effect.h"
#include "video_core/renderer_vulkan/present/post_process.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

namespace {

constexpr VkFormat BACKBUFFER_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr size_t NO_TEXTURE = ~size_t{0};

VkFormat ToVkFormat(reshadefx::texture_format format) {
    switch (format) {
    case reshadefx::texture_format::r8:
        return VK_FORMAT_R8_UNORM;
    case reshadefx::texture_format::r16f:
        return VK_FORMAT_R16_SFLOAT;
    case reshadefx::texture_format::r32f:
        return VK_FORMAT_R32_SFLOAT;
    case reshadefx::texture_format::rg8:
        return VK_FORMAT_R8G8_UNORM;
    case reshadefx::texture_format::rg16:
        return VK_FORMAT_R16G16_UNORM;
    case reshadefx::texture_format::rg16f:
        return VK_FORMAT_R16G16_SFLOAT;
    case reshadefx::texture_format::rg32f:
        return VK_FORMAT_R32G32_SFLOAT;
    case reshadefx::texture_format::rgba8:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case reshadefx::texture_format::rgba16:
        return VK_FORMAT_R16G16B16A16_UNORM;
    case reshadefx::texture_format::rgba16f:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case reshadefx::texture_format::rgba32f:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case reshadefx::texture_format::rgb10a2:
        return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    default:
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

VkSamplerAddressMode ToAddressMode(reshadefx::texture_address_mode mode) {
    switch (mode) {
    case reshadefx::texture_address_mode::wrap:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case reshadefx::texture_address_mode::mirror:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case reshadefx::texture_address_mode::border:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case reshadefx::texture_address_mode::clamp:
    default:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}

VkBlendFactor ToBlendFactor(reshadefx::blend_factor func) {
    switch (func) {
    case reshadefx::blend_factor::zero:
        return VK_BLEND_FACTOR_ZERO;
    case reshadefx::blend_factor::source_color:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case reshadefx::blend_factor::source_alpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case reshadefx::blend_factor::one_minus_source_color:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case reshadefx::blend_factor::one_minus_source_alpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case reshadefx::blend_factor::dest_color:
        return VK_BLEND_FACTOR_DST_COLOR;
    case reshadefx::blend_factor::dest_alpha:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case reshadefx::blend_factor::one_minus_dest_color:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case reshadefx::blend_factor::one_minus_dest_alpha:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case reshadefx::blend_factor::one:
    default:
        return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp ToBlendOp(reshadefx::blend_op op) {
    switch (op) {
    case reshadefx::blend_op::subtract:
        return VK_BLEND_OP_SUBTRACT;
    case reshadefx::blend_op::reverse_subtract:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case reshadefx::blend_op::min:
        return VK_BLEND_OP_MIN;
    case reshadefx::blend_op::max:
        return VK_BLEND_OP_MAX;
    case reshadefx::blend_op::add:
    default:
        return VK_BLEND_OP_ADD;
    }
}

VkPrimitiveTopology ToTopology(reshadefx::primitive_topology topology) {
    switch (topology) {
    case reshadefx::primitive_topology::point_list:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case reshadefx::primitive_topology::line_list:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case reshadefx::primitive_topology::line_strip:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case reshadefx::primitive_topology::triangle_strip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case reshadefx::primitive_topology::triangle_list:
    default:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

vk::RenderPass CreateFxRenderPass(const Device& device, VkFormat format, bool clear) {
    VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_GENERAL;
    if (clear) {
        load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
        initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    const VkAttachmentDescription attachment{
        .flags = 0,
        .format = format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = load_op,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = initial_layout,
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    const VkAttachmentReference reference{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    };

    const VkSubpassDescription subpass{
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachments = &reference,
        .pResolveAttachments = nullptr,
        .pDepthStencilAttachment = nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr,
    };

    return device.GetLogical().CreateRenderPass(VkRenderPassCreateInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 0,
        .pDependencies = nullptr,
    });
}

vk::Pipeline CreateFxPipeline(const Device& device, vk::RenderPass& renderpass,
                              vk::PipelineLayout& layout, VkShaderModule vertex_shader,
                              VkShaderModule fragment_shader,
                              const reshadefx::pass& pass) {
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shader,
            .pName = pass.vs_entry_point.c_str(),
            .pSpecializationInfo = nullptr,
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_shader,
            .pName = pass.ps_entry_point.c_str(),
            .pSpecializationInfo = nullptr,
        },
    }};

    constexpr VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr,
    };

    const VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = ToTopology(pass.topology),
        .primitiveRestartEnable = VK_FALSE,
    };

    constexpr VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr,
    };

    constexpr VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };

    constexpr VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 0.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    VkBool32 blend_enable = VK_FALSE;
    if (pass.blend_enable[0]) {
        blend_enable = VK_TRUE;
    }

    const VkPipelineColorBlendAttachmentState blending{
        .blendEnable = blend_enable,
        .srcColorBlendFactor = ToBlendFactor(pass.source_color_blend_factor[0]),
        .dstColorBlendFactor = ToBlendFactor(pass.dest_color_blend_factor[0]),
        .colorBlendOp = ToBlendOp(pass.color_blend_op[0]),
        .srcAlphaBlendFactor = ToBlendFactor(pass.source_alpha_blend_factor[0]),
        .dstAlphaBlendFactor = ToBlendFactor(pass.dest_alpha_blend_factor[0]),
        .alphaBlendOp = ToBlendOp(pass.alpha_blend_op[0]),
        .colorWriteMask = static_cast<VkColorComponentFlags>(pass.render_target_write_mask[0] & 0xF),
    };

    const VkPipelineColorBlendStateCreateInfo color_blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &blending,
        .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f},
    };

    constexpr std::array dynamic_states{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    const VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    return device.GetLogical().CreateGraphicsPipeline(VkGraphicsPipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stageCount = static_cast<u32>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState = nullptr,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = *layout,
        .renderPass = *renderpass,
        .subpass = 0,
        .basePipelineHandle = nullptr,
        .basePipelineIndex = 0,
    });
}

} // Anonymous namespace

PostProcessChain::PostProcessChain(const Device& device, MemoryAllocator& allocator,
                                   Scheduler& scheduler, size_t image_count, VkExtent2D extent)
    : m_extent(extent)
    , m_image_count(u32(image_count))
{
    m_start = std::chrono::steady_clock::now();
    m_previous = m_start;

    CreatePingPongImages(device, allocator);

    m_fallback_sampler = CreateWrappedSampler(device);
    m_fallback_image = CreateWrappedImage(allocator, VkExtent2D{1, 1}, VK_FORMAT_R8G8B8A8_UNORM);
    m_fallback_view = CreateWrappedImageView(device, m_fallback_image, VK_FORMAT_R8G8B8A8_UNORM);

    if (!BuildEffects(device, allocator, scheduler)) {
        m_effects.clear();
    }
}

PostProcessChain::~PostProcessChain() = default;

bool PostProcessChain::Empty() const {
    return m_effects.empty();
}

void PostProcessChain::CreatePingPongImages(const Device& device, MemoryAllocator& allocator) {
    m_frames.resize(m_image_count);
    for (auto& frame : m_frames) {
        for (size_t i = 0; i < frame.images.size(); ++i) {
            frame.images[i] = CreateWrappedImage(allocator, m_extent, BACKBUFFER_FORMAT);
            frame.views[i] = CreateWrappedImageView(device, frame.images[i], BACKBUFFER_FORMAT);
        }
    }
}

bool PostProcessChain::BuildEffects(const Device& device, MemoryAllocator& allocator,
                                    Scheduler& scheduler) {
    const auto snapshot = VideoCore::FxChain::Instance().Snapshot();
    if (snapshot.entries.empty()) {
        return true;
    }

    const auto root = VideoCore::GetFxRootDirectory();

    for (size_t entry_index = 0; entry_index < snapshot.entries.size(); ++entry_index) {
        const auto& entry = snapshot.entries[entry_index];
        const auto path = root / entry.file;

        const auto compiled = VideoCore::CompileFxEffect(path, m_extent.width, m_extent.height, 8);
        if (!compiled.Succeeded()) {
            LOG_ERROR(Render_Vulkan, "Post-processing effect '{}' failed to compile:\n{}",
                      entry.file, compiled.error);
            continue;
        }

        const auto& module = compiled.module;

        const auto technique = std::find_if(
            module.techniques.begin(), module.techniques.end(),
            [&](const reshadefx::technique& t) { return t.name == entry.technique; });
        if (technique == module.techniques.end()) {
            LOG_ERROR(Render_Vulkan, "Effect '{}' has no technique '{}'", entry.file,
                      entry.technique);
            continue;
        }

        Effect effect;
        effect.entry_index = entry_index;
        effect.file = entry.file;
        effect.uniform_size = module.total_uniform_size;
        for (const auto& [name, words] : compiled.entry_points) {
            effect.shaders.emplace(name, CreateWrappedShaderModule(device, words));
        }

        for (const auto& texture : module.textures) {
            Texture out;
            out.name = texture.unique_name;
            out.extent = VkExtent2D{texture.width, texture.height};
            out.format = ToVkFormat(texture.format);

            if (texture.semantic == "COLOR") {
                out.is_backbuffer = true;
                effect.textures.push_back(std::move(out));
                continue;
            }
            if (texture.semantic == "DEPTH") {
                effect.textures.push_back(std::move(out));
                continue;
            }

            out.image = CreateWrappedImage(allocator, out.extent, out.format);
            out.view = CreateWrappedImageView(device, out.image, out.format);
            effect.textures.push_back(std::move(out));
        }

        for (const auto& sampler : module.samplers) {
            Sampler out;
            out.texture_index = NO_TEXTURE;
            for (size_t i = 0; i < effect.textures.size(); ++i) {
                if (effect.textures[i].name == sampler.texture_name) {
                    out.texture_index = i;
                    break;
                }
            }

            VkFilter mag_filter = VK_FILTER_LINEAR;
            VkFilter min_filter = VK_FILTER_LINEAR;
            VkSamplerMipmapMode mip_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            const u32 filter = static_cast<u32>(sampler.filter);
            if ((filter & 0x10) == 0) {
                min_filter = VK_FILTER_NEAREST;
            }
            if ((filter & 0x04) == 0) {
                mag_filter = VK_FILTER_NEAREST;
            }
            if ((filter & 0x01) == 0) {
                mip_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            }

            out.sampler = device.GetLogical().CreateSampler(VkSamplerCreateInfo{
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .magFilter = mag_filter,
                .minFilter = min_filter,
                .mipmapMode = mip_mode,
                .addressModeU = ToAddressMode(sampler.address_u),
                .addressModeV = ToAddressMode(sampler.address_v),
                .addressModeW = ToAddressMode(sampler.address_w),
                .mipLodBias = sampler.lod_bias,
                .anisotropyEnable = VK_FALSE,
                .maxAnisotropy = 1.0f,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_NEVER,
                .minLod = sampler.min_lod,
                .maxLod = sampler.max_lod,
                .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
                .unnormalizedCoordinates = VK_FALSE,
            });

            effect.samplers.push_back(std::move(out));
        }

        for (const auto& uniform : module.uniforms) {
            UniformWrite write;
            write.name = uniform.name;
            write.offset = uniform.offset;
            write.components = std::min<u32>(uniform.type.components(), 4);
            write.kind = UniformKind::Floating;
            if (uniform.type.is_boolean()) {
                write.kind = UniformKind::Boolean;
            } else if (uniform.type.is_integral()) {
                write.kind = UniformKind::Integer;
            }

            for (const auto& annotation : uniform.annotations) {
                if (annotation.name != "source") {
                    continue;
                }
                const std::string& source = annotation.value.string_data;
                if (source == "frametime") {
                    write.source = UniformSource::FrameTime;
                } else if (source == "framecount") {
                    write.source = UniformSource::FrameCount;
                } else if (source == "timer") {
                    write.source = UniformSource::Timer;
                } else if (source == "random") {
                    write.source = UniformSource::Random;
                } else if (source == "pingpong") {
                    write.source = UniformSource::PingPong;
                }
            }

            if (uniform.has_initializer_value) {
                for (u32 i = 0; i < write.components; ++i) {
                    if (write.kind == UniformKind::Floating) {
                        write.fallback[i] = uniform.initializer_value.as_float[i];
                    } else {
                        write.fallback[i] = static_cast<f32>(uniform.initializer_value.as_int[i]);
                    }
                }
            }

            write.args = {0.0f, 1.0f, 1.0f, 0.0f};
            for (const auto& annotation : uniform.annotations) {
                if (annotation.name == "min" && annotation.type.is_floating_point()) {
                    write.args[0] = annotation.value.as_float[0];
                }
                if (annotation.name == "max" && annotation.type.is_floating_point()) {
                    write.args[1] = annotation.value.as_float[0];
                }
                if (annotation.name == "step" && annotation.type.is_floating_point()) {
                    write.args[2] = annotation.value.as_float[0];
                }
            }
            write.state = write.args[0];

            effect.uniforms.push_back(std::move(write));
        }

        effect.uniform_layout = CreateWrappedDescriptorSetLayout(
            device, std::array{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

        for (const auto& pass : technique->passes) {
            Pass out;
            out.num_vertices = pass.num_vertices;
            out.clear = pass.clear_render_targets != 0;
            out.target_texture = NO_TEXTURE;
            out.extent = m_extent;

            const std::string& target = pass.render_target_names[0];
            if (target.empty()) {
                out.writes_backbuffer = true;
            } else {
                for (size_t i = 0; i < effect.textures.size(); ++i) {
                    if (effect.textures[i].name == target) {
                        out.target_texture = i;
                        out.extent = effect.textures[i].extent;
                        break;
                    }
                }
                if (out.target_texture == NO_TEXTURE) {
                    LOG_WARNING(Render_Vulkan, "Effect '{}' pass targets unknown texture '{}'",
                                entry.file, target);
                    out.writes_backbuffer = true;
                }
            }

            if (pass.viewport_width != 0 && pass.viewport_height != 0) {
                out.extent = VkExtent2D{pass.viewport_width, pass.viewport_height};
            }

            VkFormat target_format = BACKBUFFER_FORMAT;
            if (!out.writes_backbuffer) {
                target_format = effect.textures[out.target_texture].format;
            }

            u32 binding_count = 0;
            for (const auto& binding : pass.sampler_bindings) {
                SamplerBinding entry_binding;
                entry_binding.binding = binding.entry_point_binding;
                entry_binding.sampler_index = binding.index;
                out.sampler_bindings.push_back(entry_binding);
                binding_count = std::max(binding_count, binding.entry_point_binding + 1);
            }

            const std::vector<VkDescriptorType> sampler_types(
                std::max<size_t>(binding_count, 1), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            out.sampler_layout = CreateWrappedDescriptorSetLayout(
                device, sampler_types, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

            const std::array set_layouts{*effect.uniform_layout, *out.sampler_layout};
            out.pipeline_layout =
                device.GetLogical().CreatePipelineLayout(VkPipelineLayoutCreateInfo{
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .setLayoutCount = static_cast<u32>(set_layouts.size()),
                    .pSetLayouts = set_layouts.data(),
                    .pushConstantRangeCount = 0,
                    .pPushConstantRanges = nullptr,
                });

            const auto vertex_shader = effect.shaders.find(pass.vs_entry_point);
            const auto fragment_shader = effect.shaders.find(pass.ps_entry_point);
            if (vertex_shader == effect.shaders.end() ||
                fragment_shader == effect.shaders.end()) {
                LOG_WARNING(Render_Vulkan, "Effect '{}' pass references a missing entry point",
                            entry.file);
                continue;
            }

            out.renderpass = CreateFxRenderPass(device, target_format, out.clear);
            out.pipeline = CreateFxPipeline(device, out.renderpass, out.pipeline_layout,
                                            *vertex_shader->second, *fragment_shader->second, pass);

            if (out.writes_backbuffer) {
                out.backbuffer_slot = static_cast<u32>(effect.backbuffer_pass_count % 2);
                ++effect.backbuffer_pass_count;
                for (u32 image = 0; image < m_image_count; ++image) {
                    for (size_t slot = 0; slot < 2; ++slot) {
                        out.framebuffers.push_back(CreateWrappedFramebuffer(
                            device, out.renderpass, m_frames[image].views[slot], out.extent));
                    }
                }
            } else {
                out.framebuffers.push_back(
                    CreateWrappedFramebuffer(device, out.renderpass,
                                             effect.textures[out.target_texture].view, out.extent));
            }

            effect.passes.push_back(std::move(out));
        }

        if (effect.passes.empty()) {
            LOG_WARNING(Render_Vulkan, "Effect '{}' technique '{}' has no passes", entry.file,
                        entry.technique);
            continue;
        }

        const u32 buffer_size = std::max<u32>(effect.uniform_size, 4);
        for (u32 i = 0; i < m_image_count; ++i) {
            effect.uniform_buffers.push_back(
                CreateWrappedBuffer(allocator, buffer_size, MemoryUsage::Upload));
        }

        size_t sampler_descriptor_count = 0;
        size_t sampler_set_count = 0;
        for (const auto& pass : effect.passes) {
            sampler_descriptor_count +=
                m_image_count * std::max<size_t>(pass.sampler_bindings.size(), 1);
            sampler_set_count += m_image_count;
        }

        effect.descriptor_pool = CreateWrappedDescriptorPool(
            device, m_image_count + sampler_descriptor_count, m_image_count + sampler_set_count,
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});

        const std::vector<VkDescriptorSetLayout> uniform_layouts(m_image_count,
                                                                 *effect.uniform_layout);
        effect.uniform_sets = CreateWrappedDescriptorSets(effect.descriptor_pool, uniform_layouts);

        for (auto& pass : effect.passes) {
            const std::vector<VkDescriptorSetLayout> layouts(m_image_count, *pass.sampler_layout);
            pass.sampler_sets = CreateWrappedDescriptorSets(effect.descriptor_pool, layouts);
        }

        m_effects.push_back(std::move(effect));
    }

    return true;
}

void PostProcessChain::PrepareImages(const Device& device, Scheduler& scheduler) {
    if (m_images_ready) {
        return;
    }

    scheduler.Record([this](vk::CommandBuffer cmdbuf) {
        ClearColorImage(cmdbuf, *m_fallback_image);
        for (auto& frame : m_frames) {
            for (auto& image : frame.images) {
                ClearColorImage(cmdbuf, *image);
            }
        }
        for (auto& effect : m_effects) {
            for (auto& texture : effect.textures) {
                if (texture.image) {
                    ClearColorImage(cmdbuf, *texture.image);
                }
            }
        }
    });
    scheduler.Finish();

    m_images_ready = true;
}

void PostProcessChain::UpdateUniforms(Effect& effect, size_t image_index, f32 delta_seconds) {
    if (effect.uniform_size == 0) {
        return;
    }

    static thread_local std::mt19937 rng{std::random_device{}()};

    std::vector<u8> staging(effect.uniform_size, 0);
    const f32 elapsed =
        std::chrono::duration<f32>(std::chrono::steady_clock::now() - m_start).count();
    const auto overrides = VideoCore::FxChain::Instance().EntryValues(effect.entry_index);

    for (auto& uniform : effect.uniforms) {
        std::array<f32, 4> value = uniform.fallback;

        const auto override = overrides.find(uniform.name);
        if (override != overrides.end()) {
            value = override->second;
        }

        switch (uniform.source) {
        case UniformSource::FrameTime:
            value[0] = delta_seconds * 1000.0f;
            break;
        case UniformSource::FrameCount:
            value[0] = static_cast<f32>(m_frame_count);
            break;
        case UniformSource::Timer:
            value[0] = elapsed * 1000.0f;
            break;
        case UniformSource::Random: {
            const int low = static_cast<int>(uniform.args[0]);
            int high = static_cast<int>(uniform.args[1]);
            if (high <= low) {
                high = low + 1;
            }
            std::uniform_int_distribution<int> dist(low, high);
            value[0] = static_cast<f32>(dist(rng));
            break;
        }
        case UniformSource::PingPong: {
            const f32 min_value = uniform.args[0];
            f32 max_value = uniform.args[1];
            if (max_value <= min_value) {
                max_value = min_value + 1.0f;
            }
            f32 step = uniform.args[2];
            if (step == 0.0f) {
                step = 1.0f;
            }
            uniform.state += uniform.direction * step * delta_seconds;
            if (uniform.state >= max_value) {
                uniform.state = max_value;
                uniform.direction = -1.0f;
            }
            if (uniform.state <= min_value) {
                uniform.state = min_value;
                uniform.direction = 1.0f;
            }
            value[0] = uniform.state;
            value[1] = uniform.direction;
            break;
        }
        case UniformSource::Value:
        default:
            break;
        }

        for (u32 i = 0; i < uniform.components; ++i) {
            const size_t offset = uniform.offset + i * sizeof(u32);
            if (offset + sizeof(u32) > staging.size()) {
                break;
            }
            if (uniform.kind == UniformKind::Floating) {
                const f32 element = value[i];
                std::memcpy(staging.data() + offset, &element, sizeof(f32));
            } else {
                const s32 element = static_cast<s32>(value[i]);
                std::memcpy(staging.data() + offset, &element, sizeof(s32));
            }
        }
    }

    const std::span<u8> mapped = effect.uniform_buffers[image_index].Mapped();
    if (mapped.size() >= staging.size()) {
        std::memcpy(mapped.data(), staging.data(), staging.size());
        effect.uniform_buffers[image_index].Flush();
    }
}

void PostProcessChain::UpdateDescriptors(const Device& device, Effect& effect, Pass& pass,
                                         size_t image_index, VkImageView backbuffer_view) {
    std::vector<VkDescriptorImageInfo> image_infos;
    std::vector<VkWriteDescriptorSet> writes;
    image_infos.reserve(pass.sampler_bindings.size() + 1);

    const VkDescriptorSet sampler_set = pass.sampler_sets[image_index];

    for (const auto& binding : pass.sampler_bindings) {
        VkImageView view = *m_fallback_view;
        VkSampler handle = *m_fallback_sampler;

        if (binding.sampler_index < effect.samplers.size()) {
            const Sampler& sampler = effect.samplers[binding.sampler_index];
            if (sampler.sampler) {
                handle = *sampler.sampler;
            }
            if (sampler.texture_index != NO_TEXTURE) {
                const Texture& texture = effect.textures[sampler.texture_index];
                if (texture.is_backbuffer) {
                    view = backbuffer_view;
                } else if (texture.view) {
                    view = *texture.view;
                }
            }
        }

        writes.push_back(
            CreateWriteDescriptorSet(image_infos, handle, view, sampler_set, binding.binding));
    }

    const VkDescriptorBufferInfo buffer_info{
        .buffer = *effect.uniform_buffers[image_index],
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    writes.push_back(VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = effect.uniform_sets[image_index],
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pImageInfo = nullptr,
        .pBufferInfo = &buffer_info,
        .pTexelBufferView = nullptr,
    });

    device.GetLogical().UpdateDescriptorSets(writes, {});
}

void PostProcessChain::Draw(const Device& device, Scheduler& scheduler, size_t image_index,
                            VkImage* inout_image, VkImageView* inout_image_view) {
    if (m_effects.empty()) {
        return;
    }

    PrepareImages(device, scheduler);

    const auto now = std::chrono::steady_clock::now();
    const f32 delta_seconds = std::chrono::duration<f32>(now - m_previous).count();
    m_previous = now;
    ++m_frame_count;

    FrameImages& frame = m_frames[image_index];
    VkImage current_image = *inout_image;
    VkImageView current_view = *inout_image_view;
    u32 slot = 0;

    for (auto& effect : m_effects) {
        UpdateUniforms(effect, image_index, delta_seconds);

        for (size_t pass_index = 0; pass_index < effect.passes.size(); ++pass_index) {
            Pass& pass = effect.passes[pass_index];

            UpdateDescriptors(device, effect, pass, image_index, current_view);

            VkFramebuffer framebuffer{};
            VkImage target_image{};
            if (pass.writes_backbuffer) {
                const u32 target_slot = (slot + 1) % 2;
                framebuffer = *pass.framebuffers[image_index * 2 + target_slot];
                target_image = *frame.images[target_slot];
            } else {
                framebuffer = *pass.framebuffers[0];
                target_image = *effect.textures[pass.target_texture].image;
            }

            const VkImage source_image = current_image;
            const VkRenderPass renderpass = *pass.renderpass;
            const VkPipeline pipeline = *pass.pipeline;
            const VkPipelineLayout layout = *pass.pipeline_layout;
            const VkDescriptorSet uniform_set = effect.uniform_sets[image_index];
            const VkDescriptorSet sampler_set = pass.sampler_sets[image_index];
            const VkExtent2D extent = pass.extent;
            const u32 vertices = pass.num_vertices;

            scheduler.RequestOutsideRenderPassOperationContext();
            scheduler.Record([=](vk::CommandBuffer cmdbuf) {
                TransitionImageLayout(cmdbuf, source_image, VK_IMAGE_LAYOUT_GENERAL);
                TransitionImageLayout(cmdbuf, target_image, VK_IMAGE_LAYOUT_GENERAL);
                BeginRenderPass(cmdbuf, renderpass, framebuffer, extent);
                cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                                          std::array{uniform_set, sampler_set}, {});
                cmdbuf.Draw(vertices, 1, 0, 0);
                cmdbuf.EndRenderPass();
                TransitionImageLayout(cmdbuf, target_image, VK_IMAGE_LAYOUT_GENERAL);
            });

            if (pass.writes_backbuffer) {
                slot = (slot + 1) % 2;
                current_image = *frame.images[slot];
                current_view = *frame.views[slot];
            }
        }
    }

    *inout_image = current_image;
    *inout_image_view = current_view;
}

} // namespace Vulkan
