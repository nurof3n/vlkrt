#pragma once
#include <bit>

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>
#include "Walnut/Image.h"

// FidelityFX API includes
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>

namespace Vlkrt
{
    class FSRUpscaler
    {
    public:
        enum class Quality { NativeAA = 0, Quality = 1, Balanced = 2, Performance = 3, UltraPerformance = 4 };

        FSRUpscaler();
        ~FSRUpscaler();

        void Initialize(VkDevice device, VkPhysicalDevice physDev, VkQueue queue, uint32_t queueFamilyIdx);
        void Shutdown();

        // Call on window resize
        void OnResize(uint32_t displayW, uint32_t displayH, Quality q, float sharpness);

        // Returns the render (internal) resolution for a given display res + quality
        static void GetRenderResolution(uint32_t displayW, uint32_t displayH, Quality q,
                                        uint32_t& renderW, uint32_t& renderH);

        // Per-frame upscale dispatch
        void Dispatch(VkCommandBuffer cmd,
                      std::shared_ptr<Walnut::Image> colorIn,   // denoised, internal res, RGBA32F
                      std::shared_ptr<Walnut::Image> depthIn,   // NDC depth, internal res, R32F
                      std::shared_ptr<Walnut::Image> motionIn,  // motion vectors, internal res, RG32F
                      std::shared_ptr<Walnut::Image> colorOut,  // upscaled, display res, RGBA32F
                      glm::vec2 jitter,
                      float deltaTimeMs,
                      bool resetHistory,
                      float cameraNear,
                      float cameraFar,
                      float verticalFovRadians);

        bool IsInitialized() const { return m_Initialized; }
        const char* GetStatus() const { return m_Status.c_str(); }

        Quality GetQuality() const { return m_Quality; }
        float GetSharpness() const { return m_Sharpness; }

        uint32_t GetRenderWidth() const { return m_RenderW; }
        uint32_t GetRenderHeight() const { return m_RenderH; }

    private:
        // FSR context
        ffxContext              m_Context{ nullptr };
        bool                    m_Initialized{ false };
        Quality                 m_Quality{ Quality::Quality };
        float                   m_Sharpness{ 0.0f };
        uint32_t                m_DisplayW{ 0 }, m_DisplayH{ 0 };
        uint32_t                m_RenderW{ 0 }, m_RenderH{ 0 };

        // FSR3 shared resources
        std::shared_ptr<Walnut::Image> m_DilatedDepth;
        std::shared_ptr<Walnut::Image> m_DilatedMotionVectors;
        std::shared_ptr<Walnut::Image> m_ReconstructedPrevNearestDepth;

        VkDevice                m_Device{ VK_NULL_HANDLE };
        VkPhysicalDevice        m_PhysDevice{ VK_NULL_HANDLE };
        std::string             m_Status{ "Not initialized" };
    };
}
