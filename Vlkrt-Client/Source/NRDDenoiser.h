#pragma once

#include "Walnut/Image.h"

#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace Vlkrt
{
    struct NRDDenoiseParams
    {
        uint32_t FrameIndex{ 0 };
        uint32_t Width{ 0 };
        uint32_t Height{ 0 };

        // Matrices are column-major
        float ViewToClip[16]{};
        float ViewToClipPrev[16]{};
        float WorldToView[16]{};
        float WorldToViewPrev[16]{};

        float CameraJitter[2]{ 0.0f, 0.0f };
        float CameraJitterPrev[2]{ 0.0f, 0.0f };

        // Runtime RELAX tuning (live-updated from UI)
        float MinMaterialForDiffuse{ 0.0f };
        float MinMaterialForSpecular{ 0.0f };
        float DiffusePrepassBlurRadius{ 6.0f };
        float SpecularPrepassBlurRadius{ 10.0f };
        uint32_t DiffuseMaxAccumulatedFrameNum{ 10 };
        uint32_t SpecularMaxAccumulatedFrameNum{ 12 };
        uint32_t DiffuseMaxFastAccumulatedFrameNum{ 2 };
        uint32_t SpecularMaxFastAccumulatedFrameNum{ 3 };
        float AntilagAccelerationAmount{ 0.9f };
        float AntilagSpatialSigmaScale{ 2.5f };
        float AntilagTemporalSigmaScale{ 0.22f };
        float AntilagResetAmount{ 1.0f };
        float DisocclusionThreshold{ 0.010f };
        float DisocclusionThresholdAlternate{ 0.035f };

        bool HasValidMatrices = false;
    };

    class NRDDenoiser
    {
    public:
        void Initialize(VkDevice device);
        void Shutdown();

        void SetEnabled(bool enabled);
        bool IsEnabled() const { return m_Enabled; }
        auto IsOperational() const { return m_Operational; }
        auto IsRuntimeLinked() const { return m_RuntimeLinked; }

        auto GetStatus() const -> const char*;

        // Set real guide buffers from renderer (populated by RT shader)
        void SetGuideBuffers(const std::shared_ptr<Walnut::Image>& normalRoughness,
                const std::shared_ptr<Walnut::Image>& viewZ, const std::shared_ptr<Walnut::Image>& motionVectors,
                const std::shared_ptr<Walnut::Image>& diffRadianceHitDist,
                const std::shared_ptr<Walnut::Image>& specRadianceHitDist);
        std::shared_ptr<Walnut::Image> GetOutDiffRadianceHitDist() const { return m_OutDiffRadianceHitDist; }
        std::shared_ptr<Walnut::Image> GetOutSpecRadianceHitDist() const { return m_OutSpecRadianceHitDist; }
        void Dispatch(
                VkCommandBuffer cmd, const std::shared_ptr<Walnut::Image>& ioImage, const NRDDenoiseParams& params);

        auto EnsureDirectInstance(uint32_t width, uint32_t height) -> bool;

    private:
        auto BuildDirectBackend() -> bool;
        void DestroyDirectBackend();
        auto ExecutePreparedDispatches(VkCommandBuffer cmd, const std::shared_ptr<Walnut::Image>& ioImage,
                const void* dispatchesOpaque, uint32_t dispatchesNum) -> bool;

        struct NrdPipelineState
        {
            VkShaderModule ShaderModule             = VK_NULL_HANDLE;
            VkPipeline Pipeline                     = VK_NULL_HANDLE;
            VkDescriptorSetLayout ResourceSetLayout = VK_NULL_HANDLE;
            VkPipelineLayout PipelineLayout         = VK_NULL_HANDLE;

            std::vector<VkDescriptorSet> ResourceSets;
            uint32_t CurrentSetIndex = 0;

            // Per-resource: binding number and descriptor type, in the same order as DispatchDesc::resources[]
            struct ResourceSlot
            {
                uint32_t binding;
                VkDescriptorType type;
            };
            std::vector<ResourceSlot> ResourceSlots;
        };

        VkDevice m_Device                 = VK_NULL_HANDLE;
        bool m_Enabled                    = false;
        bool m_Operational                = false;
        bool m_RuntimeLinked              = false;
        bool m_DirectInstanceReady        = false;
        bool m_BackendInitFailed          = false;
        uint32_t m_InstanceWidth          = 0;
        uint32_t m_InstanceHeight         = 0;
        uint32_t m_LastPreparedDispatches = 0;
        void* m_DirectInstance            = nullptr;
        bool m_BackendReady               = false;
        uint32_t m_ConstantSetIndex       = 0;
        uint32_t m_ResourceSetIndex       = 1;

        VkDescriptorPool m_NrdDescriptorPool  = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_NrdSet0Layout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_NrdSet0s;
        uint32_t m_CurrentSet0Index        = 0;
        VkBuffer m_NrdConstantBuffer       = VK_NULL_HANDLE;
        VkDeviceMemory m_NrdConstantMemory = VK_NULL_HANDLE;
        void* m_NrdConstantMapped          = nullptr;
        uint32_t m_NrdConstantCapacity     = 0;
        uint32_t m_NrdConstantStride       = 256;
        VkSampler m_NrdSamplers[2]         = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        std::vector<NrdPipelineState> m_NrdPipelines;

        // Guide buffers for REBLUR
        std::shared_ptr<Walnut::Image> m_GuideNormalRoughness;      // IN_NORMAL_ROUGHNESS
        std::shared_ptr<Walnut::Image> m_GuideViewZ;                // IN_VIEWZ
        std::shared_ptr<Walnut::Image> m_GuideMotionVectors;        // IN_MV
        std::shared_ptr<Walnut::Image> m_GuideDiffRadianceHitDist;  // IN_DIFF_RADIANCE_HITDIST
        std::shared_ptr<Walnut::Image> m_GuideSpecRadianceHitDist;  // IN_SPEC_RADIANCE_HITDIST

        // OUT buffers
        std::shared_ptr<Walnut::Image> m_OutDiffRadianceHitDist;
        std::shared_ptr<Walnut::Image> m_OutSpecRadianceHitDist;

        VkImageView m_GuideViewZNrdImageView         = VK_NULL_HANDLE;
        VkImageView m_GuideMotionVectorsNrdImageView = VK_NULL_HANDLE;
        std::vector<std::shared_ptr<Walnut::Image>> m_TransientPoolImages;
        std::vector<std::shared_ptr<Walnut::Image>> m_PermanentPoolImages;
        bool m_FirstDispatchTransitionsDone = false;
        NRDDenoiseParams m_LastAppliedParams{};
        bool m_HasLastAppliedParams = false;
    };
}  // namespace Vlkrt
