/**
 * @file SpriteRenderer.cpp
 * @brief Batched sprite renderer: owns a Vulkan pipeline that samples an atlas texture.
 *
 * Architecture mirrors Renderer2D: a unit quad is drawn N times via instancing.
 * Each instance carries position, size, UV region, and tint color. A single
 * combined-image-sampler descriptor set binds the atlas for the entire batch.
 */

#include "aoc/render/SpriteRenderer.hpp"

#include "vulkan_utils/Shader.hpp"
#include "vulkan_utils/ShaderBuilder.hpp"
#include "vulkan_utils/PipelineLayout.hpp"
#include "vulkan_utils/Texture.hpp"
#include "vulkan_utils/DescriptorSet.hpp"
#include "vulkan_utils/Device.hpp"

#include <vulkan_utils/Logger.hpp>

#include <cstring>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace aoc::render {

// ============================================================================
// Construction / destruction
// ============================================================================

SpriteRenderer::SpriteRenderer(const vkutils::Device& device,
                               VkRenderPass renderPass,
                               VkExtent2D extent,
                               const TextureAtlas& atlas,
                               uint32_t framesInFlight)
    : m_device(device)
    , m_extent(extent) {
    this->m_camera.screenWidth = static_cast<float>(extent.width);
    this->m_camera.screenHeight = static_cast<float>(extent.height);
    this->m_camera.cameraX = 0.0f;
    this->m_camera.cameraY = 0.0f;
    this->m_camera.zoom = 1.0f;
    this->m_instanceBuffers.resize(framesInFlight);
    this->m_sprites.reserve(1024);

    this->createDescriptorResources(atlas);
    this->createBuffers();
    this->createPipeline(renderPass);

    VKLOG_INFO("SpriteRenderer initialized (%ux%u, %u frames in flight)",
               extent.width, extent.height, framesInFlight);
}

SpriteRenderer::~SpriteRenderer() {
    this->cleanup();
}

void SpriteRenderer::cleanup() {
    const VkDevice dev = this->m_device.handle();
    if (dev == VK_NULL_HANDLE) return;

    this->m_device.waitIdle();

    if (this->m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, this->m_pipeline, nullptr);
        this->m_pipeline = VK_NULL_HANDLE;
    }

    this->m_pipelineLayout.reset();

    if (this->m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, this->m_descriptorPool, nullptr);
        this->m_descriptorPool = VK_NULL_HANDLE;
    }

    if (this->m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, this->m_descriptorSetLayout, nullptr);
        this->m_descriptorSetLayout = VK_NULL_HANDLE;
    }

    this->m_vertexBuffer.reset();
    for (vkutils::BufferPtr& buf : this->m_instanceBuffers) {
        buf.reset();
    }
}

// ============================================================================
// Descriptor set: bind the atlas as a combined image sampler at set 0, binding 0
// ============================================================================

void SpriteRenderer::createDescriptorResources(const TextureAtlas& atlas) {
    const VkDevice dev = this->m_device.handle();

    // Layout: one combined image sampler in the fragment shader
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;

    if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr,
                                     &this->m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error(
            "SpriteRenderer: failed to create descriptor set layout [" +
            std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]");
    }

    // Pool: one set with one combined image sampler
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(dev, &poolInfo, nullptr,
                                &this->m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error(
            "SpriteRenderer: failed to create descriptor pool [" +
            std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]");
    }

    // Allocate the descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = this->m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &this->m_descriptorSetLayout;

    if (vkAllocateDescriptorSets(dev, &allocInfo, &this->m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error(
            "SpriteRenderer: failed to allocate descriptor set [" +
            std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]");
    }

    // Write the atlas texture into the descriptor set
    VkDescriptorImageInfo imageInfo = atlas.texture().getDescriptorInfo();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = this->m_descriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
}

// ============================================================================
// Buffers: unit quad vertex buffer + per-frame instance buffers
// ============================================================================

void SpriteRenderer::createBuffers() {
    const std::array<Vertex2D, 6> quadVertices = {{
        {0.0f, 0.0f,  0.0f, 0.0f},
        {1.0f, 0.0f,  1.0f, 0.0f},
        {1.0f, 1.0f,  1.0f, 1.0f},
        {0.0f, 0.0f,  0.0f, 0.0f},
        {1.0f, 1.0f,  1.0f, 1.0f},
        {0.0f, 1.0f,  0.0f, 1.0f},
    }};

    this->m_vertexBuffer = vkutils::Buffer::create(
        this->m_device,
        sizeof(quadVertices),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (!this->m_vertexBuffer) {
        throw std::runtime_error(
            "SpriteRenderer: failed to create vertex buffer [" +
            std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]");
    }
    this->m_vertexBuffer->copyFrom(quadVertices.data(), sizeof(quadVertices));

    const VkDeviceSize instanceBufferSize = sizeof(SpriteInstanceData) * MAX_SPRITES;
    for (vkutils::BufferPtr& buf : this->m_instanceBuffers) {
        buf = vkutils::Buffer::create(
            this->m_device,
            instanceBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (!buf) {
            throw std::runtime_error(
                "SpriteRenderer: failed to create instance buffer [" +
                std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]");
        }
    }
}

// ============================================================================
// Pipeline creation
// ============================================================================

void SpriteRenderer::createPipeline(VkRenderPass renderPass) {
    const VkDevice dev = this->m_device.handle();

    // Compile sprite shaders from GLSL sources; fall back to pre-compiled SPIR-V
    vkutils::ShaderPtr vertShader = vkutils::Shader::createFromGLSL(
        this->m_device, "shaders/sprite.vert.glsl",
        vkutils::ShaderBuilder::Stage::Vertex);
    if (!vertShader) {
        vertShader = vkutils::Shader::createFromFile(
            this->m_device, "shaders/sprite.vert.spv");
    }

    vkutils::ShaderPtr fragShader = vkutils::Shader::createFromGLSL(
        this->m_device, "shaders/sprite.frag.glsl",
        vkutils::ShaderBuilder::Stage::Fragment);
    if (!fragShader) {
        fragShader = vkutils::Shader::createFromFile(
            this->m_device, "shaders/sprite.frag.spv");
    }

    if (!vertShader || !fragShader) {
        VKLOG_WARN("SpriteRenderer: no shaders available. "
                   "Sprite drawing will be unavailable until shaders are provided.");
        return;
    }

    // Pipeline layout: push constant (camera) + descriptor set 0 (atlas sampler)
    this->m_pipelineLayout = vkutils::PipelineLayoutBuilder(this->m_device)
        .addPushConstant<CameraData>(VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorSetLayout(this->m_descriptorSetLayout)
        .build();
    if (!this->m_pipelineLayout) {
        throw std::runtime_error(
            "SpriteRenderer: failed to create pipeline layout [" +
            std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]");
    }

    // Vertex input: binding 0 = per-vertex (Vertex2D), binding 1 = per-instance (SpriteInstanceData)
    std::array<VkVertexInputBindingDescription, 2> bindings = {};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(Vertex2D);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(SpriteInstanceData);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    // 5 attributes: 2 per-vertex + 3 per-instance (each instance vec4)
    std::array<VkVertexInputAttributeDescription, 5> attrs = {};
    // Per-vertex
    attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, x)};       // inPos
    attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, u)};       // inUV
    // Per-instance
    attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
                offsetof(SpriteInstanceData, posX)};                           // iRect (pos+size)
    attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
                offsetof(SpriteInstanceData, u0)};                             // iAtlasUV
    attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
                offsetof(SpriteInstanceData, r)};                              // iTint

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInputInfo.pVertexBindingDescriptions = bindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Alpha blending (premultiplied-friendly)
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertShader->handle();
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragShader->handle();
    fragStageInfo.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
        vertStageInfo, fragStageInfo};

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = this->m_pipelineLayout->handle();
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo,
                                   nullptr, &this->m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error(
            "SpriteRenderer: failed to create graphics pipeline [" +
            std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]");
    }

    VKLOG_INFO("SpriteRenderer: pipeline created (5 vertex attributes, "
               "%zu-byte sprite instances)",
               sizeof(SpriteInstanceData));
}

// ============================================================================
// Frame control
// ============================================================================

void SpriteRenderer::beginFrame(uint32_t frameIndex) {
    this->m_currentFrameIndex =
        frameIndex % static_cast<uint32_t>(this->m_instanceBuffers.size());
}

void SpriteRenderer::begin() {
    this->m_sprites.clear();
}

void SpriteRenderer::end(VkCommandBuffer cmdBuffer) {
    this->flushBatch(cmdBuffer);
}

void SpriteRenderer::flushBatch(VkCommandBuffer cmdBuffer) {
    if (this->m_sprites.empty() || this->m_pipeline == VK_NULL_HANDLE) return;

    // Viewport culling (same approach as Renderer2D)
    const float invZoom = 1.0f / std::max(this->m_camera.zoom, 0.001f);
    const float viewLeft   = this->m_camera.cameraX;
    const float viewTop    = this->m_camera.cameraY;
    const float viewRight  = viewLeft + this->m_camera.screenWidth * invZoom;
    const float viewBottom = viewTop  + this->m_camera.screenHeight * invZoom;

    this->m_sprites.erase(
        std::remove_if(this->m_sprites.begin(), this->m_sprites.end(),
            [viewLeft, viewTop, viewRight, viewBottom](const SpriteInstanceData& s) {
                return (s.posX + s.sizeX < viewLeft  || s.posX > viewRight ||
                        s.posY + s.sizeY < viewTop   || s.posY > viewBottom);
            }),
        this->m_sprites.end());

    if (this->m_sprites.empty()) return;

    vkutils::BufferPtr& instanceBuffer = this->m_instanceBuffers[this->m_currentFrameIndex];
    const uint32_t totalSprites = static_cast<uint32_t>(this->m_sprites.size());

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->m_pipeline);

    VkViewport viewport{};
    viewport.width = static_cast<float>(this->m_extent.width);
    viewport.height = static_cast<float>(this->m_extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = this->m_extent;
    vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

    vkCmdPushConstants(cmdBuffer, this->m_pipelineLayout->handle(),
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(CameraData), &this->m_camera);

    // Bind the atlas descriptor set
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            this->m_pipelineLayout->handle(),
                            0, 1, &this->m_descriptorSet, 0, nullptr);

    // Bind vertex buffer (unit quad)
    const VkBuffer vertBuffers[] = {this->m_vertexBuffer->handle()};
    const VkDeviceSize vertOffsets[] = {0};
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertBuffers, vertOffsets);

    // Draw in batches of MAX_SPRITES
    uint32_t offset = 0;
    while (offset < totalSprites) {
        const uint32_t batchSize = std::min(MAX_SPRITES, totalSprites - offset);

        instanceBuffer->copyFrom(this->m_sprites.data() + offset,
                                  sizeof(SpriteInstanceData) * batchSize);

        const VkBuffer instBuffers[] = {instanceBuffer->handle()};
        const VkDeviceSize instOffsets[] = {0};
        vkCmdBindVertexBuffers(cmdBuffer, 1, 1, instBuffers, instOffsets);

        vkCmdDraw(cmdBuffer, 6, batchSize, 0, 0);
        offset += batchSize;
    }

    this->m_sprites.clear();
}

// ============================================================================
// Draw API
// ============================================================================

void SpriteRenderer::drawSprite(float x, float y, float w, float h,
                                SpriteRegion region,
                                float r, float g, float b, float a) {
    SpriteInstanceData inst;
    inst.posX = x;
    inst.posY = y;
    inst.sizeX = w;
    inst.sizeY = h;
    inst.u0 = region.u0;
    inst.v0 = region.v0;
    inst.u1 = region.u1;
    inst.v1 = region.v1;
    inst.r = r;
    inst.g = g;
    inst.b = b;
    inst.a = a;
    this->m_sprites.push_back(inst);
}

// ============================================================================
// Camera control
// ============================================================================

void SpriteRenderer::setCamera(float x, float y) {
    this->m_camera.cameraX = x;
    this->m_camera.cameraY = y;
}

void SpriteRenderer::setZoom(float zoom) {
    this->m_camera.zoom = zoom;
}

void SpriteRenderer::setExtent(VkExtent2D extent) {
    this->m_extent = extent;
    this->m_camera.screenWidth = static_cast<float>(extent.width);
    this->m_camera.screenHeight = static_cast<float>(extent.height);
}

void SpriteRenderer::resetCamera() {
    this->m_camera.cameraX = 0.0f;
    this->m_camera.cameraY = 0.0f;
    this->m_camera.zoom = 1.0f;
}

uint32_t SpriteRenderer::getSpriteCount() const {
    return static_cast<uint32_t>(this->m_sprites.size());
}

} // namespace aoc::render
