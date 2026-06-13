#include "FSRUpscaler.h"
#include "Walnut/Core/Log.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Vlkrt
{
    // Helper function to create FfxApiResource from Walnut::Image
    static FfxApiResource CreateFfxResource(std::shared_ptr<Walnut::Image> img, const wchar_t* name, uint32_t state)
    {
        if (!img) return ffxApiGetResourceVK(nullptr, {}, FFX_API_RESOURCE_STATE_COMMON);

        VkImageCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        
        switch (img->GetFormat())
        {
            case Walnut::ImageFormat::RGBA:
                info.format = VK_FORMAT_R8G8B8A8_UNORM;
                break;
            case Walnut::ImageFormat::RGBA32F:
                info.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
            case Walnut::ImageFormat::R32F:
                info.format = VK_FORMAT_R32_SFLOAT;
                break;
            case Walnut::ImageFormat::RG16F:
                info.format = VK_FORMAT_R16G16_SFLOAT;
                break;
            case Walnut::ImageFormat::R32UI:
                info.format = VK_FORMAT_R32_UINT;
                break;
            default:
                info.format = VK_FORMAT_R8G8B8A8_UNORM;
                break;
        }

        info.extent.width = img->GetWidth();
        info.extent.height = img->GetHeight();
        info.extent.depth = 1;
        info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(img->GetWidth(), img->GetHeight())))) + 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        FfxApiResourceDescription desc = ffxApiGetImageResourceDescriptionVK(img->GetVkImage(), info, 0);
        return ffxApiGetResourceVK(img->GetVkImage(), desc, state);
    }

    FSRUpscaler::FSRUpscaler()
    {
    }

    FSRUpscaler::~FSRUpscaler()
    {
        Shutdown();
    }

    void FSRUpscaler::Initialize(VkDevice device, VkPhysicalDevice physDev, VkQueue queue, uint32_t queueFamilyIdx)
    {
        m_Device = device;
        m_PhysDevice = physDev;
        m_Initialized = true;
        m_Status = "Initialized backend";
        WL_INFO_TAG("FSRUpscaler", "FidelityFX Vulkan backend initialized successfully.");
    }

    void FSRUpscaler::Shutdown()
    {
        if (!m_Initialized) return;

        if (m_Context != nullptr)
        {
            ffxDestroyContext(&m_Context, nullptr);
            m_Context = nullptr;
        }

        m_Initialized = false;
        m_Status = "Shutdown";
        WL_INFO_TAG("FSRUpscaler", "FSRUpscaler shutdown complete.");
    }

    void FSRUpscaler::GetRenderResolution(uint32_t displayW, uint32_t displayH, Quality q,
                                          uint32_t& renderW, uint32_t& renderH)
    {
        ffxQueryDescUpscaleGetRenderResolutionFromQualityMode queryDesc = {};
        queryDesc.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE;
        queryDesc.header.pNext = nullptr;
        queryDesc.displayWidth = displayW;
        queryDesc.displayHeight = displayH;
        queryDesc.qualityMode = static_cast<uint32_t>(q);
        queryDesc.pOutRenderWidth = &renderW;
        queryDesc.pOutRenderHeight = &renderH;

        if (ffxQuery(nullptr, &queryDesc.header) != FFX_API_RETURN_OK)
        {
            renderW = displayW;
            renderH = displayH;
        }
    }

    void FSRUpscaler::OnResize(uint32_t displayW, uint32_t displayH, Quality q, float sharpness)
    {
        if (!m_Initialized) return;

        if (m_DisplayW == displayW && m_DisplayH == displayH && m_Quality == q && m_Sharpness == sharpness)
            return;

        // Destroy context if changing sizes or settings
        if (m_Context != nullptr)
        {
            ffxDestroyContext(&m_Context, nullptr);
            m_Context = nullptr;
        }

        m_DisplayW = displayW;
        m_DisplayH = displayH;
        m_Quality = q;
        m_Sharpness = sharpness;

        GetRenderResolution(displayW, displayH, q, m_RenderW, m_RenderH);

        // Configure the Vulkan backend descriptor
        ffxCreateBackendVKDesc backendDesc = {};
        backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backendDesc.header.pNext = nullptr;
        backendDesc.vkDevice = m_Device;
        backendDesc.vkPhysicalDevice = m_PhysDevice;
        backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;

        // Configure the upscale descriptor
        ffxCreateContextDescUpscale desc = {};
        desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        desc.header.pNext = &backendDesc.header;
        desc.flags = FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION; // We pass LDR (tonemapped) inputs, so no HDR flag
        
        desc.maxRenderSize.width = m_RenderW;
        desc.maxRenderSize.height = m_RenderH;
        desc.maxUpscaleSize.width = m_DisplayW;
        desc.maxUpscaleSize.height = m_DisplayH;
        desc.fpMessage = nullptr;

        ffxReturnCode_t err = ffxCreateContext(&m_Context, &desc.header, nullptr);
        if (err != FFX_API_RETURN_OK)
        {
            m_Status = "Context creation failed: " + std::to_string(err);
            WL_ERROR_TAG("FSRUpscaler", "Failed to create FSR upscaler context, error: {}", (int)err);
        }
        else
        {
            m_Status = "Ready (" + std::to_string(m_RenderW) + "x" + std::to_string(m_RenderH) + " -> " + std::to_string(m_DisplayW) + "x" + std::to_string(m_DisplayH) + ")";
            WL_INFO_TAG("FSRUpscaler", "Created FSR upscaler context: {}x{} -> {}x{}", m_RenderW, m_RenderH, m_DisplayW, m_DisplayH);
        }
    }

    void FSRUpscaler::Dispatch(VkCommandBuffer cmd,
                               std::shared_ptr<Walnut::Image> colorIn,
                               std::shared_ptr<Walnut::Image> depthIn,
                               std::shared_ptr<Walnut::Image> motionIn,
                               std::shared_ptr<Walnut::Image> colorOut,
                               glm::vec2 jitter,
                               float deltaTimeMs,
                               bool resetHistory,
                               float cameraNear,
                               float cameraFar,
                               float verticalFovRadians)
    {
        if (!m_Initialized || m_Context == nullptr) return;

        ffxDispatchDescUpscale desc = {};
        desc.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        desc.header.pNext = nullptr;

        desc.commandList = cmd;
        desc.color = CreateFfxResource(colorIn, L"FSR3_InputColor", FFX_API_RESOURCE_STATE_COMPUTE_READ);
        desc.depth = CreateFfxResource(depthIn, L"FSR3_InputDepth", FFX_API_RESOURCE_STATE_COMPUTE_READ);
        desc.motionVectors = CreateFfxResource(motionIn, L"FSR3_InputMotionVectors", FFX_API_RESOURCE_STATE_COMPUTE_READ);
        
        // Optional reactive/exposure masks are null
        desc.exposure = ffxApiGetResourceVK(nullptr, {}, FFX_API_RESOURCE_STATE_COMMON);
        desc.reactive = ffxApiGetResourceVK(nullptr, {}, FFX_API_RESOURCE_STATE_COMMON);
        desc.transparencyAndComposition = ffxApiGetResourceVK(nullptr, {}, FFX_API_RESOURCE_STATE_COMMON);

        desc.output = CreateFfxResource(colorOut, L"FSR3_OutputColor", FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

        // Jitter should be in pixel units [-0.5, 0.5]
        desc.jitterOffset.x = jitter.x;
        desc.jitterOffset.y = jitter.y;

        // Motion vector scaling: convert motion from NDC to pixels
        desc.motionVectorScale.x = static_cast<float>(m_RenderW);
        desc.motionVectorScale.y = static_cast<float>(m_RenderH);

        desc.renderSize.width = m_RenderW;
        desc.renderSize.height = m_RenderH;
        desc.upscaleSize.width = m_DisplayW;
        desc.upscaleSize.height = m_DisplayH;

        desc.enableSharpening = m_Sharpness > 0.0f;
        desc.sharpness = m_Sharpness;

        desc.frameTimeDelta = deltaTimeMs;
        desc.preExposure = 1.0f;
        desc.reset = resetHistory;

        desc.cameraNear = cameraNear;
        desc.cameraFar = cameraFar;
        desc.cameraFovAngleVertical = verticalFovRadians;
        desc.viewSpaceToMetersFactor = 1.0f; // View space is already in meters conceptually
        desc.flags = 0;

        ffxReturnCode_t err = ffxDispatch(&m_Context, &desc.header);
        if (err != FFX_API_RETURN_OK)
        {
            WL_ERROR_TAG("FSRUpscaler", "ffxDispatch failed, error: {}", (int)err);
        }
    }
}
