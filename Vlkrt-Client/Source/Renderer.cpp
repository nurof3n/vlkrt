#include "Renderer.h"
#include "Camera.h"
#include "Scene.h"
#include "ShaderLoader.h"
#include "Utils.h"
#include "FSRUpscaler.h"

#include "Walnut/Application.h"
#include "Walnut/VulkanRayTracing.h"
#include "Walnut/Core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

namespace Vlkrt
{
    namespace
    {
        static auto BytesPerPixel(Walnut::ImageFormat format) -> uint64_t
        {
            switch (format) {
                case Walnut::ImageFormat::RGBA: return 4;
                case Walnut::ImageFormat::RGBA16F: return 8;
                case Walnut::ImageFormat::RGBA32F: return 16;
                case Walnut::ImageFormat::R32F: return 4;
                case Walnut::ImageFormat::RG16F: return 4;
                case Walnut::ImageFormat::R32UI: return 4;
                default: return 0;
            }
        }

        static auto EstimateImageBytes(const std::shared_ptr<Walnut::Image>& image) -> uint64_t
        {
            if (!image) return 0;
            return static_cast<uint64_t>(image->GetWidth()) * static_cast<uint64_t>(image->GetHeight())
                   * BytesPerPixel(image->GetFormat());
        }
    }  // namespace

    Renderer::Renderer()
    {
        m_Device                = Walnut::Application::GetDevice();
        m_RTPipelineProperties  = Walnut::Application::GetRayTracingPipelineProperties();
        m_AccelerationStructure = std::make_unique<AccelerationStructure>();
        m_NRDDenoiser.Initialize(m_Device);

        m_FSRUpscaler = std::make_unique<FSRUpscaler>();
        m_FSRUpscaler->Initialize(m_Device, Walnut::Application::GetPhysicalDevice(),
                Walnut::Application::GetGraphicsQueue(), Walnut::Application::GetGraphicsQueueFamily());
    }

    Renderer::~Renderer()
    {
        m_NRDDenoiser.Shutdown();
        if (m_FSRUpscaler) m_FSRUpscaler->Shutdown();

        if (m_SBTBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_SBTBuffer, nullptr);
            vkFreeMemory(m_Device, m_SBTMemory, nullptr);
        }

        if (m_VertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_VertexBuffer, nullptr);
            vkFreeMemory(m_Device, m_VertexMemory, nullptr);
        }

        if (m_PrevVertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_PrevVertexBuffer, nullptr);
            vkFreeMemory(m_Device, m_PrevVertexMemory, nullptr);
        }

        if (m_IndexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_IndexBuffer, nullptr);
            vkFreeMemory(m_Device, m_IndexMemory, nullptr);
        }

        if (m_MaterialBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_MaterialBuffer, nullptr);
            vkFreeMemory(m_Device, m_MaterialMemory, nullptr);
        }

        if (m_MaterialIndexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_MaterialIndexBuffer, nullptr);
            vkFreeMemory(m_Device, m_MaterialIndexMemory, nullptr);
        }

        if (m_LightBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_LightBuffer, nullptr);
            vkFreeMemory(m_Device, m_LightMemory, nullptr);
        }

        if (m_AABBTransformBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_AABBTransformBuffer, nullptr);
            vkFreeMemory(m_Device, m_AABBTransformMemory, nullptr);
        }

        if (m_PrevAABBTransformBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_PrevAABBTransformBuffer, nullptr);
            vkFreeMemory(m_Device, m_PrevAABBTransformMemory, nullptr);
        }

        if (m_AABBMaterialBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_AABBMaterialBuffer, nullptr);
            vkFreeMemory(m_Device, m_AABBMaterialMemory, nullptr);
        }

        if (m_SceneUBOBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_SceneUBOBuffer, nullptr);
            vkFreeMemory(m_Device, m_SceneUBOMemory, nullptr);
        }

        if (m_QualityMetricsBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_QualityMetricsBuffer, nullptr);
            vkFreeMemory(m_Device, m_QualityMetricsMemory, nullptr);
        }

        if (m_DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);

        if (m_DescriptorSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);

        if (m_RTPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_Device, m_RTPipeline, nullptr);

        if (m_ComposeDenoisedPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(m_Device, m_ComposeDenoisedPipeline, nullptr);

        if (m_RTPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_Device, m_RTPipelineLayout, nullptr);

        if (m_RaygenShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, m_RaygenShader, nullptr);
        if (m_MissShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, m_MissShader, nullptr);
        if (m_ClosestHitShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, m_ClosestHitShader, nullptr);
        if (m_RaygenTemporalShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, m_RaygenTemporalShader, nullptr);
        if (m_ShadowMissShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, m_ShadowMissShader, nullptr);
        if (m_ClosestHitAABBShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, m_ClosestHitAABBShader, nullptr);
        if (m_IntersectAnalyticShader != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_Device, m_IntersectAnalyticShader, nullptr);
        if (m_IntersectSDFShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device, m_IntersectSDFShader, nullptr);
        if (m_ComposeDenoisedShader != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_Device, m_ComposeDenoisedShader, nullptr);

        m_PreviousFrameVertices.clear();
        m_PreviousFrameAABBTransforms.clear();
    }

    void Renderer::OnFSRSettingsChanged(bool enabled, uint32_t qualityMode, float sharpness)
    {
        // Sharpness is set immediately
        m_FSRSharpness = sharpness;
        m_FSRUpscaler->SetSharpness(sharpness);

        // Quality changes require pipeline/resource updates
        bool changed = (m_FSREnabled != enabled) || (m_FSRQuality != qualityMode);
        if (changed) {
            m_FSREnabled = enabled;
            m_FSRQuality = qualityMode;

            if (m_DisplayWidth > 0 && m_DisplayHeight > 0) { OnResize(m_DisplayWidth, m_DisplayHeight); }
        }
    }

    void Renderer::OnResize(uint32_t width, uint32_t height)
    {
        m_DisplayWidth  = width;
        m_DisplayHeight = height;

        if (m_FSREnabled && m_FSRUpscaler) {
            m_FSRUpscaler->OnResize(width, height, static_cast<FSRUpscaler::Quality>(m_FSRQuality), m_FSRSharpness);
            m_RenderWidth  = m_FSRUpscaler->GetRenderWidth();
            m_RenderHeight = m_FSRUpscaler->GetRenderHeight();
        }
        else {
            m_RenderWidth  = width;
            m_RenderHeight = height;
        }

        auto createOrResizeImage
                = [&](std::shared_ptr<Walnut::Image>& img, uint32_t w, uint32_t h, Walnut::ImageFormat fmt) {
                      if (img) {
                          if (img->GetWidth() == w && img->GetHeight() == h) return;
                          img->Resize(w, h);
                      }
                      else {
                          img = std::make_shared<Walnut::Image>(w, h, fmt);
                      }
                  };

        createOrResizeImage(m_FinalImage, m_RenderWidth, m_RenderHeight, Walnut::ImageFormat::RGBA);
        createOrResizeImage(m_AccumImage, m_RenderWidth, m_RenderHeight, Walnut::ImageFormat::RGBA32F);

        createOrResizeImage(m_GuideNormalRoughness, m_RenderWidth, m_RenderHeight, Walnut::ImageFormat::RGBA32F);
        createOrResizeImage(m_GuideViewZ, m_RenderWidth, m_RenderHeight, Walnut::ImageFormat::RGBA16F);
        createOrResizeImage(m_GuideMotionVectors, m_RenderWidth, m_RenderHeight, Walnut::ImageFormat::RGBA16F);
        createOrResizeImage(m_GuideDiffRadianceHitDist, m_RenderWidth, m_RenderHeight, Walnut::ImageFormat::RGBA32F);
        createOrResizeImage(m_GuideSpecRadianceHitDist, m_RenderWidth, m_RenderHeight, Walnut::ImageFormat::RGBA32F);
        createOrResizeImage(m_GuideEmission, m_RenderWidth, m_RenderHeight, Walnut::ImageFormat::RGBA16F);
        createOrResizeImage(m_GuideDepth, m_RenderWidth, m_RenderHeight, Walnut::ImageFormat::R32F);

        if (m_FSREnabled) {
            createOrResizeImage(m_FinalImageUpscaled, m_DisplayWidth, m_DisplayHeight, Walnut::ImageFormat::RGBA);
        }
        else {
            m_FinalImageUpscaled = nullptr;
        }

        // Pre-initialize NRD instance to allocate output images at the render resolution
        m_NRDDenoiser.EnsureDirectInstance(m_RenderWidth, m_RenderHeight);

        // First-time descriptor set creation (layout never changes)
        if (m_DescriptorSetLayout == VK_NULL_HANDLE) CreateDescriptorSets();

        if (m_QualityMetricsBuffer == VK_NULL_HANDLE) {
            m_QualityMetricsBuffer = CreateBuffer(sizeof(uint32_t) * 4,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_QualityMetricsMemory);
        }

        // Update output image bindings
        if (m_DescriptorSet != VK_NULL_HANDLE) {
            VkDescriptorImageInfo outputInfo{};
            outputInfo.imageView   = m_FinalImage->GetVkImageView();
            outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo accumInfo{};
            accumInfo.imageView   = m_AccumImage->GetVkImageView();
            accumInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet writes[12] = {};
            writes[0].sType                 = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet                = m_DescriptorSet;
            writes[0].dstBinding            = 1;
            writes[0].descriptorCount       = 1;
            writes[0].descriptorType        = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[0].pImageInfo            = &outputInfo;

            writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet          = m_DescriptorSet;
            writes[1].dstBinding      = 10;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo      = &accumInfo;

            // Guide buffer descriptors (bindings 12-16)
            VkDescriptorImageInfo guideInfos[5]{};
            guideInfos[0].imageView   = m_GuideNormalRoughness->GetVkImageView();
            guideInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            guideInfos[1].imageView   = m_GuideViewZ->GetVkImageView();
            guideInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            guideInfos[2].imageView   = m_GuideMotionVectors->GetVkImageView();
            guideInfos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            guideInfos[3].imageView   = m_GuideDiffRadianceHitDist->GetVkImageView();
            guideInfos[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            guideInfos[4].imageView   = m_GuideSpecRadianceHitDist->GetVkImageView();
            guideInfos[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            for (int i = 0; i < 5; ++i) {
                writes[2 + i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[2 + i].dstSet          = m_DescriptorSet;
                writes[2 + i].dstBinding      = 12 + i;
                writes[2 + i].descriptorCount = 1;
                writes[2 + i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writes[2 + i].pImageInfo      = &guideInfos[i];
            }

            // OUT buffer descriptors (bindings 19-20)
            VkDescriptorImageInfo outInfos[2]{};
            outInfos[0].imageView   = m_NRDDenoiser.GetOutDiffRadianceHitDist()->GetVkImageView();
            outInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            outInfos[1].imageView   = m_NRDDenoiser.GetOutSpecRadianceHitDist()->GetVkImageView();
            outInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            writes[7].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[7].dstSet          = m_DescriptorSet;
            writes[7].dstBinding      = 19;
            writes[7].descriptorCount = 1;
            writes[7].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[7].pImageInfo      = &outInfos[0];

            writes[8].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[8].dstSet          = m_DescriptorSet;
            writes[8].dstBinding      = 20;
            writes[8].descriptorCount = 1;
            writes[8].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[8].pImageInfo      = &outInfos[1];

            // Guide emission buffer descriptor (binding 21)
            VkDescriptorImageInfo emissionInfo{};
            emissionInfo.imageView   = m_GuideEmission->GetVkImageView();
            emissionInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            writes[9].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[9].dstSet          = m_DescriptorSet;
            writes[9].dstBinding      = 21;
            writes[9].descriptorCount = 1;
            writes[9].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[9].pImageInfo      = &emissionInfo;

            // Guide depth buffer descriptor (binding 22)
            VkDescriptorImageInfo depthInfo{};
            depthInfo.imageView   = m_GuideDepth->GetVkImageView();
            depthInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            writes[10].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[10].dstSet          = m_DescriptorSet;
            writes[10].dstBinding      = 22;
            writes[10].descriptorCount = 1;
            writes[10].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[10].pImageInfo      = &depthInfo;

            // Quality metrics accumulation buffer (binding 23)
            VkDescriptorBufferInfo metricsInfo{};
            metricsInfo.buffer = m_QualityMetricsBuffer;
            metricsInfo.offset = 0;
            metricsInfo.range  = sizeof(uint32_t) * 4;

            writes[11].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[11].dstSet          = m_DescriptorSet;
            writes[11].dstBinding      = 23;
            writes[11].descriptorCount = 1;
            writes[11].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[11].pBufferInfo     = &metricsInfo;

            vkUpdateDescriptorSets(m_Device, 12, writes, 0, nullptr);
        }

        // Update NRD denoiser with the new guide buffers
        m_NRDDenoiser.SetGuideBuffers(m_GuideNormalRoughness, m_GuideViewZ, m_GuideMotionVectors,
                m_GuideDiffRadianceHitDist, m_GuideSpecRadianceHitDist);

        // Force pipeline rebuild on next frame
        m_FirstFrame       = true;
        m_AccumFirstFrame  = true;
        m_GuidesFirstFrame = true;
        m_GuidesInReadOnly = false;
    }

    void Renderer::Render(const Scene& scene, const Camera& camera)
    {
        if (!m_FinalImage || m_FinalImage->GetWidth() == 0 || m_FinalImage->GetHeight() == 0) return;
        if (scene.StaticMeshes.empty() && scene.DynamicMeshes.empty() && scene.ProceduralEntities.empty()) return;

        using Clock    = std::chrono::high_resolution_clock;
        auto msBetween = [](const Clock::time_point& a, const Clock::time_point& b) -> float {
            return std::chrono::duration<float, std::milli>(b - a).count();
        };

        const auto frameStart = Clock::now();
        const auto setupStart = frameStart;

        m_ActiveScene  = &scene;
        m_ActiveCamera = &camera;

        // Rebuild pipeline and SBT if required
        uint32_t proceduralCount = (uint32_t) scene.ProceduralEntities.size();
        if (m_RTPipeline == VK_NULL_HANDLE || proceduralCount != m_LastProceduralCount) {
            DestroyPipelineObjects();
            CreateRayTracingPipeline();
            CreateShaderBindingTable(scene);
            m_LastProceduralCount = proceduralCount;
        }

        // Recalculate current scene metrics if scene is invalidated
        if (!m_SceneValid) {
            size_t totalMeshCount = scene.StaticMeshes.size() + scene.DynamicMeshes.size();
            size_t totalVertices  = 0;
            size_t totalIndices   = 0;
            for (const auto& mesh : scene.StaticMeshes) {
                totalVertices += mesh.Vertices.size();
                totalIndices += mesh.Indices.size();
            }
            for (const auto& mesh : scene.DynamicMeshes) {
                totalVertices += mesh.Vertices.size();
                totalIndices += mesh.Indices.size();
            }
            m_CachedTotalMeshCount = totalMeshCount;
            m_CachedTotalVertices  = totalVertices;
            m_CachedTotalIndices   = totalIndices;
        }

        // Rebuild scene buffers if the scene structure has changed
        bool sizeChanged = (m_CachedTotalMeshCount != m_LastMeshCount) || (m_CachedTotalVertices != m_LastVertexCount)
                           || (m_CachedTotalIndices != m_LastIndexCount)
                           || (scene.Materials.size() != m_LastMaterialCount);
        bool resetBySceneChange = false;
        bool needsRebuild       = !m_SceneValid || sizeChanged;
        if (m_VertexBuffer == VK_NULL_HANDLE || needsRebuild) {
            if (needsRebuild && m_VertexBuffer != VK_NULL_HANDLE) {
                if (m_CachedTotalMeshCount != m_LastMeshCount || m_CachedTotalVertices != m_LastVertexCount
                        || m_CachedTotalIndices != m_LastIndexCount) {
                    Walnut::Application::SubmitResourceFree(
                            [device = m_Device, vbuf = m_VertexBuffer, vmem = m_VertexMemory, ibuf = m_IndexBuffer,
                                    imem = m_IndexMemory, mibuf = m_MaterialIndexBuffer,
                                    mimem = m_MaterialIndexMemory]() {
                                if (vbuf) vkDestroyBuffer(device, vbuf, nullptr);
                                if (vmem) vkFreeMemory(device, vmem, nullptr);
                                if (ibuf) vkDestroyBuffer(device, ibuf, nullptr);
                                if (imem) vkFreeMemory(device, imem, nullptr);
                                if (mibuf) vkDestroyBuffer(device, mibuf, nullptr);
                                if (mimem) vkFreeMemory(device, mimem, nullptr);
                            });
                    m_VertexBuffer        = VK_NULL_HANDLE;
                    m_IndexBuffer         = VK_NULL_HANDLE;
                    m_MaterialIndexBuffer = VK_NULL_HANDLE;
                }
                if (scene.Materials.size() != m_LastMaterialCount) {
                    Walnut::Application::SubmitResourceFree(
                            [device = m_Device, buf = m_MaterialBuffer, mem = m_MaterialMemory]() {
                                if (buf) vkDestroyBuffer(device, buf, nullptr);
                                if (mem) vkFreeMemory(device, mem, nullptr);
                            });
                    m_MaterialBuffer = VK_NULL_HANDLE;
                }
                if (scene.Lights.size() != m_LastLightCount) {
                    Walnut::Application::SubmitResourceFree(
                            [device = m_Device, buf = m_LightBuffer, mem = m_LightMemory]() {
                                if (buf) vkDestroyBuffer(device, buf, nullptr);
                                if (mem) vkFreeMemory(device, mem, nullptr);
                            });
                    m_LightBuffer     = VK_NULL_HANDLE;
                    m_LightMemory     = VK_NULL_HANDLE;
                    size_t lightCount = std::max(scene.Lights.size(), (size_t) 1);
                    m_LightBufferSize = sizeof(GPULight) * lightCount;
                    m_LightBuffer     = CreateBuffer(m_LightBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_LightMemory);
                }
            }

            if (m_VertexBuffer == VK_NULL_HANDLE) CreateSceneBuffers(scene);
            UpdateSceneData(scene);

            m_LastMeshCount     = m_CachedTotalMeshCount;
            m_LastVertexCount   = m_CachedTotalVertices;
            m_LastIndexCount    = m_CachedTotalIndices;
            m_LastMaterialCount = scene.Materials.size();
            m_LastLightCount    = scene.Lights.size();
            m_SceneValid        = true;

            if (sizeChanged) {
                m_AccumFirstFrame  = true;
                m_FrameIndex       = 0;
                resetBySceneChange = true;
            }
        }

        if (!m_AccelerationStructure->IsBuilt()) return;

        // Reset temporal accumulation when camera moves (epsilon compare to avoid float noise resets)
        const glm::mat4 currentView = camera.GetView();
        auto viewChanged            = [](const glm::mat4& a, const glm::mat4& b) {
            constexpr float kEpsilon = 1e-6f;
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    if (std::abs(a[c][r] - b[c][r]) > kEpsilon) return true;
                }
            }
            return false;
        };

        const bool resetByCameraView = viewChanged(currentView, m_LastCameraView);
        const bool allowTemporalCameraReuse
                = scene.EnableNRDDenoiser && (scene.RaytracingType == RaytracingMode::PathTracing);
        if (resetByCameraView && !allowTemporalCameraReuse) {
            m_FrameIndex      = 0;
            m_AccumFirstFrame = true;
        }

        const auto setupEnd = Clock::now();

        // Upload SceneUBO
        const auto uboStart = Clock::now();
        UpdateSceneUBO(scene, camera);
        const auto uboEnd = Clock::now();

        VkCommandBuffer cmd = Walnut::Application::GetCommandBuffer(true);

        if (m_QualityMetricsBuffer != VK_NULL_HANDLE) {
            vkCmdFillBuffer(cmd, m_QualityMetricsBuffer, 0, sizeof(uint32_t) * 4, 0);

            VkBufferMemoryBarrier metricsResetBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
            metricsResetBarrier.srcAccessMask         = VK_ACCESS_TRANSFER_WRITE_BIT;
            metricsResetBarrier.dstAccessMask         = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            metricsResetBarrier.srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
            metricsResetBarrier.dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
            metricsResetBarrier.buffer                = m_QualityMetricsBuffer;
            metricsResetBarrier.offset                = 0;
            metricsResetBarrier.size                  = sizeof(uint32_t) * 4;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                    nullptr, 1, &metricsResetBarrier, 0, nullptr);
        }

        // Transition final image to GENERAL layout for shader write
        {
            VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.image                = m_FinalImage->GetVkImage();
            barrier.oldLayout     = m_FirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcAccessMask = m_FirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        // Transition accum image every frame:
        //   first frame after reset: UNDEFINED→GENERAL (discard old content, make visible for write)
        //   subsequent frames: GENERAL→GENERAL (make previous frame's stores visible to this frame's loads)
        {
            VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.image                = m_AccumImage->GetVkImage();
            barrier.oldLayout            = m_AccumFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout            = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcAccessMask        = m_AccumFirstFrame ? 0 : VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;
            VkPipelineStageFlags srcStage           = m_AccumFirstFrame ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                                                        : VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
            vkCmdPipelineBarrier(cmd, srcStage, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0, nullptr, 0, nullptr,
                    1, &barrier);
            m_AccumFirstFrame = false;
        }

        // Transition guide images to GENERAL for RT shader writes
        {
            std::shared_ptr<Walnut::Image> guideImages[] = {
                m_GuideNormalRoughness,
                m_GuideViewZ,
                m_GuideMotionVectors,
                m_GuideDiffRadianceHitDist,
                m_GuideSpecRadianceHitDist,
                m_GuideEmission,
                m_FSREnabled ? m_GuideDepth : nullptr,
            };

            VkImageMemoryBarrier barriers[7]{};
            uint32_t barrierCount = 0;
            for (const auto& guideImage : guideImages) {
                if (!guideImage) continue;

                VkImageMemoryBarrier& b = barriers[barrierCount++];
                b                       = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                b.image                 = guideImage->GetVkImage();
                b.oldLayout     = m_GuidesFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED
                                                     : (m_GuidesInReadOnly ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                                           : VK_IMAGE_LAYOUT_GENERAL);
                b.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
                b.srcAccessMask = m_GuidesFirstFrame ? 0
                                                     : (m_GuidesInReadOnly ? VK_ACCESS_SHADER_READ_BIT
                                                                           : VK_ACCESS_SHADER_WRITE_BIT);
                b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                b.subresourceRange.baseMipLevel   = 0;
                b.subresourceRange.levelCount     = 1;
                b.subresourceRange.baseArrayLayer = 0;
                b.subresourceRange.layerCount     = 1;
            }

            if (barrierCount > 0) {
                // On the very first frame only TOP_OF_PIPE is needed (no prior writes)
                VkPipelineStageFlags srcStage
                        = m_GuidesFirstFrame ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                             : (m_GuidesInReadOnly ? (VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
                                                                             | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                                                             | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
                                                                   : VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
                vkCmdPipelineBarrier(cmd, srcStage, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0, nullptr, 0,
                        nullptr, barrierCount, barriers);
            }

            m_GuidesFirstFrame = false;
            m_GuidesInReadOnly = false;
        }

        // Bind pipeline and descriptor set
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_RTPipeline);
        vkCmdBindDescriptorSets(
                cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_RTPipelineLayout, 0, 1, &m_DescriptorSet, 0, nullptr);

        // Select raygen SBT entry: 0=PathTracing, 1=PathTracingTemporal
        const uint32_t raygenIdx = (scene.RaytracingType == RaytracingMode::PathTracingTemporal) ? 1u : 0u;
        VkStridedDeviceAddressRegionKHR activeRaygen = m_RaygenRegion;
        activeRaygen.deviceAddress += raygenIdx * m_RaygenRegion.stride;
        activeRaygen.size = m_RaygenRegion.stride;

        // Push constants: hot scene parameters
        struct PushConstantsData
        {
            uint32_t numLights;
            uint32_t onlyOneLightSample;
            uint32_t russianRouletteDepth;
            uint32_t sceneIndex;
        } pc{};
        pc.numLights            = static_cast<uint32_t>(scene.Lights.size());
        pc.onlyOneLightSample   = scene.OnlyOneLightSample ? 1u : 0u;
        pc.russianRouletteDepth = scene.RussianRouletteDepth;
        pc.sceneIndex           = static_cast<uint32_t>(scene.SceneIndex);

        vkCmdPushConstants(cmd, m_RTPipelineLayout,
                VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR
                        | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
                0, sizeof(pc), &pc);

        const auto rtRecordStart = Clock::now();
        pvkCmdTraceRaysKHR(cmd, &activeRaygen, &m_MissRegion, &m_HitRegion, &m_CallableRegion, m_FinalImage->GetWidth(),
                m_FinalImage->GetHeight(), 1);
        const auto rtRecordEnd = Clock::now();

        m_NRDDenoiser.SetEnabled(scene.EnableNRDDenoiser);
        float nrdRecordMs = 0.0f;

        // Sync RT shader writes before NRD compute dispatches
        if (scene.EnableNRDDenoiser) {
            {
                VkMemoryBarrier memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                memoryBarrier.srcAccessMask   = VK_ACCESS_SHADER_WRITE_BIT;
                memoryBarrier.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
            }

            NRDDenoiseParams denoiseParams{};
            denoiseParams.FrameIndex = m_FrameIndex;
            denoiseParams.Width      = m_FinalImage->GetWidth();
            denoiseParams.Height     = m_FinalImage->GetHeight();

            const glm::mat4 viewToClip      = camera.GetProjection();
            const glm::mat4 worldToView     = camera.GetView();
            const glm::mat4 viewToClipPrev  = (m_FirstFrame) ? viewToClip : m_LastCameraProjection;
            const glm::mat4 worldToViewPrev = (m_FirstFrame) ? worldToView : m_LastCameraView;

            std::memcpy(denoiseParams.ViewToClip, glm::value_ptr(viewToClip), sizeof(denoiseParams.ViewToClip));
            std::memcpy(
                    denoiseParams.ViewToClipPrev, glm::value_ptr(viewToClipPrev), sizeof(denoiseParams.ViewToClipPrev));
            std::memcpy(denoiseParams.WorldToView, glm::value_ptr(worldToView), sizeof(denoiseParams.WorldToView));
            std::memcpy(denoiseParams.WorldToViewPrev, glm::value_ptr(worldToViewPrev),
                    sizeof(denoiseParams.WorldToViewPrev));

            denoiseParams.CameraJitter[0]     = m_LastCameraJitter.x;
            denoiseParams.CameraJitter[1]     = m_LastCameraJitter.y;
            denoiseParams.CameraJitterPrev[0] = m_PrevCameraJitter.x;
            denoiseParams.CameraJitterPrev[1] = m_PrevCameraJitter.y;

            denoiseParams.MinMaterialForDiffuse              = scene.NRDMinMaterialForDiffuse;
            denoiseParams.MinMaterialForSpecular             = scene.NRDMinMaterialForSpecular;
            denoiseParams.DiffusePrepassBlurRadius           = scene.NRDDiffusePrepassBlurRadius;
            denoiseParams.SpecularPrepassBlurRadius          = scene.NRDSpecularPrepassBlurRadius;
            denoiseParams.DiffuseMaxAccumulatedFrameNum      = scene.NRDDiffuseMaxAccumulatedFrameNum;
            denoiseParams.SpecularMaxAccumulatedFrameNum     = scene.NRDSpecularMaxAccumulatedFrameNum;
            denoiseParams.DiffuseMaxFastAccumulatedFrameNum  = scene.NRDDiffuseMaxFastAccumulatedFrameNum;
            denoiseParams.SpecularMaxFastAccumulatedFrameNum = scene.NRDSpecularMaxFastAccumulatedFrameNum;
            denoiseParams.AntilagAccelerationAmount          = scene.NRDAntilagAccelerationAmount;
            denoiseParams.AntilagSpatialSigmaScale           = scene.NRDAntilagSpatialSigmaScale;
            denoiseParams.AntilagTemporalSigmaScale          = scene.NRDAntilagTemporalSigmaScale;
            denoiseParams.AntilagResetAmount                 = scene.NRDAntilagResetAmount;
            denoiseParams.DisocclusionThreshold              = scene.NRDDisocclusionThreshold;
            denoiseParams.DisocclusionThresholdAlternate     = scene.NRDDisocclusionThresholdAlternate;
            denoiseParams.HasValidMatrices                   = true;

            const auto nrdRecordStart = Clock::now();
            m_NRDDenoiser.Dispatch(cmd, m_FinalImage, denoiseParams);

            if (m_ComposeDenoisedPipeline != VK_NULL_HANDLE) {
                // Ensure NRD compute dispatches finish before our custom Compose compute shader runs
                VkMemoryBarrier composeBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                composeBarrier.srcAccessMask   = VK_ACCESS_SHADER_WRITE_BIT;
                composeBarrier.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                        1, &composeBarrier, 0, nullptr, 0, nullptr);

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComposeDenoisedPipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipelineLayout, 0, 1,
                        &m_DescriptorSet, 0, nullptr);
                const uint32_t groupX = (m_FinalImage->GetWidth() + 7u) / 8u;
                const uint32_t groupY = (m_FinalImage->GetHeight() + 7u) / 8u;
                vkCmdDispatch(cmd, groupX, groupY, 1);

                if (m_QualityMetricsBuffer != VK_NULL_HANDLE) {
                    VkBufferMemoryBarrier metricsReadbackBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
                    metricsReadbackBarrier.srcAccessMask         = VK_ACCESS_SHADER_WRITE_BIT;
                    metricsReadbackBarrier.dstAccessMask         = VK_ACCESS_HOST_READ_BIT;
                    metricsReadbackBarrier.srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
                    metricsReadbackBarrier.dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
                    metricsReadbackBarrier.buffer                = m_QualityMetricsBuffer;
                    metricsReadbackBarrier.offset                = 0;
                    metricsReadbackBarrier.size                  = sizeof(uint32_t) * 4;

                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                            nullptr, 1, &metricsReadbackBarrier, 0, nullptr);
                }
            }

            const auto nrdRecordEnd = Clock::now();
            nrdRecordMs             = msBetween(nrdRecordStart, nrdRecordEnd);
        }

        // Transition final image back to SHADER_READ_ONLY for ImGui rendering
        {
            VkImageMemoryBarrier barrier            = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.image                           = m_FinalImage->GetVkImage();
            barrier.oldLayout                       = VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask                   = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
            barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;
            vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        // FSR upscale pass
        if (m_FSREnabled && m_FSRUpscaler && m_FinalImageUpscaled) {
            {
                VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                barrier.image                = m_FinalImageUpscaled->GetVkImage();
                barrier.oldLayout = m_FirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.srcAccessMask                   = m_FirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT;
                barrier.dstAccessMask                   = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel   = 0;
                barrier.subresourceRange.levelCount     = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount     = 1;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            m_FSRUpscaler->Dispatch(cmd, m_FinalImage, m_GuideDepth, m_GuideMotionVectors, m_FinalImageUpscaled,
                    m_LastCameraJitter, m_LastPassStats.FrameTotalMs, m_FirstFrame, camera.GetNearClip(),
                    camera.GetFarClip(), glm::radians(camera.GetFOV()));

            {
                VkImageMemoryBarrier barrier            = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                barrier.image                           = m_FinalImageUpscaled->GetVkImage();
                barrier.oldLayout                       = VK_IMAGE_LAYOUT_GENERAL;
                barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask                   = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
                barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel   = 0;
                barrier.subresourceRange.levelCount     = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount     = 1;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
        }

        // Transition only the active debug guide for ImGui sampling.
        // In FinalImage mode, skip guide readback transitions entirely.
        if (scene.NRDGuideDebugView != NRDGuideDebugViewMode::FinalImage) {
            std::shared_ptr<Walnut::Image> debugGuide;
            switch (scene.NRDGuideDebugView) {
                case NRDGuideDebugViewMode::NormalRoughness: debugGuide = m_GuideNormalRoughness; break;
                case NRDGuideDebugViewMode::ViewZ: debugGuide = m_GuideViewZ; break;
                case NRDGuideDebugViewMode::MotionVectors: debugGuide = m_GuideMotionVectors; break;
                case NRDGuideDebugViewMode::DiffRadianceHitDist: debugGuide = m_GuideDiffRadianceHitDist; break;
                case NRDGuideDebugViewMode::SpecRadianceHitDist: debugGuide = m_GuideSpecRadianceHitDist; break;
                default: break;
            }

            if (debugGuide) {
                VkImageMemoryBarrier barrier            = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                barrier.image                           = debugGuide->GetVkImage();
                barrier.oldLayout                       = VK_IMAGE_LAYOUT_GENERAL;
                barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask                   = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
                barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel   = 0;
                barrier.subresourceRange.levelCount     = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount     = 1;

                vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                m_GuidesInReadOnly = true;
            }
        }

        const auto submitStart = Clock::now();
        Walnut::Application::FlushCommandBuffer(cmd);
        const auto submitEnd = Clock::now();

        if (scene.EnableNRDDenoiser && m_QualityMetricsMemory != VK_NULL_HANDLE) {
            uint32_t* metricsData = nullptr;
            vkMapMemory(m_Device, m_QualityMetricsMemory, 0, sizeof(uint32_t) * 4, 0, (void**) &metricsData);

            const uint32_t sumSquaredScaled = metricsData ? metricsData[0] : 0u;
            const uint32_t sumAbsScaled     = metricsData ? metricsData[1] : 0u;
            const uint32_t count            = metricsData ? metricsData[2] : 0u;

            if (metricsData) vkUnmapMemory(m_Device, m_QualityMetricsMemory);

            constexpr float kMetricScale = 256.0f;
            if (count > 0) {
                const float mse  = (sumSquaredScaled / kMetricScale) / (float) count;
                const float rmse = std::sqrt(std::max(mse, 0.0f));
                const float mad  = (sumAbsScaled / kMetricScale) / (float) count;

                m_LastDenoiseMetrics.Valid           = true;
                m_LastDenoiseMetrics.SampleCount     = count;
                m_LastDenoiseMetrics.LumaMSE         = mse;
                m_LastDenoiseMetrics.LumaRMSE        = rmse;
                m_LastDenoiseMetrics.LumaMeanAbsDiff = mad;
                m_LastDenoiseMetrics.LumaPSNR        = (mse > 1e-8f) ? (10.0f * std::log10(1.0f / mse)) : 99.0f;
            }
            else {
                m_LastDenoiseMetrics = {};
            }
        }
        else {
            m_LastDenoiseMetrics = {};
        }

        m_LastCameraProjection = camera.GetProjection();
        m_LastCameraView       = camera.GetView();


        const auto frameEnd              = Clock::now();
        m_LastPassStats.SceneSetupMs     = msBetween(setupStart, setupEnd);
        m_LastPassStats.UBOUploadMs      = msBetween(uboStart, uboEnd);
        m_LastPassStats.RayTraceRecordMs = msBetween(rtRecordStart, rtRecordEnd);
        m_LastPassStats.NRDRecordMs      = nrdRecordMs;
        m_LastPassStats.CommandSubmitMs  = msBetween(submitStart, submitEnd);
        m_LastPassStats.FrameTotalMs     = msBetween(frameStart, frameEnd);
        m_LastPassStats.NRDEnabled       = scene.EnableNRDDenoiser;
        m_LastPassStats.NRDOperational   = m_NRDDenoiser.IsOperational();
        m_LastPassStats.Width            = m_FinalImage->GetWidth();
        m_LastPassStats.Height           = m_FinalImage->GetHeight();

        uint64_t estimatedBytes = 0;
        estimatedBytes += static_cast<uint64_t>(m_VertexBufferSize);
        estimatedBytes += static_cast<uint64_t>(m_PrevVertexBufferSize);
        estimatedBytes += static_cast<uint64_t>(m_IndexBufferSize);
        estimatedBytes += static_cast<uint64_t>(m_MaterialBufferSize);
        estimatedBytes += static_cast<uint64_t>(m_MaterialIndexBufferSize);
        estimatedBytes += static_cast<uint64_t>(m_LightBufferSize);
        estimatedBytes += static_cast<uint64_t>(m_AABBTransformBufferSize);
        estimatedBytes += static_cast<uint64_t>(m_PrevAABBTransformBufferSize);
        estimatedBytes += static_cast<uint64_t>(m_AABBMaterialBufferSize);
        estimatedBytes += static_cast<uint64_t>(sizeof(SceneUBOData));
        estimatedBytes += static_cast<uint64_t>(sizeof(uint32_t) * 4);  // quality metrics buffer
        estimatedBytes += static_cast<uint64_t>(m_SBTBufferSize);

        estimatedBytes += EstimateImageBytes(m_FinalImage);
        estimatedBytes += EstimateImageBytes(m_AccumImage);
        estimatedBytes += EstimateImageBytes(m_GuideNormalRoughness);
        estimatedBytes += EstimateImageBytes(m_GuideViewZ);
        estimatedBytes += EstimateImageBytes(m_GuideMotionVectors);
        estimatedBytes += EstimateImageBytes(m_GuideDiffRadianceHitDist);
        estimatedBytes += EstimateImageBytes(m_GuideSpecRadianceHitDist);
        estimatedBytes += EstimateImageBytes(m_GuideEmission);
        estimatedBytes += EstimateImageBytes(m_GuideDepth);
        estimatedBytes += EstimateImageBytes(m_FinalImageUpscaled);
        estimatedBytes += EstimateImageBytes(m_NRDDenoiser.GetOutDiffRadianceHitDist());
        estimatedBytes += EstimateImageBytes(m_NRDDenoiser.GetOutSpecRadianceHitDist());

        for (const auto& [_, tex] : m_TextureCache) { estimatedBytes += EstimateImageBytes(tex); }

        m_LastPassStats.EstimatedGraphicsMemoryMB = static_cast<float>(estimatedBytes / (1024.0 * 1024.0));

        m_FirstFrame = false;
        ++m_FrameIndex;
    }

    void Renderer::DestroyPipelineObjects()
    {
        if (m_RTPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_Device, m_RTPipeline, nullptr);
            m_RTPipeline = VK_NULL_HANDLE;
        }
        if (m_ComposeDenoisedPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_Device, m_ComposeDenoisedPipeline, nullptr);
            m_ComposeDenoisedPipeline = VK_NULL_HANDLE;
        }
        if (m_RTPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_Device, m_RTPipelineLayout, nullptr);
            if (m_ComputePipelineLayout == m_RTPipelineLayout) { m_ComputePipelineLayout = VK_NULL_HANDLE; }
            m_RTPipelineLayout = VK_NULL_HANDLE;
        }
        if (m_ComputePipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_Device, m_ComputePipelineLayout, nullptr);
            m_ComputePipelineLayout = VK_NULL_HANDLE;
        }
        if (m_SBTBuffer != VK_NULL_HANDLE) {
            m_SBTBufferSize = 0;
            vkDestroyBuffer(m_Device, m_SBTBuffer, nullptr);
            vkFreeMemory(m_Device, m_SBTMemory, nullptr);
            m_SBTBuffer = VK_NULL_HANDLE;
            m_SBTMemory = VK_NULL_HANDLE;
        }
    }

    static float Halton(int32_t index, int32_t base)
    {
        float f      = 1.0f;
        float r      = 0.0f;
        int32_t curr = index;
        while (curr > 0) {
            f    = f / (float) base;
            r    = r + f * (float) (curr % base);
            curr = curr / base;
        }
        return r;
    }

    void Renderer::UpdateSceneUBO(const Scene& scene, const Camera& camera)
    {
        ++m_GlobalTick;
        SceneUBOData ubo{};

        m_PrevCameraJitter = m_LastCameraJitter;

        glm::mat4 projection = camera.GetProjection();
        float jitterX        = 0.0f;
        float jitterY        = 0.0f;

        if (m_FSREnabled && m_FSRUpscaler) {
            // Calculate jitter phase count using FSR formula: 8.0 * (display_width / render_width)^2
            float ratio        = (float) m_DisplayWidth / (float) m_RenderWidth;
            int32_t phaseCount = int32_t(8.0f * ratio * ratio);
            if (phaseCount < 1) phaseCount = 1;

            jitterX = Halton((m_FrameIndex % phaseCount) + 1, 2) - 0.5f;
            jitterY = Halton((m_FrameIndex % phaseCount) + 1, 3) - 0.5f;

            // Apply jitter to projection matrix (unit pixels -> NDC translation)
            float ndcOffsetX = 2.0f * jitterX / (float) m_RenderWidth;
            float ndcOffsetY = -2.0f * jitterY / (float) m_RenderHeight;
            projection[2][0] += ndcOffsetX;
            projection[2][1] += ndcOffsetY;

            m_LastCameraJitter = glm::vec2(jitterX, jitterY);
        }
        else {
            m_LastCameraJitter = glm::vec2(0.0f, 0.0f);
        }

        const glm::mat4 worldToClipJittered = projection * camera.GetView();

        // Compute unjittered clip space matrices for jitter-free motion vectors on the GPU
        const glm::mat4 worldToClipUnjittered = camera.GetProjection() * camera.GetView();
        const glm::mat4 worldToClipPrevUnjittered
                = (m_FirstFrame) ? worldToClipUnjittered : (m_LastCameraProjection * m_LastCameraView);

        // projectionToWorld uses the jittered projection to generate jittered camera rays
        ubo.projectionToWorld = glm::inverse(worldToClipJittered);

        // worldToClip and worldToClipPrev use the unjittered projection for jitter-free motion vectors
        ubo.worldToClip     = worldToClipUnjittered;
        ubo.worldToClipPrev = worldToClipPrevUnjittered;

        ubo.cameraPosition          = glm::vec4(camera.GetPosition(), 1.0f);
        ubo.backgroundColor         = glm::vec4(scene.BackgroundColor, 1.0f);
        ubo.numLights               = (uint32_t) scene.Lights.size();
        ubo.elapsedTime             = m_ElapsedTime;
        ubo.elapsedTicks            = m_GlobalTick;
        ubo.raytracingType          = (uint32_t) scene.RaytracingType;
        ubo.importanceSamplingType  = (uint32_t) scene.ImportanceSampling;
        ubo.maxRecursionDepth       = scene.MaxRecursionDepth;
        ubo.maxShadowRecursionDepth = scene.MaxShadowRecursionDepth;
        ubo.pathSqrtSamplesPerPixel = scene.PathSqrtSamplesPerPixel;
        ubo.pathFrameCacheIndex     = m_FrameIndex + 1;

        // Disable random per-pixel/per-sample jitter when FSR is active to prevent noise clash
        ubo.applyJitter = (scene.ApplyJitter && !m_FSREnabled) ? 1u : 0u;

        ubo.onlyOneLightSample   = scene.OnlyOneLightSample ? 1u : 0u;
        ubo.russianRouletteDepth = scene.RussianRouletteDepth;
        ubo.anisotropicBSDF      = scene.AnisotropicBSDF ? 1u : 0u;
        ubo.sceneIndex           = scene.SceneIndex;
        glm::vec3 camDir         = camera.GetDirection();
        ubo.cameraForward[0]     = camDir.x;
        ubo.cameraForward[1]     = camDir.y;
        ubo.cameraForward[2]     = camDir.z;
        ubo.nrdDebugViewMode     = (uint32_t) scene.NRDGuideDebugView;
        ubo._pad[0]              = scene.EnableNRDDenoiser ? 1.0f : 0.0f;
        ubo._pad[1]              = m_FSREnabled ? 1.0f : 0.0f;

        void* data;
        vkMapMemory(m_Device, m_SceneUBOMemory, 0, sizeof(SceneUBOData), 0, &data);
        memcpy(data, &ubo, sizeof(SceneUBOData));
        vkUnmapMemory(m_Device, m_SceneUBOMemory);

        m_ElapsedTime += 1.0f / 60.0f;
    }

    void Renderer::CreateRayTracingPipeline()
    {
        // Load all shader modules
        auto loadModule = [&](const char* spvName, VkShaderModule& mod) {
            auto code = ShaderLoader::LoadShaderBytecode(spvName);
            mod       = ShaderLoader::CreateShaderModule(m_Device, code);
        };
        loadModule("raygen.rgen.spv", m_RaygenShader);
        loadModule("raygen_temporal.rgen.spv", m_RaygenTemporalShader);
        loadModule("miss.rmiss.spv", m_MissShader);
        loadModule("shadow_miss.rmiss.spv", m_ShadowMissShader);
        loadModule("closesthit.rchit.spv", m_ClosestHitShader);
        loadModule("closesthit_aabb.rchit.spv", m_ClosestHitAABBShader);
        loadModule("intersect_analytic.rint.spv", m_IntersectAnalyticShader);
        loadModule("intersect_sdf.rint.spv", m_IntersectSDFShader);
        loadModule("compose_denoised.comp.spv", m_ComposeDenoisedShader);

        // Stage index constants
        constexpr uint32_t IDX_RGEN      = 0;
        constexpr uint32_t IDX_RGEN_TMP  = 1;
        constexpr uint32_t IDX_MISS      = 2;
        constexpr uint32_t IDX_MISS_SHD  = 3;
        constexpr uint32_t IDX_CHIT      = 4;
        constexpr uint32_t IDX_CHIT_AABB = 5;
        constexpr uint32_t IDX_RINT_ANLY = 6;
        constexpr uint32_t IDX_RINT_SDF  = 7;

        VkPipelineShaderStageCreateInfo stages[8] = {};
        auto makeStage = [](VkShaderStageFlagBits stg, VkShaderModule mod, const char* entry) {
            VkPipelineShaderStageCreateInfo s = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            s.stage                           = stg;
            s.module                          = mod;
            s.pName                           = entry;
            return s;
        };

        const uint32_t N           = m_ActiveScene ? (uint32_t) m_ActiveScene->ProceduralEntities.size() : 0u;
        const uint32_t totalGroups = 6u + N * 2u;

        std::vector<VkRayTracingShaderGroupCreateInfoKHR> grps(totalGroups);
        const uint32_t U = VK_SHADER_UNUSED_KHR;
        auto fillGrp     = [&](uint32_t idx, VkRayTracingShaderGroupTypeKHR type, uint32_t general, uint32_t chit,
                                   uint32_t rint) {
            grps[idx]                    = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
            grps[idx].type               = type;
            grps[idx].generalShader      = general;
            grps[idx].closestHitShader   = chit;
            grps[idx].anyHitShader       = U;
            grps[idx].intersectionShader = rint;
        };
        fillGrp(0, VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, IDX_RGEN, U, U);
        fillGrp(1, VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, IDX_RGEN_TMP, U, U);
        fillGrp(2, VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, IDX_MISS, U, U);
        fillGrp(3, VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, IDX_MISS_SHD, U, U);
        fillGrp(4, VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR, U, IDX_CHIT, U);
        fillGrp(5, VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR, U, U, U);
        for (uint32_t i = 0; i < N; ++i) {
            const uint32_t rint = m_ActiveScene->ProceduralEntities[i].IsAnalytic ? IDX_RINT_ANLY : IDX_RINT_SDF;
            fillGrp(6 + i * 2, VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR, U, IDX_CHIT_AABB, rint);
            fillGrp(6 + i * 2 + 1, VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR, U, U, rint);
        }

        // Create RT pipeline layout
        VkPipelineLayoutCreateInfo layoutCI = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutCI.setLayoutCount             = 1;
        layoutCI.pSetLayouts                = &m_DescriptorSetLayout;

        // Push constants: 16 bytes for hot scene parameters
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR
                             | VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                             | VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
        pcRange.offset     = 0;
        pcRange.size       = 16;  // sizeof(numLights + onlyOneLightSample + russianRouletteDepth + sceneIndex)

        layoutCI.pushConstantRangeCount = 1;
        layoutCI.pPushConstantRanges    = &pcRange;
        vkCreatePipelineLayout(m_Device, &layoutCI, nullptr, &m_RTPipelineLayout);

        // Reuse the same layout for compute
        m_ComputePipelineLayout = m_RTPipelineLayout;

        auto tryCreatePipeline = [&](const char* const entryNames[8]) -> VkResult {
            stages[IDX_RGEN] = makeStage(VK_SHADER_STAGE_RAYGEN_BIT_KHR, m_RaygenShader, entryNames[IDX_RGEN]);
            stages[IDX_RGEN_TMP]
                    = makeStage(VK_SHADER_STAGE_RAYGEN_BIT_KHR, m_RaygenTemporalShader, entryNames[IDX_RGEN_TMP]);
            stages[IDX_MISS] = makeStage(VK_SHADER_STAGE_MISS_BIT_KHR, m_MissShader, entryNames[IDX_MISS]);
            stages[IDX_MISS_SHD]
                    = makeStage(VK_SHADER_STAGE_MISS_BIT_KHR, m_ShadowMissShader, entryNames[IDX_MISS_SHD]);
            stages[IDX_CHIT] = makeStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, m_ClosestHitShader, entryNames[IDX_CHIT]);
            stages[IDX_CHIT_AABB]
                    = makeStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, m_ClosestHitAABBShader, entryNames[IDX_CHIT_AABB]);
            stages[IDX_RINT_ANLY] = makeStage(
                    VK_SHADER_STAGE_INTERSECTION_BIT_KHR, m_IntersectAnalyticShader, entryNames[IDX_RINT_ANLY]);
            stages[IDX_RINT_SDF]
                    = makeStage(VK_SHADER_STAGE_INTERSECTION_BIT_KHR, m_IntersectSDFShader, entryNames[IDX_RINT_SDF]);

            // RayPayload struct: 7×float4 (112B) + 3×uint+2×float (20B) + NRD fields ~48B = 180B total
            VkRayTracingPipelineInterfaceCreateInfoKHR interfaceConfig
                    = { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_INTERFACE_CREATE_INFO_KHR };
            interfaceConfig.maxPipelineRayPayloadSize      = 180;
            interfaceConfig.maxPipelineRayHitAttributeSize = 32;

            VkRayTracingPipelineCreateInfoKHR pci = { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
            pci.stageCount                        = 8;
            pci.pStages                           = stages;
            pci.groupCount                        = totalGroups;
            pci.pGroups                           = grps.data();
            // Keep pipeline recursion fixed to device capability to avoid pipeline+SBT rebuilds
            // when users tweak the global recursion setting from UI.
            pci.maxPipelineRayRecursionDepth = std::min(m_RTPipelineProperties.maxRayRecursionDepth, 16u);
            pci.layout                       = m_RTPipelineLayout;
            pci.pLibraryInterface            = &interfaceConfig;
            return pvkCreateRayTracingPipelinesKHR(
                    m_Device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pci, nullptr, &m_RTPipeline);
        };

        // Slang compiler exports all entry points as "main" in the SPIR-V module,
        // regardless of the function name in the source code.
        const char* entryPoints[8] = { "main", "main", "main", "main", "main", "main", "main", "main" };

        VkResult pipelineRes = tryCreatePipeline(entryPoints);
        if (pipelineRes != VK_SUCCESS || m_RTPipeline == VK_NULL_HANDLE) {
            if (m_RTPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(m_Device, m_RTPipeline, nullptr);
                m_RTPipeline = VK_NULL_HANDLE;
            }
            throw std::runtime_error(
                    "Failed to create ray tracing pipeline. Result code: " + std::to_string((int) pipelineRes));
        }
        WL_INFO_TAG("Renderer", "Ray tracing pipeline created successfully (handle={}).", (void*) m_RTPipeline);

        // Create compose compute pipeline for NRD output composition
        VkPipelineShaderStageCreateInfo stageInfo
                = makeStage(VK_SHADER_STAGE_COMPUTE_BIT, m_ComposeDenoisedShader, "main");

        VkComputePipelineCreateInfo composePipelineCI = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        composePipelineCI.layout                      = m_ComputePipelineLayout;
        composePipelineCI.stage                       = stageInfo;

        VkResult composeRes = vkCreateComputePipelines(
                m_Device, VK_NULL_HANDLE, 1, &composePipelineCI, nullptr, &m_ComposeDenoisedPipeline);
        if (composeRes != VK_SUCCESS || m_ComposeDenoisedPipeline == VK_NULL_HANDLE) {
            if (m_ComposeDenoisedPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(m_Device, m_ComposeDenoisedPipeline, nullptr);
                m_ComposeDenoisedPipeline = VK_NULL_HANDLE;
            }
            throw std::runtime_error(
                    "Failed to create compose compute pipeline. Result code: " + std::to_string((int) composeRes));
        }
        else {
            WL_INFO_TAG("Renderer", "Compose compute pipeline created successfully.");
        }
    }

    void Renderer::CreateShaderBindingTable(const Scene& scene)
    {
        const uint32_t handleSize      = m_RTPipelineProperties.shaderGroupHandleSize;
        const uint32_t handleAlignment = m_RTPipelineProperties.shaderGroupHandleAlignment;
        const uint32_t baseAlignment   = m_RTPipelineProperties.shaderGroupBaseAlignment;
        const uint32_t N               = (uint32_t) scene.ProceduralEntities.size();
        const uint32_t totalGroups     = 6u + N * 2u;

        // Align-up helper
        auto alignUp = [](uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); };

        // All entries in a region share one stride.
        // Raygen/Miss: no record data → stride = alignUp(handleSize, handleAlignment)
        // Hit: AABB groups have 4-byte packed records (instanceIndex<<4 | primitiveType)
        //      → stride = alignUp(handleSize + 4, handleAlignment)
        const uint32_t raygenStride = alignUp(handleSize, handleAlignment);
        const uint32_t missStride   = alignUp(handleSize, handleAlignment);
        const uint32_t hitStride    = alignUp(handleSize + 4u, handleAlignment);

        // Region sizes must be multiples of baseAlignment
        const uint32_t raygenRegionSize = alignUp(2u * raygenStride, baseAlignment);
        const uint32_t missRegionSize   = alignUp(2u * missStride, baseAlignment);
        const uint32_t hitEntries       = 2u + N * 2u;
        const uint32_t hitRegionSize    = alignUp(hitEntries * hitStride, baseAlignment);

        const VkDeviceSize sbtSize = raygenRegionSize + missRegionSize + hitRegionSize;
        m_SBTBufferSize            = sbtSize;
        // Some drivers do not expose HOST_VISIBLE|HOST_COHERENT for SBT-compatible memory.
        // Request HOST_VISIBLE and explicitly flush after CPU writes.
        m_SBTBuffer = CreateBuffer(sbtSize,
                VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, m_SBTMemory);

        // Fetch all group handles
        std::vector<uint8_t> handles(handleSize * totalGroups);
        VkResult handlesRes = pvkGetRayTracingShaderGroupHandlesKHR(
                m_Device, m_RTPipeline, 0, totalGroups, handles.size(), handles.data());
        if (handlesRes != VK_SUCCESS) {
            throw std::runtime_error("Failed to fetch RT shader group handles (vkGetRayTracingShaderGroupHandlesKHR)");
        }
        auto handle = [&](uint32_t groupIdx) { return handles.data() + groupIdx * handleSize; };

        // Map and write SBT
        uint8_t* base   = nullptr;
        VkResult mapRes = vkMapMemory(m_Device, m_SBTMemory, 0, sbtSize, 0, (void**) &base);
        if (mapRes != VK_SUCCESS || base == nullptr) {
            throw std::runtime_error("Failed to map SBT memory (vkMapMemory)");
        }
        memset(base, 0, sbtSize);

        // Raygen region (groups 0 and 1)
        uint8_t* pRaygen = base;
        memcpy(pRaygen + 0 * raygenStride, handle(0), handleSize);
        memcpy(pRaygen + 1 * raygenStride, handle(1), handleSize);

        // Miss region (groups 2 and 3)
        uint8_t* pMiss = base + raygenRegionSize;
        memcpy(pMiss + 0 * missStride, handle(2), handleSize);
        memcpy(pMiss + 1 * missStride, handle(3), handleSize);

        // Hit region (groups 4..totalGroups-1)
        uint8_t* pHit = base + raygenRegionSize + missRegionSize;
        // Triangle hit groups (entries 0 and 1, groups 4 and 5) — no record data
        memcpy(pHit + 0 * hitStride, handle(4), handleSize);
        memcpy(pHit + 1 * hitStride, handle(5), handleSize);
        // AABB hit groups — per-entry record: packed uint32_t (instanceIndex<<4 | primitiveType)
        for (uint32_t i = 0; i < N; ++i) {
            uint32_t packed = (i << 4u) | (scene.ProceduralEntities[i].PrimitiveType & 0xFu);
            // Radiance group (entry 2 + i*2)
            uint8_t* pRadiance = pHit + (2u + i * 2u) * hitStride;
            memcpy(pRadiance, handle(6u + i * 2u), handleSize);
            memcpy(pRadiance + handleSize, &packed, sizeof(packed));
            // Shadow group (entry 3 + i*2)
            uint8_t* pShadow = pHit + (3u + i * 2u) * hitStride;
            memcpy(pShadow, handle(6u + i * 2u + 1u), handleSize);
            memcpy(pShadow + handleSize, &packed, sizeof(packed));
        }

        VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
        range.memory              = m_SBTMemory;
        range.offset              = 0;
        range.size                = sbtSize;
        vkFlushMappedMemoryRanges(m_Device, 1, &range);
        vkUnmapMemory(m_Device, m_SBTMemory);

        // Device addresses for each region
        VkBufferDeviceAddressInfo ai  = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        ai.buffer                     = m_SBTBuffer;
        const VkDeviceAddress sbtAddr = pvkGetBufferDeviceAddressKHR(m_Device, &ai);

        m_RaygenRegion.deviceAddress = sbtAddr;
        m_RaygenRegion.stride        = raygenStride;
        m_RaygenRegion.size          = raygenStride;  // one entry at a time (selected at dispatch)

        m_MissRegion.deviceAddress = sbtAddr + raygenRegionSize;
        m_MissRegion.stride        = missStride;
        m_MissRegion.size          = 2u * missStride;

        m_HitRegion.deviceAddress = sbtAddr + raygenRegionSize + missRegionSize;
        m_HitRegion.stride        = hitStride;
        m_HitRegion.size          = hitEntries * hitStride;

        m_CallableRegion = {};
    }

    void Renderer::CreateDescriptorSets()
    {
        // All RT stages used across bindings
        const VkShaderStageFlags kAllRT        = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                                                 | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
        const VkShaderStageFlags kAllRTCompute = kAllRT | VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bindings[24] = {};

        // 0: TLAS
        bindings[0] = { 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, kAllRTCompute, nullptr };
        // 1: output image (rgba8)
        bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, kAllRTCompute, nullptr };
        // 2: vertex buffer
        bindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kAllRTCompute, nullptr };
        // 3: index buffer
        bindings[3] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kAllRTCompute, nullptr };
        // 4: triangle materials
        bindings[4] = { 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kAllRTCompute, nullptr };
        // 5: per-triangle material indices
        bindings[5] = { 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kAllRTCompute, nullptr };
        // 6: lights
        bindings[6] = { 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kAllRTCompute, nullptr };
        // 7: textures (up to 16)
        bindings[7] = { 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16, kAllRTCompute, nullptr };
        // 8: AABB transforms
        bindings[8] = { 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kAllRTCompute, nullptr };
        // 9: AABB materials
        bindings[9] = { 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kAllRTCompute, nullptr };
        // 10: accumulation image (rgba32f)
        bindings[10] = { 10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, kAllRTCompute, nullptr };
        // 11: scene UBO
        bindings[11] = { 11, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, kAllRTCompute, nullptr };
        // 12-16: NRD guide buffers
        bindings[12] = { 12, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, kAllRTCompute, nullptr };
        // 13: viewZ
        bindings[13] = { 13, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, kAllRTCompute, nullptr };
        // 14: motion
        bindings[14] = { 14, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, kAllRTCompute, nullptr };
        // 15: diff
        bindings[15] = { 15, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, kAllRTCompute, nullptr };
        // 16: spec
        bindings[16] = { 16, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, kAllRTCompute, nullptr };
        // 17: previous-frame vertex buffer
        bindings[17] = { 17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kAllRTCompute, nullptr };
        // 18: previous-frame AABB transforms
        bindings[18] = { 18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kAllRTCompute, nullptr };
        // 19-20: Separate OUT targets for NRD
        bindings[19] = { 19, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, kAllRTCompute, nullptr };
        // 20: specular
        bindings[20] = { 20, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, kAllRTCompute, nullptr };
        // 21: guide emission
        bindings[21] = { 21, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, kAllRTCompute, nullptr };
        // 22: guide depth
        bindings[22] = { 22, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, kAllRTCompute, nullptr };
        // 23: denoise metrics accumulation buffer
        bindings[23] = { 23, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kAllRTCompute, nullptr };

        VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.bindingCount                    = 24;
        layoutInfo.pBindings                       = bindings;
        vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout);

        // Pool
        VkDescriptorPoolSize poolSizes[5] = {};
        poolSizes[0]                      = { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 };
        poolSizes[1]                      = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 11 };  // binding 1,10,12-16,19,20,21,22
        poolSizes[2]                      = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            10 };  // vtx,idx,mat,matIdx,lights,aabbT,aabbM,prevVtx,prevAabbT,qualityMetrics
        poolSizes[3]                      = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 };
        poolSizes[4]                      = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };

        VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets                    = 1;
        poolInfo.poolSizeCount              = 5;
        poolInfo.pPoolSizes                 = poolSizes;
        vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool);

        VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool              = m_DescriptorPool;
        allocInfo.descriptorSetCount          = 1;
        allocInfo.pSetLayouts                 = &m_DescriptorSetLayout;
        vkAllocateDescriptorSets(m_Device, &allocInfo, &m_DescriptorSet);
    }

    void Renderer::CreateSceneBuffers(const Scene& scene)
    {
        // Calculate total vertices and indices across all meshes (static + dynamic)
        size_t totalVertices = 0;
        size_t totalIndices  = 0;
        for (const auto& mesh : scene.StaticMeshes) {
            totalVertices += mesh.Vertices.size();
            totalIndices += mesh.Indices.size();
        }
        for (const auto& mesh : scene.DynamicMeshes) {
            totalVertices += mesh.Vertices.size();
            totalIndices += mesh.Indices.size();
        }

        // Create vertex buffer
        size_t vertexCount = std::max(totalVertices, (size_t) 1);
        m_VertexBufferSize = sizeof(GPUVertex) * vertexCount;
        m_VertexBuffer     = CreateBuffer(m_VertexBufferSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_VertexMemory);

        m_PrevVertexBufferSize = m_VertexBufferSize;
        m_PrevVertexBuffer     = CreateBuffer(m_PrevVertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_PrevVertexMemory);

        // Create index buffer
        size_t indexCount = std::max(totalIndices, (size_t) 1);
        m_IndexBufferSize = sizeof(uint32_t) * indexCount;
        m_IndexBuffer     = CreateBuffer(m_IndexBufferSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_IndexMemory);

        // Create material buffer
        size_t materialCount = std::max(scene.Materials.size(), (size_t) 1);
        m_MaterialBufferSize = sizeof(GPUPBRMaterial) * materialCount;
        m_MaterialBuffer     = CreateBuffer(m_MaterialBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_MaterialMemory);

        // Create material index buffer (one uint32 per triangle)
        size_t triangleCount      = std::max(totalIndices / 3, (size_t) 1);
        m_MaterialIndexBufferSize = sizeof(uint32_t) * triangleCount;
        m_MaterialIndexBuffer     = CreateBuffer(m_MaterialIndexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_MaterialIndexMemory);

        // Create AABB transform buffer
        size_t aabbCount          = std::max(scene.ProceduralEntities.size(), (size_t) 1);
        m_AABBTransformBufferSize = sizeof(AABBTransform) * aabbCount;
        m_AABBTransformBuffer     = CreateBuffer(m_AABBTransformBufferSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_AABBTransformMemory);

        m_PrevAABBTransformBufferSize = m_AABBTransformBufferSize;
        m_PrevAABBTransformBuffer     = CreateBuffer(m_PrevAABBTransformBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_PrevAABBTransformMemory);

        // Create AABB material buffer (one GPUPBRMaterial per procedural entity)
        m_AABBMaterialBufferSize = sizeof(GPUPBRMaterial) * aabbCount;
        m_AABBMaterialBuffer     = CreateBuffer(m_AABBMaterialBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_AABBMaterialMemory);

        // Create light buffer
        size_t lightCount = std::max(scene.Lights.size(), (size_t) 1);
        m_LightBufferSize = sizeof(GPULight) * lightCount;
        m_LightBuffer     = CreateBuffer(m_LightBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_LightMemory);

        // Create Scene UBO buffer
        m_SceneUBOBuffer = CreateBuffer(sizeof(SceneUBOData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_SceneUBOMemory);
    }

    void Renderer::UpdateSceneData(const Scene& scene)
    {
        // Flatten all meshes (static + dynamic) into single vertex/index buffers
        std::vector<GPUVertex> gpuVertices;
        std::vector<uint32_t> gpuIndices;
        std::vector<uint32_t> materialIndices;  // Material index per triangle
        uint32_t vertexOffset = 0;

        // Process static meshes
        for (const auto& mesh : scene.StaticMeshes) {
            // Compute normal matrix once per mesh (avoid per-vertex inverse/transpose)
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(mesh.Transform)));

            for (const auto& vertex : mesh.Vertices) {
                GPUVertex gpuVert{};
                gpuVert.position = glm::vec3(mesh.Transform * glm::vec4(vertex.Position, 1.0f));
                gpuVert.normal   = glm::normalize(normalMatrix * vertex.Normal);
                gpuVert.texCoord = vertex.TexCoord;
                gpuVertices.push_back(gpuVert);
            }

            // Copy indices with offset
            for (const auto& index : mesh.Indices) { gpuIndices.push_back(index + vertexOffset); }

            // Store material index for each triangle in this mesh
            const uint32_t triangleCount = static_cast<uint32_t>(mesh.Indices.size() / 3);
            for (uint32_t i = 0; i < triangleCount; i++) {
                materialIndices.push_back(static_cast<uint32_t>(mesh.MaterialIndex));
            }

            vertexOffset += static_cast<uint32_t>(mesh.Vertices.size());
        }

        // Process dynamic meshes
        for (const auto& mesh : scene.DynamicMeshes) {
            // Compute normal matrix once per mesh (avoid per-vertex inverse/transpose)
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(mesh.Transform)));

            for (const auto& vertex : mesh.Vertices) {
                GPUVertex gpuVert{};
                gpuVert.position = glm::vec3(mesh.Transform * glm::vec4(vertex.Position, 1.0f));
                gpuVert.normal   = glm::normalize(normalMatrix * vertex.Normal);
                gpuVert.texCoord = vertex.TexCoord;
                gpuVertices.push_back(gpuVert);
            }

            // Copy indices with offset
            for (const auto& index : mesh.Indices) { gpuIndices.push_back(index + vertexOffset); }

            // Store material index for each triangle in this mesh
            const uint32_t triangleCount = static_cast<uint32_t>(mesh.Indices.size() / 3);
            for (uint32_t i = 0; i < triangleCount; i++) {
                materialIndices.push_back(static_cast<uint32_t>(mesh.MaterialIndex));
            }

            vertexOffset += static_cast<uint32_t>(mesh.Vertices.size());
        }

        std::vector<GPUVertex> prevGpuVertices
                = (m_PreviousFrameVertices.size() == gpuVertices.size()) ? m_PreviousFrameVertices : gpuVertices;

        // Upload vertex data to GPU
        void* data;
        if (!gpuVertices.empty()) {
            vkMapMemory(m_Device, m_VertexMemory, 0, sizeof(GPUVertex) * gpuVertices.size(), 0, &data);
            memcpy(data, gpuVertices.data(), sizeof(GPUVertex) * gpuVertices.size());
            vkUnmapMemory(m_Device, m_VertexMemory);

            vkMapMemory(m_Device, m_PrevVertexMemory, 0, sizeof(GPUVertex) * prevGpuVertices.size(), 0, &data);
            memcpy(data, prevGpuVertices.data(), sizeof(GPUVertex) * prevGpuVertices.size());
            vkUnmapMemory(m_Device, m_PrevVertexMemory);
        }

        // Upload index data to GPU
        if (!gpuIndices.empty()) {
            vkMapMemory(m_Device, m_IndexMemory, 0, sizeof(uint32_t) * gpuIndices.size(), 0, &data);
            memcpy(data, gpuIndices.data(), sizeof(uint32_t) * gpuIndices.size());
            vkUnmapMemory(m_Device, m_IndexMemory);
        }

        // Upload material index data (one per triangle)
        if (!materialIndices.empty()) {
            vkMapMemory(m_Device, m_MaterialIndexMemory, 0, sizeof(uint32_t) * materialIndices.size(), 0, &data);
            memcpy(data, materialIndices.data(), sizeof(uint32_t) * materialIndices.size());
            vkUnmapMemory(m_Device, m_MaterialIndexMemory);
        }

        // Upload light data
        if (!scene.Lights.empty()) {
            std::vector<GPULight> gpuLights(scene.Lights.size());
            for (size_t i = 0; i < scene.Lights.size(); i++) {
                gpuLights[i].position  = scene.Lights[i].Position;
                gpuLights[i].intensity = scene.Lights[i].Intensity;
                gpuLights[i].emission  = scene.Lights[i].Emission;
                gpuLights[i].size      = scene.Lights[i].Size;
                gpuLights[i].direction = scene.Lights[i].Direction;
                gpuLights[i].type      = static_cast<uint32_t>(scene.Lights[i].Type);
            }

            vkMapMemory(m_Device, m_LightMemory, 0, sizeof(GPULight) * gpuLights.size(), 0, &data);
            memcpy(data, gpuLights.data(), sizeof(GPULight) * gpuLights.size());
            vkUnmapMemory(m_Device, m_LightMemory);
        }

        // Upload AABB transforms and materials for procedural entities
        if (!scene.ProceduralEntities.empty()) {
            std::vector<AABBTransform> aabbTransforms(scene.ProceduralEntities.size());
            std::vector<GPUPBRMaterial> aabbMaterials(scene.ProceduralEntities.size());
            for (size_t i = 0; i < scene.ProceduralEntities.size(); i++) {
                const auto& pe                              = scene.ProceduralEntities[i];
                aabbTransforms[i].localSpaceToBottomLevelAS = pe.Transform;
                aabbTransforms[i].bottomLevelASToLocalSpace = glm::inverse(pe.Transform);

                if (pe.MaterialIndex >= 0 && pe.MaterialIndex < (int) scene.Materials.size()) {
                    const auto& mat         = scene.Materials[pe.MaterialIndex];
                    GPUPBRMaterial& gm      = aabbMaterials[i];
                    gm.albedo               = mat.Albedo;
                    gm.textureIndex         = -1;
                    gm.emission             = mat.Emission;
                    gm.tiling               = mat.Tiling;
                    gm.extinction           = mat.Extinction;
                    gm.materialIndex        = mat.MaterialIndex;
                    gm.stepScale            = mat.StepScale;
                    gm.sheen                = mat.Sheen;
                    gm.sheenTint            = mat.SheenTint;
                    gm.clearcoat            = mat.Clearcoat;
                    gm.clearcoatGloss       = mat.ClearcoatGloss;
                    gm.roughness            = mat.Roughness;
                    gm.subsurface           = mat.Subsurface;
                    gm.anisotropic          = mat.Anisotropic;
                    gm.metallic             = mat.Metallic;
                    gm.specularTint         = mat.SpecularTint;
                    gm.specularTransmission = mat.SpecularTransmission;
                    gm.eta                  = mat.Eta;
                    gm.atDistance           = mat.AtDistance;
                    gm.lightIndex           = mat.LightIndex;
                    gm._pad1 = gm._pad2 = 0.0f;
                }
            }

            std::vector<AABBTransform> prevAabbTransforms
                    = (m_PreviousFrameAABBTransforms.size() == aabbTransforms.size()) ? m_PreviousFrameAABBTransforms
                                                                                      : aabbTransforms;

            void* aabbData;
            vkMapMemory(
                    m_Device, m_AABBTransformMemory, 0, sizeof(AABBTransform) * aabbTransforms.size(), 0, &aabbData);
            memcpy(aabbData, aabbTransforms.data(), sizeof(AABBTransform) * aabbTransforms.size());
            vkUnmapMemory(m_Device, m_AABBTransformMemory);

            vkMapMemory(m_Device, m_PrevAABBTransformMemory, 0, sizeof(AABBTransform) * prevAabbTransforms.size(), 0,
                    &aabbData);
            memcpy(aabbData, prevAabbTransforms.data(), sizeof(AABBTransform) * prevAabbTransforms.size());
            vkUnmapMemory(m_Device, m_PrevAABBTransformMemory);

            vkMapMemory(m_Device, m_AABBMaterialMemory, 0, sizeof(GPUPBRMaterial) * aabbMaterials.size(), 0, &aabbData);
            memcpy(aabbData, aabbMaterials.data(), sizeof(GPUPBRMaterial) * aabbMaterials.size());
            vkUnmapMemory(m_Device, m_AABBMaterialMemory);

            m_PreviousFrameAABBTransforms = std::move(aabbTransforms);
        }
        else {
            m_PreviousFrameAABBTransforms.clear();
        }

        m_PreviousFrameVertices = std::move(gpuVertices);

        // Rebuild acceleration structure with all meshes (static + dynamic) and procedural entities
        std::vector<Mesh> allMeshes;
        allMeshes.insert(allMeshes.end(), scene.StaticMeshes.begin(), scene.StaticMeshes.end());
        allMeshes.insert(allMeshes.end(), scene.DynamicMeshes.begin(), scene.DynamicMeshes.end());
        m_AccelerationStructure->Rebuild(allMeshes, m_VertexBuffer, m_IndexBuffer, scene.ProceduralEntities);

        // Update descriptor sets
        VkWriteDescriptorSetAccelerationStructureKHR asInfo
                = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
        asInfo.accelerationStructureCount = 1;
        VkAccelerationStructureKHR tlas   = m_AccelerationStructure->GetTLAS();
        asInfo.pAccelerationStructures    = &tlas;

        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageView             = m_FinalImage->GetVkImageView();
        imageInfo.imageLayout           = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorBufferInfo vertexBufferInfo = {};
        vertexBufferInfo.buffer                 = m_VertexBuffer;
        vertexBufferInfo.offset                 = 0;
        vertexBufferInfo.range                  = m_VertexBufferSize > 0 ? m_VertexBufferSize : 16;

        VkDescriptorBufferInfo indexBufferInfo = {};
        indexBufferInfo.buffer                 = m_IndexBuffer;
        indexBufferInfo.offset                 = 0;
        indexBufferInfo.range                  = m_IndexBufferSize > 0 ? m_IndexBufferSize : 16;

        VkDescriptorBufferInfo materialBufferInfo = {};
        materialBufferInfo.buffer                 = m_MaterialBuffer;
        materialBufferInfo.offset                 = 0;
        materialBufferInfo.range                  = m_MaterialBufferSize > 0 ? m_MaterialBufferSize : 16;

        VkDescriptorBufferInfo materialIndexBufferInfo = {};
        materialIndexBufferInfo.buffer                 = m_MaterialIndexBuffer;
        materialIndexBufferInfo.offset                 = 0;
        materialIndexBufferInfo.range                  = m_MaterialIndexBufferSize > 0 ? m_MaterialIndexBufferSize : 16;

        VkDescriptorBufferInfo lightBufferInfo = {};
        lightBufferInfo.buffer                 = m_LightBuffer;
        lightBufferInfo.offset                 = 0;
        lightBufferInfo.range                  = m_LightBufferSize > 0 ? m_LightBufferSize : 16;

        // Collect all textures from the scene materials
        std::vector<std::shared_ptr<Walnut::Image>> allTextures;
        std::unordered_map<std::string, int> textureToIndex;
        for (const auto& mat : scene.Materials) {
            if (!mat.TextureFilename.empty() && textureToIndex.find(mat.TextureFilename) == textureToIndex.end()) {
                auto tex = LoadOrGetTexture(mat.TextureFilename);
                if (tex) {
                    textureToIndex[mat.TextureFilename] = (int) allTextures.size();
                    allTextures.push_back(tex);
                }
            }
        }

        // Update material buffer again with the correct texture indices
        if (!scene.Materials.empty()) {
            std::vector<GPUPBRMaterial> gpuMaterials(scene.Materials.size());
            for (size_t i = 0; i < scene.Materials.size(); i++) {
                const auto& mat         = scene.Materials[i];
                GPUPBRMaterial& gm      = gpuMaterials[i];
                gm.albedo               = mat.Albedo;
                gm.emission             = mat.Emission;
                gm.tiling               = mat.Tiling;
                gm.extinction           = mat.Extinction;
                gm.materialIndex        = mat.MaterialIndex;
                gm.stepScale            = mat.StepScale;
                gm.sheen                = mat.Sheen;
                gm.sheenTint            = mat.SheenTint;
                gm.clearcoat            = mat.Clearcoat;
                gm.clearcoatGloss       = mat.ClearcoatGloss;
                gm.roughness            = mat.Roughness;
                gm.subsurface           = mat.Subsurface;
                gm.anisotropic          = mat.Anisotropic;
                gm.metallic             = mat.Metallic;
                gm.specularTint         = mat.SpecularTint;
                gm.specularTransmission = mat.SpecularTransmission;
                gm.eta                  = mat.Eta;
                gm.atDistance           = mat.AtDistance;
                gm.lightIndex           = mat.LightIndex;
                gm._pad1 = gm._pad2 = 0.0f;

                auto it         = textureToIndex.find(mat.TextureFilename);
                gm.textureIndex = (it != textureToIndex.end()) ? it->second : -1;
            }
            void* data;
            vkMapMemory(m_Device, m_MaterialMemory, 0, sizeof(GPUPBRMaterial) * gpuMaterials.size(), 0, &data);
            memcpy(data, gpuMaterials.data(), sizeof(GPUPBRMaterial) * gpuMaterials.size());
            vkUnmapMemory(m_Device, m_MaterialMemory);
        }

        std::vector<VkDescriptorImageInfo> textureInfos(16);
        for (size_t i = 0; i < 16; i++) {
            if (i < allTextures.size()) {
                textureInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                textureInfos[i].imageView   = allTextures[i]->GetVkImageView();
                textureInfos[i].sampler     = allTextures[i]->GetVkSampler();
            }
            else {
                // Point to final image as a fallback (valid object for Vulkan)
                textureInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                textureInfos[i].imageView   = m_FinalImage->GetVkImageView();
                textureInfos[i].sampler     = m_FinalImage->GetVkSampler();
            }
        }

        VkWriteDescriptorSet writes[8] = {};

        // Acceleration structure
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].pNext           = &asInfo;
        writes[0].dstSet          = m_DescriptorSet;
        writes[0].dstBinding      = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

        // Storage image
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_DescriptorSet;
        writes[1].dstBinding      = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo      = &imageInfo;

        // Vertex buffer
        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = m_DescriptorSet;
        writes[2].dstBinding      = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo     = &vertexBufferInfo;

        // Index buffer
        writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet          = m_DescriptorSet;
        writes[3].dstBinding      = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].pBufferInfo     = &indexBufferInfo;

        // Material buffer
        writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet          = m_DescriptorSet;
        writes[4].dstBinding      = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].pBufferInfo     = &materialBufferInfo;

        // Material index buffer
        writes[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet          = m_DescriptorSet;
        writes[5].dstBinding      = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].pBufferInfo     = &materialIndexBufferInfo;

        // Light buffer
        writes[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet          = m_DescriptorSet;
        writes[6].dstBinding      = 6;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].pBufferInfo     = &lightBufferInfo;

        // Textures
        writes[7].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet          = m_DescriptorSet;
        writes[7].dstBinding      = 7;
        writes[7].descriptorCount = 16;
        writes[7].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[7].pImageInfo      = textureInfos.data();

        vkUpdateDescriptorSets(m_Device, 8, writes, 0, nullptr);

        // Bindings 8–11: AABB transforms, AABB materials, accum image, scene UBO
        VkDescriptorBufferInfo aabbTransformInfo = {};
        aabbTransformInfo.buffer                 = m_AABBTransformBuffer;
        aabbTransformInfo.offset                 = 0;
        aabbTransformInfo.range                  = m_AABBTransformBufferSize > 0 ? m_AABBTransformBufferSize : 16;

        VkDescriptorBufferInfo aabbMaterialInfo = {};
        aabbMaterialInfo.buffer                 = m_AABBMaterialBuffer;
        aabbMaterialInfo.offset                 = 0;
        aabbMaterialInfo.range                  = m_AABBMaterialBufferSize > 0 ? m_AABBMaterialBufferSize : 16;

        VkDescriptorImageInfo accumImageInfo = {};
        accumImageInfo.imageView   = m_AccumImage ? m_AccumImage->GetVkImageView() : m_FinalImage->GetVkImageView();
        accumImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorBufferInfo sceneUBOInfo = {};
        sceneUBOInfo.buffer                 = m_SceneUBOBuffer;
        sceneUBOInfo.offset                 = 0;
        sceneUBOInfo.range                  = sizeof(SceneUBOData);

        VkDescriptorBufferInfo prevVertexBufferInfo = {};
        prevVertexBufferInfo.buffer                 = m_PrevVertexBuffer;
        prevVertexBufferInfo.offset                 = 0;
        prevVertexBufferInfo.range                  = m_PrevVertexBufferSize > 0 ? m_PrevVertexBufferSize : 16;

        VkDescriptorBufferInfo prevAabbTransformInfo = {};
        prevAabbTransformInfo.buffer                 = m_PrevAABBTransformBuffer;
        prevAabbTransformInfo.offset                 = 0;
        prevAabbTransformInfo.range = m_PrevAABBTransformBufferSize > 0 ? m_PrevAABBTransformBufferSize : 16;

        VkWriteDescriptorSet extraWrites[6] = {};

        extraWrites[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[0].dstSet          = m_DescriptorSet;
        extraWrites[0].dstBinding      = 8;
        extraWrites[0].descriptorCount = 1;
        extraWrites[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        extraWrites[0].pBufferInfo     = &aabbTransformInfo;

        extraWrites[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[1].dstSet          = m_DescriptorSet;
        extraWrites[1].dstBinding      = 9;
        extraWrites[1].descriptorCount = 1;
        extraWrites[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        extraWrites[1].pBufferInfo     = &aabbMaterialInfo;

        extraWrites[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[2].dstSet          = m_DescriptorSet;
        extraWrites[2].dstBinding      = 10;
        extraWrites[2].descriptorCount = 1;
        extraWrites[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        extraWrites[2].pImageInfo      = &accumImageInfo;

        extraWrites[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[3].dstSet          = m_DescriptorSet;
        extraWrites[3].dstBinding      = 11;
        extraWrites[3].descriptorCount = 1;
        extraWrites[3].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        extraWrites[3].pBufferInfo     = &sceneUBOInfo;

        extraWrites[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[4].dstSet          = m_DescriptorSet;
        extraWrites[4].dstBinding      = 17;
        extraWrites[4].descriptorCount = 1;
        extraWrites[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        extraWrites[4].pBufferInfo     = &prevVertexBufferInfo;

        extraWrites[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[5].dstSet          = m_DescriptorSet;
        extraWrites[5].dstBinding      = 18;
        extraWrites[5].descriptorCount = 1;
        extraWrites[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        extraWrites[5].pBufferInfo     = &prevAabbTransformInfo;

        vkUpdateDescriptorSets(m_Device, 6, extraWrites, 0, nullptr);
    }

    auto Renderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
            VkDeviceMemory& bufferMemory) const -> VkBuffer
    {
        if (size == 0) size = 16;  // Minimum size to avoid Vulkan errors

        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size               = size;
        bufferInfo.usage              = usage;
        bufferInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer buffer    = VK_NULL_HANDLE;
        VkResult createRes = vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer);
        if (createRes != VK_SUCCESS || buffer == VK_NULL_HANDLE) {
            throw std::runtime_error("vkCreateBuffer failed in Renderer::CreateBuffer");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_Device, buffer, &memRequirements);

        // Find memory type
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(Walnut::Application::GetPhysicalDevice(), &memProperties);

        uint32_t memoryTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((memRequirements.memoryTypeBits & (1 << i))
                    && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                memoryTypeIndex = i;
                break;
            }
        }

        if (memoryTypeIndex == UINT32_MAX) {
            throw std::runtime_error("No compatible Vulkan memory type found in Renderer::CreateBuffer");
        }

        VkMemoryAllocateFlagsInfo allocFlagsInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
        allocFlagsInfo.flags                     = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocInfo.pNext           = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? &allocFlagsInfo : nullptr;
        allocInfo.allocationSize  = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        VkResult allocRes = vkAllocateMemory(m_Device, &allocInfo, nullptr, &bufferMemory);
        if (allocRes != VK_SUCCESS || bufferMemory == VK_NULL_HANDLE) {
            throw std::runtime_error("vkAllocateMemory failed in Renderer::CreateBuffer");
        }

        VkResult bindRes = vkBindBufferMemory(m_Device, buffer, bufferMemory, 0);
        if (bindRes != VK_SUCCESS) { throw std::runtime_error("vkBindBufferMemory failed in Renderer::CreateBuffer"); }

        return buffer;
    }

    void Renderer::PreloadTextures(const std::vector<std::string>& textureFilenames)
    {
        for (const auto& textureFilename : textureFilenames) { LoadOrGetTexture(textureFilename); }
    }

    auto Renderer::LoadOrGetTexture(const std::string& filename) -> std::shared_ptr<Walnut::Image>
    {
        auto filepath = Vlkrt::TEXTURES_DIR + filename;

        // Check if texture already cached
        auto it = m_TextureCache.find(filename);
        if (it != m_TextureCache.end()) { return it->second; }

        // Try to load texture from disk
        std::shared_ptr<Walnut::Image> texture;

        try {
            auto newImg = std::make_shared<Walnut::Image>(filepath);
            if (newImg->GetWidth() > 0) {
                texture                  = newImg;
                m_TextureCache[filename] = texture;
                WL_INFO_TAG("Renderer", "Loaded texture: {}", filepath);
            }
        }
        catch (const std::exception& e) {
            WL_WARN_TAG("Renderer", "Failed to load texture '{}': {}", filepath, e.what());
        }

        if (!texture) { WL_WARN_TAG("Renderer", "Failed to load texture '{}'.", filepath); }

        return texture;
    }
}  // namespace Vlkrt