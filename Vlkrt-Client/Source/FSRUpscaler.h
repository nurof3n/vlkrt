#pragma once
#include <bit>

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>
#include "Walnut/Image.h"

#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>

namespace Vlkrt
{
    /// <summary>
    /// Class that encapsulates the FSR3 upscaling context and resources, providing an easy-to-use interface for
    /// initializing, resizing, and dispatching FSR upscaling in a Vulkan application.
    /// It manages the necessary Vulkan resources and FSR state to perform high-quality temporal upscaling based on the
    /// input color, depth, and motion vector buffers. The class also provides utility functions to calculate the
    /// appropriate render resolution based on the display resolution and selected quality mode.
    /// </summary>
    class FSRUpscaler
    {
    public:
        enum class Quality
        {
            NativeAA         = 0,
            Quality          = 1,
            Balanced         = 2,
            Performance      = 3,
            UltraPerformance = 4
        };

        FSRUpscaler();
        ~FSRUpscaler();

        void Initialize(VkDevice device, VkPhysicalDevice physDev, VkQueue queue, uint32_t queueFamilyIdx);
        void Shutdown();

        void OnResize(uint32_t displayW, uint32_t displayH, Quality q, float sharpness);

        static void GetRenderResolution(
                uint32_t displayW, uint32_t displayH, Quality q, uint32_t& renderW, uint32_t& renderH);

        void Dispatch(VkCommandBuffer cmd,
                std::shared_ptr<Walnut::Image> colorIn,   // denoised, internal res, RGBA32F
                std::shared_ptr<Walnut::Image> depthIn,   // NDC depth, internal res, R32F
                std::shared_ptr<Walnut::Image> motionIn,  // motion vectors, internal res, RG32F
                std::shared_ptr<Walnut::Image> colorOut,  // upscaled, display res, RGBA32F
                glm::vec2 jitter, float deltaTimeMs, bool resetHistory, float cameraNear, float cameraFar,
                float verticalFovRadians);

        auto IsInitialized() const { return m_Initialized; }
        auto GetStatus() const { return m_Status.c_str(); }

        auto GetQuality() const { return m_Quality; }
        auto GetSharpness() const { return m_Sharpness; }
        void SetSharpness(float sharpness) { m_Sharpness = sharpness; }

        auto GetRenderWidth() const { return m_RenderW; }
        auto GetRenderHeight() const { return m_RenderH; }

    private:
        // FSR context
        ffxContext m_Context{ nullptr };
        bool m_Initialized{ false };
        Quality m_Quality{ Quality::Quality };
        float m_Sharpness{ 0.0f };
        uint32_t m_DisplayW{ 0 }, m_DisplayH{ 0 };
        uint32_t m_RenderW{ 0 }, m_RenderH{ 0 };

        // FSR3 shared resources
        std::shared_ptr<Walnut::Image> m_DilatedDepth;
        std::shared_ptr<Walnut::Image> m_DilatedMotionVectors;
        std::shared_ptr<Walnut::Image> m_ReconstructedPrevNearestDepth;

        VkDevice m_Device{ VK_NULL_HANDLE };
        VkPhysicalDevice m_PhysDevice{ VK_NULL_HANDLE };
        std::string m_Status{ "Not initialized" };
    };
}  // namespace Vlkrt
