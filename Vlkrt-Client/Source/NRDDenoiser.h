#pragma once

#include "Walnut/Image.h"

#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace Vlkrt
{
    // Header availability check for staged direct NRD integration.
#if defined(__has_include)
#if __has_include("NRD.h")
#define VLKRT_NRD_HELPER_HEADERS_AVAILABLE 1
#else
#define VLKRT_NRD_HELPER_HEADERS_AVAILABLE 0
#endif
#else
#define VLKRT_NRD_HELPER_HEADERS_AVAILABLE 0
#endif

    struct NRDDenoiseParams
    {
        uint32_t FrameIndex = 0;
        uint32_t Width      = 0;
        uint32_t Height     = 0;

        // Matrices are column-major and match NRD common settings expectations.
        float ViewToClip[16]     = {};
        float ViewToClipPrev[16] = {};
        float WorldToView[16]    = {};
        float WorldToViewPrev[16]= {};

        float CameraJitter[2]     = { 0.0f, 0.0f };
        float CameraJitterPrev[2] = { 0.0f, 0.0f };

        bool HasValidMatrices = false;
    };

    class NRDDenoiser
    {
    public:
        void Initialize(VkDevice device);
        void Shutdown();

        void SetEnabled(bool enabled);
        bool IsEnabled() const { return m_Enabled; }
        bool IsOperational() const { return m_Operational; }
        bool AreHelperHeadersAvailable() const { return m_HelperHeadersAvailable; }
        bool IsRuntimeLinked() const { return m_RuntimeLinked; }

        const char* GetStatus() const;

        // Set real guide buffers from renderer (populated by RT shader)
        void SetGuideBuffers(const std::shared_ptr<Walnut::Image>& normalRoughness,
                            const std::shared_ptr<Walnut::Image>& viewZ,
                            const std::shared_ptr<Walnut::Image>& motionVectors,
                            const std::shared_ptr<Walnut::Image>& diffRadianceHitDist,
                            const std::shared_ptr<Walnut::Image>& specRadianceHitDist)
        {
            m_GuideNormalRoughness = normalRoughness;
            m_GuideViewZ = viewZ;
            m_GuideMotionVectors = motionVectors;
            m_GuideDiffRadianceHitDist = diffRadianceHitDist;
            m_GuideSpecRadianceHitDist = specRadianceHitDist;
        }
        std::shared_ptr<Walnut::Image> GetOutDiffRadianceHitDist() const { return m_OutDiffRadianceHitDist; }
        std::shared_ptr<Walnut::Image> GetOutSpecRadianceHitDist() const { return m_OutSpecRadianceHitDist; }
        // Runs NRD denoising when available. In scaffold mode this is a no-op.
        void Dispatch(VkCommandBuffer cmd, const std::shared_ptr<Walnut::Image>& ioImage,
                      const NRDDenoiseParams& params);

    private:
        bool EnsureDirectInstance(uint32_t width, uint32_t height);
        bool BuildDirectBackend();
        void DestroyDirectBackend();
        bool ExecutePreparedDispatches(VkCommandBuffer cmd, const std::shared_ptr<Walnut::Image>& ioImage,
                                       const void* dispatchesOpaque, uint32_t dispatchesNum);

        struct NrdPipelineState
        {
            VkShaderModule ShaderModule = VK_NULL_HANDLE;
            VkPipeline Pipeline = VK_NULL_HANDLE;
            VkDescriptorSetLayout ResourceSetLayout = VK_NULL_HANDLE;
            VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
            
            std::vector<VkDescriptorSet> ResourceSets;
            uint32_t CurrentSetIndex = 0;

            // Per-resource: binding number and descriptor type, in the same
            // order as DispatchDesc::resources[].  Populated at build time.
            struct ResourceSlot { uint32_t binding; VkDescriptorType type; };
            std::vector<ResourceSlot> ResourceSlots;

            // Keep totals for quick sanity checks only
            uint32_t TextureCount = 0;
            uint32_t StorageCount = 0;
        };

        VkDevice m_Device      = VK_NULL_HANDLE;
        bool m_Enabled         = false;
        bool m_Operational     = false;
        bool m_HelperHeadersAvailable = false;
        bool m_RuntimeLinked   = false;
        bool m_DirectInstanceReady = false;
        bool m_BackendInitFailed = false;
        uint32_t m_InstanceWidth = 0;
        uint32_t m_InstanceHeight = 0;
        uint32_t m_LastPreparedDispatches = 0;
        void* m_DirectInstance = nullptr;
        bool m_BackendReady = false;
        uint32_t m_ConstantSetIndex = 0;
        uint32_t m_ResourceSetIndex = 1;
        bool m_LoggedUnsupportedResources = false;

        VkDescriptorPool m_NrdDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_NrdSet0Layout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_NrdSet0s;
        uint32_t m_CurrentSet0Index = 0;
        VkBuffer m_NrdConstantBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_NrdConstantMemory = VK_NULL_HANDLE;
        uint32_t m_NrdConstantCapacity = 0;
        uint32_t m_NrdConstantStride = 256;
        VkSampler m_NrdSamplers[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        std::vector<NrdPipelineState> m_NrdPipelines;

        // Guide buffers for REBLUR (placeholder textures, populated from RT shader later)
        std::shared_ptr<Walnut::Image> m_GuideNormalRoughness;    // IN_NORMAL_ROUGHNESS
        std::shared_ptr<Walnut::Image> m_GuideViewZ;              // IN_VIEWZ  
        std::shared_ptr<Walnut::Image> m_GuideMotionVectors;      // IN_MV
        std::shared_ptr<Walnut::Image> m_GuideDiffRadianceHitDist; // IN_DIFF_RADIANCE_HITDIST
        std::shared_ptr<Walnut::Image> m_GuideSpecRadianceHitDist; // IN_SPEC_RADIANCE_HITDIST

        // Separate OUT buffers (NRD does not support in-place read/writes)
        std::shared_ptr<Walnut::Image> m_OutDiffRadianceHitDist; 
        std::shared_ptr<Walnut::Image> m_OutSpecRadianceHitDist;

        VkImageView m_GuideViewZNrdImageView = VK_NULL_HANDLE;
        VkImageView m_GuideMotionVectorsNrdImageView = VK_NULL_HANDLE;
        std::vector<std::shared_ptr<Walnut::Image>> m_TransientPoolImages;
        std::vector<std::shared_ptr<Walnut::Image>> m_PermanentPoolImages;

        bool m_LoggedFirstPass = false;
        bool m_LoggedDispatchPlan = false;
    };
}
