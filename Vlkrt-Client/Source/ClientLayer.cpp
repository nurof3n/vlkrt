#include "ClientLayer.h"
#include "ServerPacket.h"
#include "SceneLoader.h"
#include "ScriptEngine.h"
#include "Utils.h"

#include "Walnut/Application.h"
#include "Walnut/Input/Input.h"
#include "Walnut/ImGui/ImGuiTheme.h"
#include "Walnut/Timer.h"
#include "Walnut/Serialization/BufferStream.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <GLFW/glfw3.h>
#include <filesystem>
#include <algorithm>


namespace Vlkrt
{
    static Walnut::Buffer s_ScratchBuffer{};

    void ClientLayer::OnAttach()
    {
        ScriptEngine::Init();
        RefreshResources();
        s_ScratchBuffer.Allocate(10 * 1024 * 1024);  // 10 MB

        m_Client.SetDataReceivedCallback([this](const Walnut::Buffer& buffer) { OnDataReceived(buffer); });

        LoadScene("cornell_box");
    }

    void ClientLayer::RefreshResources()
    {
        m_AvailableTextures.clear();
        m_AvailableModels.clear();
        m_AvailableScenes.clear();
        m_AvailableScripts.clear();

        auto scanDirectory = [](const std::string& path, std::vector<std::string>& list,
                                     const std::vector<std::string>& extensions, bool stripExtension = false) {
            if (!std::filesystem::exists(path)) return;
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (extensions.empty()
                            || std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                        if (stripExtension)
                            list.push_back(entry.path().stem().string());
                        else
                            list.push_back(entry.path().filename().string());
                    }
                }
            }
            std::sort(list.begin(), list.end());
        };

        scanDirectory(TEXTURES_DIR, m_AvailableTextures, { ".jpg", ".jpeg", ".png", ".tga", ".hdr" });
        scanDirectory(MODELS_DIR, m_AvailableModels, { ".obj", ".gltf", ".glb" });
        scanDirectory(SCENES_DIR, m_AvailableScenes, { ".yaml", ".yml" }, true);
        scanDirectory(SCRIPTS_DIR, m_AvailableScripts, { ".lua" });
    }

    void ClientLayer::OnDetach() { ScriptEngine::Shutdown(); }

    void ClientLayer::OnUpdate(float ts)
    {
        if (!m_TexturesLoaded) {
            m_Renderer.PreloadTextures(m_AvailableTextures);
            m_TexturesLoaded = true;
        }

        RunScripts(m_SceneRoot, ts);

        bool cameraControlMode = Walnut::Input::IsMouseButtonDown(Walnut::MouseButton::Right);
        if (cameraControlMode) {
            // When holding right-click: camera moves with WASD, player stays still
            m_PlayerVelocity = {};
            m_Camera.OnUpdate(ts);
        }
        else {
            // Only process WASD input if ImGui doesn't want keyboard focus
            glm::vec3 dir{ 0.0f };
            if (!ImGui::GetIO().WantCaptureKeyboard) {
                if (Walnut::Input::IsKeyDown(Walnut::KeyCode::W))
                    dir.z = -1.0f;
                else if (Walnut::Input::IsKeyDown(Walnut::KeyCode::S))
                    dir.z = 1.0f;

                if (Walnut::Input::IsKeyDown(Walnut::KeyCode::A))
                    dir.x = -1.0f;
                else if (Walnut::Input::IsKeyDown(Walnut::KeyCode::D))
                    dir.x = 1.0f;
            }

            bool playerMoving = (dir.x != 0.0f || dir.z != 0.0f);

            if (playerMoving) {
                dir              = glm::normalize(dir);
                m_PlayerVelocity = dir * m_Speed;
            }
            else
                m_PlayerVelocity = {};

            m_PlayerPosition += m_PlayerVelocity * ts;

            // Camera only moves when player is not moving
            if (!playerMoving) { m_Camera.OnUpdate(ts); }
        }

        // Client update
        if (m_Client.GetConnectionStatus() == Walnut::Client::ConnectionStatus::Connected) {
            Walnut::BufferStreamWriter stream(s_ScratchBuffer);
            stream.WriteRaw(PacketType::ClientUpdate);
            stream.WriteRaw<glm::vec3>(m_PlayerPosition);
            stream.WriteRaw<glm::vec3>(m_PlayerVelocity);
            m_Client.SendBuffer(stream.GetBuffer());
        }

        // Only update network-driven dynamic scene content for the demo scene.
        // Large static scenes (e.g. Sponza) should not rebuild RT scene data every network tick.
        const bool enableNetworkSceneUpdates = (m_CurrentScene != "sponza");

        // Only update scene if something actually changed
        size_t currentPlayerCount = m_PlayerData.size();
        if (enableNetworkSceneUpdates
                && (m_PlayerPosition != m_LastPlayerPosition || currentPlayerCount != m_LastPlayerCount
                        || m_NetworkDataChanged)) {
            UpdateScene();
            m_Renderer.InvalidateScene();
            m_LastPlayerPosition = m_PlayerPosition;
            m_LastPlayerCount    = currentPlayerCount;
            m_NetworkDataChanged = false;
        }
        else if (!enableNetworkSceneUpdates) {
            // Consume network updates without forcing costly scene rebuilds.
            m_LastPlayerPosition = m_PlayerPosition;
            m_LastPlayerCount    = currentPlayerCount;
            m_NetworkDataChanged = false;
        }

        // Sync hierarchy changes to flat arrays; only invalidate if data changed
        m_SceneDirty = false;
        FlattenHierarchyToScene(m_SceneRoot, glm::mat4(1.0f));
        if (m_SceneDirty) m_Renderer.InvalidateScene();
    }

    void ClientLayer::OnRender()
    {
        if (m_Client.GetConnectionStatus() == Walnut::Client::ConnectionStatus::Connected) [[likely]] {
            Walnut::Timer timer;
            m_Renderer.OnFSRSettingsChanged(m_Scene.EnableFSR, m_Scene.FSRQualityMode, m_Scene.FSRSharpness);
            m_Renderer.Render(m_Scene, m_Camera);
            m_LastRenderTime = timer.ElapsedMillis();
        }
    }

    void ClientLayer::OnUIRender()
    {
        if (m_Client.GetConnectionStatus() == Walnut::Client::ConnectionStatus::Connected) [[likely]] {
            ImGuiViewport* viewport    = ImGui::GetMainViewport();
            uint32_t newViewportWidth  = (uint32_t) viewport->Size.x;
            uint32_t newViewportHeight = (uint32_t) viewport->Size.y;

            // Check if we need to resize
            if (newViewportWidth != m_ViewportWidth || newViewportHeight != m_ViewportHeight) {
                m_ViewportWidth  = newViewportWidth;
                m_ViewportHeight = newViewportHeight;
                m_Renderer.OnResize(m_ViewportWidth, m_ViewportHeight);
                m_Camera.OnResize(m_ViewportWidth, m_ViewportHeight);
            }

            // Render selected output as background (final image or NRD guide debug views).
            std::shared_ptr<Walnut::Image> image = m_Renderer.GetFinalImage();
            if (m_Scene.EnableFSR && m_Scene.NRDGuideDebugView == NRDGuideDebugViewMode::FinalImage) {
                auto upscaled = m_Renderer.GetUpscaledImage();
                if (upscaled) image = upscaled;
            }
            if (!m_Scene.EnableNRDDenoiser) {
                switch (m_Scene.NRDGuideDebugView) {
                    case NRDGuideDebugViewMode::NormalRoughness: image = m_Renderer.GetGuideNormalRoughness(); break;
                    case NRDGuideDebugViewMode::ViewZ: image = m_Renderer.GetGuideViewZ(); break;
                    case NRDGuideDebugViewMode::MotionVectors: image = m_Renderer.GetGuideMotionVectors(); break;
                    case NRDGuideDebugViewMode::DiffRadianceHitDist:
                        image = m_Renderer.GetGuideDiffRadianceHitDist();
                        break;
                    case NRDGuideDebugViewMode::SpecRadianceHitDist:
                        image = m_Renderer.GetGuideSpecRadianceHitDist();
                        break;
                    case NRDGuideDebugViewMode::FinalImage:
                    default: break;
                }
            }
            if (image) {
                ImGui::GetBackgroundDrawList()->AddImage(image->GetDescriptorSet(), viewport->Pos,
                        ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), ImVec2(0, 0),
                        ImVec2(1, 1));
            }

            // Stats panel overlay
            ImGui::Begin("Stats");
            ImGui::Text("Player ID: %u", m_PlayerID);
            ImGui::Text("Players: %zu", m_PlayerData.size());
            ImGui::Text("Camera Pos: (%.2f, %.2f, %.2f)", m_Camera.GetPosition().x, m_Camera.GetPosition().y,
                    m_Camera.GetPosition().z);
            ImGui::Text("Camera Forward: (%.2f, %.2f, %.2f)", m_Camera.GetDirection().x, m_Camera.GetDirection().y,
                    m_Camera.GetDirection().z);
            ImGui::Separator();

            const auto& passStats = m_Renderer.GetLastPassStats();
            ImGui::Text("Resolution: %ux%u", passStats.Width, passStats.Height);
            ImGui::Text("Estimated GPU Memory: %.2f MB", passStats.EstimatedGraphicsMemoryMB);
            ImGui::Separator();
            ImGui::Text("Last render: %.3fms", m_LastRenderTime);
            ImGui::Separator();
            ImGui::Text("Frame Total: %.3f ms", passStats.FrameTotalMs);
            ImGui::Text("Scene Setup: %.3f ms", passStats.SceneSetupMs);
            ImGui::Text("UBO Upload: %.3f ms", passStats.UBOUploadMs);
            ImGui::Text("RT (GPU): %.3f ms", passStats.RayTraceGpuMs);
            if (passStats.NRDEnabled) { ImGui::Text("NRD (GPU): %.3f ms", passStats.NRDGpuMs); }
            if (passStats.FSREnabled) { ImGui::Text("FSR (GPU): %.3f ms", passStats.FSRGpuMs); }
            ImGui::Text("Cmd Submit+Wait: %.3f ms", passStats.CommandSubmitMs);

            const auto& denoiseMetrics = m_Renderer.GetDenoiseComparisonMetrics();
            if (m_Scene.EnableNRDDenoiser && m_Scene.EnableDenoiseMetrics && denoiseMetrics.Valid) {
                ImGui::Separator();
                ImGui::Text(m_Renderer.HasReferenceImage() ? "Denoised vs Reference" : "Denoised vs Raw");
                ImGui::Text("Samples: %u", denoiseMetrics.SampleCount);
                ImGui::Text("MSE: %.6f", denoiseMetrics.LumaMSE);
                ImGui::Text("RMSE: %.6f", denoiseMetrics.LumaRMSE);
                ImGui::Text("PSNR: %.2f dB", denoiseMetrics.LumaPSNR);
                ImGui::Text("Mean Abs Diff: %.6f", denoiseMetrics.LumaMeanAbsDiff);

                if (denoiseMetrics.RawValid) {
                    ImGui::Separator();
                    ImGui::Text("Raw vs Reference");
                    ImGui::Text("Samples: %u", denoiseMetrics.RawSampleCount);
                    ImGui::Text("MSE: %.6f", denoiseMetrics.RawLumaMSE);
                    ImGui::Text("RMSE: %.6f", denoiseMetrics.RawLumaRMSE);
                    ImGui::Text("PSNR: %.2f dB", denoiseMetrics.RawLumaPSNR);
                    ImGui::Text("Mean Abs Diff: %.6f", denoiseMetrics.RawLumaMeanAbsDiff);
                }
            }
            ImGui::End();

            // Raytracing settings panel
            ImGui::Begin("Raytracing Settings");
            {
                // Rendering mode
                const char* modeNames[] = { "Path Tracing", "Path Tracing Temporal" };
                int rtMode              = (m_Scene.RaytracingType == RaytracingMode::PathTracingTemporal) ? 1 : 0;
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::Combo("Mode", &rtMode, modeNames, 2)) {
                    m_Scene.RaytracingType
                            = (rtMode == 1) ? RaytracingMode::PathTracingTemporal : RaytracingMode::PathTracing;
                    m_Renderer.ResetAccumulation();
                }

                // Importance sampling
                const char* isNames[] = { "Uniform", "Cosine", "BSDF" };
                int isMode            = (int) m_Scene.ImportanceSampling;
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::Combo("Importance Sampling", &isMode, isNames, 3)) {
                    m_Scene.ImportanceSampling = (ImportanceSamplingMode) isMode;
                    m_Renderer.ResetAccumulation();
                }

                ImGui::Separator();

                // Path tracing parameters
                static constexpr int k_MaxRayDepth = 12;
                int maxDepth                       = (int) m_Scene.MaxRecursionDepth;
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::SliderInt("Max Ray Depth", &maxDepth, 1, k_MaxRayDepth)) {
                    m_Scene.MaxRecursionDepth = (uint32_t) maxDepth;
                    uint32_t maxAllowedShadow = m_Scene.MaxRecursionDepth;
                    if (m_Scene.MaxShadowRecursionDepth > maxAllowedShadow)
                        m_Scene.MaxShadowRecursionDepth = maxAllowedShadow;

                    uint32_t maxAllowedRR = (m_Scene.MaxRecursionDepth > 1u) ? (m_Scene.MaxRecursionDepth - 1u) : 1u;
                    if (m_Scene.RussianRouletteDepth > maxAllowedRR) m_Scene.RussianRouletteDepth = maxAllowedRR;
                    m_Renderer.ResetAccumulation();
                }

                int maxShadowDepth = (int) m_Scene.MaxShadowRecursionDepth;
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::SliderInt("Max Shadow Depth", &maxShadowDepth, 1, (int) m_Scene.MaxRecursionDepth)) {
                    m_Scene.MaxShadowRecursionDepth = (uint32_t) maxShadowDepth;
                    m_Renderer.ResetAccumulation();
                }

                int sqrtSamples = (int) m_Scene.PathSqrtSamplesPerPixel;
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::SliderInt("Sqrt SPP", &sqrtSamples, 1, 4)) {
                    m_Scene.PathSqrtSamplesPerPixel = (uint32_t) sqrtSamples;
                    m_Renderer.ResetAccumulation();
                }

                int rrMaxDepth = std::max(1, (int) m_Scene.MaxRecursionDepth - 1);
                if ((int) m_Scene.RussianRouletteDepth > rrMaxDepth)
                    m_Scene.RussianRouletteDepth = (uint32_t) rrMaxDepth;

                int rrDepth = (int) m_Scene.RussianRouletteDepth;
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::SliderInt("Russian Roulette Depth", &rrDepth, 1, rrMaxDepth)) {
                    m_Scene.RussianRouletteDepth = (uint32_t) rrDepth;
                    m_Renderer.ResetAccumulation();
                }

                if (ImGui::Checkbox("Apply Jitter", &m_Scene.ApplyJitter)) m_Renderer.ResetAccumulation();
                if (ImGui::Checkbox("One Light Sample", &m_Scene.OnlyOneLightSample)) m_Renderer.ResetAccumulation();
                if (ImGui::Checkbox("Anisotropic BSDF", &m_Scene.AnisotropicBSDF)) m_Renderer.ResetAccumulation();

                ImGui::Separator();
                ImGui::Text("NRD Denoiser");
                if (ImGui::Checkbox("Enable NRD Denoiser", &m_Scene.EnableNRDDenoiser)) {
                    if (!m_Scene.EnableNRDDenoiser) m_Scene.NRDGuideDebugView = NRDGuideDebugViewMode::FinalImage;
                    m_Renderer.ResetAccumulation();
                }

                if (m_Scene.EnableNRDDenoiser) {
                    if (ImGui::Checkbox("Collect Denoise Metrics", &m_Scene.EnableDenoiseMetrics)) {
                        m_Renderer.ResetAccumulation();
                    }

                    static int referenceCaptureFrames = 64;
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::InputInt("Reference Frames", &referenceCaptureFrames, 1, 32)) {
                        referenceCaptureFrames = std::clamp(referenceCaptureFrames, 1, 2048);
                    }

                    if (ImGui::Button("Record Reference")) {
                        m_Renderer.StartReferenceCapture((uint32_t) referenceCaptureFrames);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear Reference")) { m_Renderer.ClearReferenceImage(); }

                    if (m_Renderer.IsReferenceCaptureInProgress()) {
                        uint32_t captured = m_Renderer.GetReferenceCaptureCapturedFrames();
                        uint32_t target   = m_Renderer.GetReferenceCaptureTargetFrames();
                        ImGui::Text("Recording reference: %u / %u", captured, target);
                    }
                    else {
                        ImGui::Text("Reference status: %s", m_Renderer.HasReferenceImage() ? "Ready" : "Not captured");
                    }

                    const char* nrdDebugViewNames[] = { "Final Image", "Guide: Normal + Roughness", "Guide: ViewZ",
                        "Guide: Motion Vectors", "Guide: Diffuse Radiance + HitDist",
                        "Guide: Specular Radiance + HitDist", "Raw (No Denoise)", "Split: Denoised | Raw" };
                    int nrdDebugViewMode            = (int) m_Scene.NRDGuideDebugView;
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::Combo("NRD Debug View", &nrdDebugViewMode, nrdDebugViewNames, 8)) {
                        m_Scene.NRDGuideDebugView = (NRDGuideDebugViewMode) nrdDebugViewMode;
                    }

                    if (ImGui::TreeNode("NRD Tuning")) {
                        ImGui::SetNextItemWidth(200.0f);
                        ImGui::SliderFloat(
                                "Min Material Diffuse", &m_Scene.NRDMinMaterialForDiffuse, 0.0f, 4.0f, "%.2f");
                        ImGui::SetNextItemWidth(200.0f);
                        ImGui::SliderFloat(
                                "Min Material Specular", &m_Scene.NRDMinMaterialForSpecular, 0.0f, 4.0f, "%.2f");
                        ImGui::SetNextItemWidth(200.0f);
                        ImGui::SliderFloat(
                                "Diffuse Prepass Radius", &m_Scene.NRDDiffusePrepassBlurRadius, 0.0f, 24.0f, "%.1f");
                        ImGui::SetNextItemWidth(200.0f);
                        ImGui::SliderFloat(
                                "Specular Prepass Radius", &m_Scene.NRDSpecularPrepassBlurRadius, 0.0f, 24.0f, "%.1f");

                        int diffHist = (int) m_Scene.NRDDiffuseMaxAccumulatedFrameNum;
                        ImGui::SetNextItemWidth(200.0f);
                        if (ImGui::SliderInt("Diffuse Max History", &diffHist, 1, 64)) {
                            m_Scene.NRDDiffuseMaxAccumulatedFrameNum = (uint32_t) diffHist;
                        }
                        int specHist = (int) m_Scene.NRDSpecularMaxAccumulatedFrameNum;
                        ImGui::SetNextItemWidth(200.0f);
                        if (ImGui::SliderInt("Specular Max History", &specHist, 1, 64)) {
                            m_Scene.NRDSpecularMaxAccumulatedFrameNum = (uint32_t) specHist;
                        }
                        int diffFastHist = (int) m_Scene.NRDDiffuseMaxFastAccumulatedFrameNum;
                        ImGui::SetNextItemWidth(200.0f);
                        if (ImGui::SliderInt("Diffuse Fast History", &diffFastHist, 1, 16)) {
                            m_Scene.NRDDiffuseMaxFastAccumulatedFrameNum = (uint32_t) diffFastHist;
                        }
                        int specFastHist = (int) m_Scene.NRDSpecularMaxFastAccumulatedFrameNum;
                        ImGui::SetNextItemWidth(200.0f);
                        if (ImGui::SliderInt("Specular Fast History", &specFastHist, 1, 16)) {
                            m_Scene.NRDSpecularMaxFastAccumulatedFrameNum = (uint32_t) specFastHist;
                        }

                        ImGui::SetNextItemWidth(200.0f);
                        ImGui::SliderFloat(
                                "Antilag Acceleration", &m_Scene.NRDAntilagAccelerationAmount, 0.0f, 1.0f, "%.2f");
                        ImGui::SetNextItemWidth(200.0f);
                        ImGui::SliderFloat(
                                "Antilag Spatial Sigma", &m_Scene.NRDAntilagSpatialSigmaScale, 0.1f, 8.0f, "%.2f");
                        ImGui::SetNextItemWidth(200.0f);
                        ImGui::SliderFloat(
                                "Antilag Temporal Sigma", &m_Scene.NRDAntilagTemporalSigmaScale, 0.05f, 2.0f, "%.2f");
                        ImGui::SetNextItemWidth(200.0f);
                        ImGui::SliderFloat("Antilag Reset", &m_Scene.NRDAntilagResetAmount, 0.0f, 1.0f, "%.2f");

                        ImGui::SetNextItemWidth(200.0f);
                        ImGui::SliderFloat(
                                "Disocclusion Threshold", &m_Scene.NRDDisocclusionThreshold, 0.001f, 0.050f, "%.3f");
                        ImGui::SetNextItemWidth(200.0f);
                        ImGui::SliderFloat("Disocclusion Threshold Alt", &m_Scene.NRDDisocclusionThresholdAlternate,
                                0.005f, 0.150f, "%.3f");

                        ImGui::TreePop();
                    }
                }

                ImGui::Separator();
                ImGui::Text("FSR Upscaling");
                bool fsrEnabled = m_Scene.EnableFSR;
                if (ImGui::Checkbox("Enable FSR", &fsrEnabled)) {
                    m_Scene.EnableFSR = fsrEnabled;
                    m_Renderer.ResetAccumulation();
                }
                if (m_Scene.EnableFSR) {
                    const char* fsrQualityNames[] = { "Native AA (1.0x)", "Quality (1.5x)", "Balanced (1.7x)",
                        "Performance (2.0x)", "Ultra Performance (3.0x)" };
                    int fsrQuality                = (int) m_Scene.FSRQualityMode;
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::Combo("FSR Quality Mode", &fsrQuality, fsrQualityNames, 5)) {
                        m_Scene.FSRQualityMode = (uint32_t) fsrQuality;
                        m_Renderer.ResetAccumulation();
                    }

                    float fsrSharpness = m_Scene.FSRSharpness;
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat("FSR Sharpness", &fsrSharpness, 0.0f, 1.0f)) {
                        m_Scene.FSRSharpness = fsrSharpness;
                        m_Renderer.ResetAccumulation();
                    }
                }
                ImGui::Separator();

                // PBR Showcase: adjustable sun direction


                // Demo: background color
                if (m_Scene.SceneIndex == SceneFactory::SCENE_DEMO) {
                    ImGui::Separator();
                    ImGui::Text("Background");
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::ColorEdit3("Color##bg", &m_Scene.BackgroundColor.x)) m_Renderer.ResetAccumulation();
                    ImGui::Separator();
                }
                else if (m_Scene.SceneIndex != SceneFactory::SCENE_PBR_SHOWCASE) {
                    // Generic background color for other scenes (e.g. Cornell Box / YAML)
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::ColorEdit3("Background", &m_Scene.BackgroundColor.x)) m_Renderer.ResetAccumulation();
                    ImGui::Separator();
                }

                if (ImGui::Button("Reset Temporal Accumulation")) m_Renderer.ResetAccumulation();

                // Show accumulated frame count in temporal mode
                if (m_Scene.RaytracingType == RaytracingMode::PathTracingTemporal) {
                    uint32_t frames = m_Renderer.GetAccumulatedFrameCount();
                    uint32_t spp    = m_Scene.PathSqrtSamplesPerPixel * m_Scene.PathSqrtSamplesPerPixel;
                    ImGui::Text("Accumulated frames: %u  (%.1f%% of %u SPP cycle)", frames,
                            (spp > 0 ? 100.0f * (frames % spp) / (float) spp : 0.0f), spp);
                }
            }
            ImGui::End();

            // Chat panel
            ImGuiRenderChatPanel();

            // Scene hierarchy editor panel
            ImGuiRenderSceneHierarchy();
        }
        else {
            auto connectionStatus = m_Client.GetConnectionStatus();
            auto readOnly         = (connectionStatus == Walnut::Client::ConnectionStatus::Connecting);

            ImGui::Begin("Connect to Server");

            ImGui::InputText("Server Address", &m_ServerAddress, readOnly ? ImGuiInputTextFlags_ReadOnly : 0);
            if (connectionStatus == Walnut::Client::ConnectionStatus::Connecting) {
                ImGui::TextColored(ImColor(Walnut::UI::Colors::Theme::textDarker), "Connecting to server...");
            }
            else if (connectionStatus == Walnut::Client::ConnectionStatus::FailedToConnect) {
                ImGui::TextColored(ImColor(Walnut::UI::Colors::Theme::error), "Failed to connect to server.");
            }
            if (ImGui::Button("Connect")) { m_Client.ConnectToServer(m_ServerAddress); }

            ImGui::End();
        }
    }

    void ClientLayer::UpdateScene()
    {
        // Clear only dynamic meshes
        m_Scene.DynamicMeshes.clear();

        // Add current player as cube mesh
        const float cubeSize     = 1.0f;
        Mesh playerMesh          = MeshLoader::GenerateCube(cubeSize);
        playerMesh.MaterialIndex = 0;
        glm::vec3 playerPos      = m_PlayerPosition + glm::vec3(0.0f, cubeSize * 0.5f, 0.0f);
        playerMesh.Transform     = glm::translate(glm::mat4(1.0f), playerPos);
        m_Scene.DynamicMeshes.push_back(playerMesh);

        // Add other players as cube meshes
        m_PlayerDataMutex.lock();
        for (const auto& [playerID, playerData] : m_PlayerData) {
            if (playerID == m_PlayerID) continue;

            Mesh otherPlayerMesh          = MeshLoader::GenerateCube(cubeSize);
            otherPlayerMesh.MaterialIndex = 1;
            glm::vec3 otherPos            = playerData.Position + glm::vec3(0.0f, cubeSize * 0.5f, 0.0f);
            otherPlayerMesh.Transform     = glm::translate(glm::mat4(1.0f), otherPos);
            m_Scene.DynamicMeshes.push_back(otherPlayerMesh);
        }
        m_PlayerDataMutex.unlock();
    }

    void ClientLayer::RunScripts(SceneEntity& entity, float ts)
    {
        if (!entity.ScriptPath.empty()) {
            if (!entity.ScriptInitialized) [[unlikely]]
                ScriptEngine::LoadScript(entity);

            ScriptEngine::CallOnUpdate(entity, ts);
        }

        for (auto& child : entity.Children) RunScripts(child, ts);
    }

    void ClientLayer::LoadScene(const std::string& sceneName)
    {
        auto [scene, root] = SceneLoader::LoadFromYAMLWithHierarchy(sceneName + ".yaml");
        m_Scene            = scene;
        m_SceneRoot        = root;

        // Re-link parent pointers after copy assignment.
        // LoadFromYAMLWithHierarchy builds pointers in a temporary hierarchy; once copied,
        // those addresses are stale and must be rebuilt for runtime editing logic.
        m_SceneRoot.Parent = nullptr;
        auto relinkParents = [&](auto&& self, SceneEntity& entity, SceneEntity* parent) -> void {
            entity.Parent = parent;
            for (auto& child : entity.Children) self(self, child, &entity);
        };
        for (auto& child : m_SceneRoot.Children) relinkParents(relinkParents, child, &m_SceneRoot);

        m_CurrentScene  = sceneName;
        m_SelectedScene = sceneName;

        // Apply camera hint from YAML scene_settings if present
        if (m_Scene.HasCameraHint) {
            m_Camera.SetPosition(m_Scene.CameraPosition);
            m_Camera.SetTarget(m_Scene.CameraTarget);
        }

        m_HierarchyMapping = SceneLoader::CreateMapping(m_SceneRoot, m_Scene);
        SyncSceneToHierarchy();
        m_Renderer.InvalidateSceneStructure();
        m_Renderer.ResetAccumulation();
    }

    void ClientLayer::LoadFactoryScene(uint32_t sceneIndex)
    {
        SceneFactory::CameraHint cam;
        switch (sceneIndex) {
            case SceneFactory::SCENE_CORNELL_BOX: m_Scene = SceneFactory::CreateCornellBox(&cam); break;
            case SceneFactory::SCENE_DEMO: m_Scene = SceneFactory::CreateDemo(&cam); break;
            case SceneFactory::SCENE_PBR_SHOWCASE: m_Scene = SceneFactory::CreatePbrShowcase(&cam); break;
            default: return;
        }
        // Move camera to canonical position
        m_Camera.SetPosition(cam.Eye);
        m_Camera.SetTarget(cam.Target);
        // Clear hierarchy — factory scenes have no YAML hierarchy
        m_SceneRoot        = SceneEntity{};
        m_HierarchyMapping = HierarchyMapping{};
        m_Renderer.InvalidateSceneStructure();
        m_Renderer.ResetAccumulation();
    }

    void ClientLayer::OnDataReceived(const Walnut::Buffer& data)
    {
        Walnut::BufferStreamReader stream(data);

        PacketType type;
        stream.ReadRaw(type);

        switch (type) {
            case PacketType::ClientConnect:
                stream.ReadRaw(m_PlayerID);
                WL_INFO_TAG("Client", "Connected to server with Player ID: {}", m_PlayerID);
                break;
            case PacketType::Message: {
                ChatMessage msg;
                msg.Deserialize(&stream, msg);
                m_ChatMutex.lock();
                m_ChatHistory.push_back(msg);
                // Keep history limited to 100 messages
                while (m_ChatHistory.size() > 100) { m_ChatHistory.pop_front(); }
                m_ChatMutex.unlock();
                break;
            }
            case PacketType::ClientUpdate:
                m_PlayerDataMutex.lock();
                stream.ReadMap(m_PlayerData);
                m_NetworkDataChanged = true;
                m_PlayerDataMutex.unlock();
                break;
            default: WL_WARN_TAG("Client", "Received unknown packet type: {}", (int) type); break;
        }
    }

    void ClientLayer::SyncSceneToHierarchy()
    {
        // Keep hierarchy transform/light orientation as authored in YAML. We only sync
        // procedural properties needed by the inspector from the loaded flat scene data.
        for (size_t i = 0; i < m_Scene.ProceduralEntities.size(); ++i) {
            if (i < m_HierarchyMapping.ProceduralIndexToEntity.size()) {
                SceneEntity* procEntity = m_HierarchyMapping.ProceduralIndexToEntity[i];
                if (procEntity) {
                    procEntity->ProceduralData.MaterialIndex = m_Scene.ProceduralEntities[i].MaterialIndex;
                    procEntity->ProceduralData.IsAnalytic    = m_Scene.ProceduralEntities[i].IsAnalytic;
                    procEntity->ProceduralData.PrimitiveType = m_Scene.ProceduralEntities[i].PrimitiveType;
                }
            }
        }
    }

    void ClientLayer::SaveScene()
    {
        // Update scene camera settings with current runtime camera state so they persist
        m_Scene.HasCameraHint  = true;
        m_Scene.CameraPosition = m_Camera.GetPosition();
        m_Scene.CameraTarget   = m_Camera.GetPosition() + m_Camera.GetDirection();

        // Hierarchy is already synced to flat arrays every frame in OnUpdate()
        // Just save to YAML
        SceneLoader::SaveToYAMLWithHierarchy(m_CurrentScene + ".yaml", m_Scene, m_SceneRoot);

        // Reload scene from file to verify save
        LoadScene(m_CurrentScene);
    }

    void ClientLayer::ImGuiRenderSceneHierarchy()
    {
        ImGui::Begin("Scene");

        ImGui::Text("Current Scene: %s", m_CurrentScene.c_str());

        if (ImGui::BeginCombo("##SceneSelector", m_SelectedScene.c_str())) {
            for (const auto& sceneName : m_AvailableScenes) {
                if (ImGui::Selectable(sceneName.c_str(), m_SelectedScene == sceneName)) {
                    m_SelectedScene = sceneName;
                    LoadScene(sceneName);
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Save Scene", ImVec2(-1, 0))) { SaveScene(); }
        if (ImGui::Button("Reload Scene", ImVec2(-1, 0))) { LoadScene(m_CurrentScene); }

        ImGui::Separator();
        for (auto& child : m_SceneRoot.Children) { ImGuiRenderEntity(child, glm::mat4(1.0f)); }

        ImGui::End();
    }

    void ClientLayer::ImGuiRenderEntity(SceneEntity& entity, const glm::mat4& parentWorldTransform)
    {
        // Compute world transform for this entity
        glm::mat4 worldTransform = entity.LocalTransform.GetWorldMatrix(parentWorldTransform);

        // Unique ID for this entity using its address
        auto idStr = std::to_string((uintptr_t) &entity);

        // Create tree node for this entity (collapsed by default)
        // Use entity pointer as unique ID so tree state persists during name edits
        // Printf-style formatting keeps label separate from ID for stability
        bool isOpen = ImGui::TreeNodeEx((void*) (uintptr_t) &entity, 0, "%s", entity.Name.c_str());

        if (isOpen) {
            // Editable entity name
            std::string nameLabel = "Name##" + idStr;
            ImGui::InputText(nameLabel.c_str(), &entity.Name);

            ImGui::Separator();

            // Transform controls (collapsed by default)
            if (ImGui::TreeNodeEx(("Transform##" + idStr).c_str())) {
                ImGuiRenderTransformControls(entity.LocalTransform, idStr);
                ImGui::TreePop();
            }

            // Entity-specific properties (collapsed by default)
            if (ImGui::TreeNodeEx(("Properties##" + idStr).c_str())) {
                ImGuiRenderEntityProperties(entity);
                ImGui::TreePop();
            }

            // Children section
            if (!entity.Children.empty()) {
                ImGui::Separator();
                ImGui::Text("Children:");
                ImGui::Indent();
                for (auto& child : entity.Children) { ImGuiRenderEntity(child, worldTransform); }
                ImGui::Unindent();
            }

            ImGui::TreePop();
        }
    }

    void ClientLayer::ImGuiRenderTransformControls(Transform& localTransform, const std::string& id)
    {
        // Position drag controls
        glm::vec3 pos = localTransform.Position;
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::DragFloat3(("Position##" + id).c_str(), &pos.x, 0.1f, -100.0f, 100.0f)) {
            localTransform.Position = pos;
        }

        // Rotation - display as direction for intuitive control
        glm::vec3 direction = glm::normalize(
                glm::vec3(glm::mat4_cast(localTransform.Rotation) * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::DragFloat3(("Direction##" + id).c_str(), &direction.x, 0.01f, -1.0f, 1.0f)) {
            direction                  = glm::normalize(direction);
            glm::vec3 defaultDirection = glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec3 axis             = glm::cross(defaultDirection, direction);
            float dot                  = glm::dot(defaultDirection, direction);

            if (glm::length(axis) > 0.001f) {
                float angle             = glm::acos(glm::clamp(dot, -1.0f, 1.0f));
                localTransform.Rotation = glm::angleAxis(angle, glm::normalize(axis));
            }
            else if (dot < 0.0f) {
                localTransform.Rotation = glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
            }
            else {
                localTransform.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
        }

        // Scale drag controls
        glm::vec3 scale = localTransform.Scale;
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::DragFloat3(("Scale##" + id).c_str(), &scale.x, 0.1f, 0.01f, 100.0f)) {
            localTransform.Scale = scale;
        }
    }

    void ClientLayer::ImGuiRenderEntityProperties(SceneEntity& entity)
    {
        auto idStr = std::to_string((uintptr_t) &entity);

        switch (entity.Type) {
            case EntityType::Light: {
                // Light type
                const char* lightTypes[] = { "Square", "Directional" };
                int selectedType         = static_cast<int>(entity.LightData.Type);
                if (ImGui::Combo(
                            ("Light Type##" + idStr).c_str(), &selectedType, lightTypes, IM_ARRAYSIZE(lightTypes))) {
                    entity.LightData.Type = static_cast<LightType>(selectedType);
                }

                // Emission colour
                if (ImGui::ColorEdit3(("Emission##" + idStr).c_str(), glm::value_ptr(entity.LightData.Emission)))
                    m_Renderer.ResetAccumulation();

                // Intensity control
                if (ImGui::DragFloat(("Intensity##" + idStr).c_str(), &entity.LightData.Intensity, 0.01f, 0.0f, 10.0f))
                    m_Renderer.ResetAccumulation();

                // Size for square lights
                if (entity.LightData.Type == LightType::Square) {
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::DragFloat(("Size##" + idStr).c_str(), &entity.LightData.Size, 0.05f, 0.01f, 50.0f))
                        m_Renderer.ResetAccumulation();
                }
                break;
            }

            case EntityType::Mesh: {
                // Material index
                int matIdx = entity.MeshData.MaterialIndex;
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::DragInt(("Material Index##" + idStr).c_str(), &matIdx, 1.0f, 0,
                            (int) m_Scene.Materials.size() - 1)) {
                    entity.MeshData.MaterialIndex = matIdx;
                }

                // Texture selector for assigned material
                if (matIdx >= 0 && matIdx < (int) m_Scene.Materials.size()) {
                    Material& mat = m_Scene.Materials[matIdx];

                    ImGui::Separator();
                    ImGui::Text("Material: %s", mat.Name.c_str());

                    // Disney BSDF properties
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::ColorEdit3(("Albedo##" + idStr).c_str(), glm::value_ptr(mat.Albedo)))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::ColorEdit3(("Emission##" + idStr).c_str(), glm::value_ptr(mat.Emission)))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::ColorEdit3(("Extinction##" + idStr).c_str(), glm::value_ptr(mat.Extinction)))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Roughness##" + idStr).c_str(), &mat.Roughness, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Metallic##" + idStr).c_str(), &mat.Metallic, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Subsurface##" + idStr).c_str(), &mat.Subsurface, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Anisotropic##" + idStr).c_str(), &mat.Anisotropic, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Sheen##" + idStr).c_str(), &mat.Sheen, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Sheen Tint##" + idStr).c_str(), &mat.SheenTint, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Specular Tint##" + idStr).c_str(), &mat.SpecularTint, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Spec. Trans.##" + idStr).c_str(), &mat.SpecularTransmission, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Clearcoat##" + idStr).c_str(), &mat.Clearcoat, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Clearcoat Gloss##" + idStr).c_str(), &mat.ClearcoatGloss, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Eta##" + idStr).c_str(), &mat.Eta, 1.0f, 3.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("At Distance##" + idStr).c_str(), &mat.AtDistance, 0.01f, 10.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Step Scale##" + idStr).c_str(), &mat.StepScale, 0.01f, 2.0f))
                        m_Renderer.ResetAccumulation();

                    // Tiling factor
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::DragFloat(("Tiling##" + idStr).c_str(), &mat.Tiling, 0.1f, 0.01f, 100.0f))
                        m_Renderer.ResetAccumulation();

                    ImGui::Text("Texture");

                    // Display current texture path
                    ImGui::Text("Current: %s", mat.TextureFilename.empty() ? "(none)" : mat.TextureFilename.c_str());

                    // Texture combo box (textures discovered at startup)
                    if (ImGui::BeginCombo(("Texture##" + idStr).c_str(),
                                mat.TextureFilename.empty() ? "(none)" : mat.TextureFilename.c_str())) {
                        if (ImGui::Selectable("(none)", mat.TextureFilename.empty())) {
                            mat.TextureFilename.clear();
                            m_Renderer.ResetAccumulation();
                        }
                        for (const auto& textureName : m_AvailableTextures) {
                            if (ImGui::Selectable(textureName.c_str(), mat.TextureFilename == textureName)) {
                                mat.TextureFilename = textureName;
                                m_Renderer.ResetAccumulation();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                if (ImGui::BeginCombo(("Mesh##" + idStr).c_str(), entity.MeshData.Filename.c_str())) {
                    for (const auto& modelName : m_AvailableModels) {
                        if (ImGui::Selectable(modelName.c_str(), entity.MeshData.Filename == modelName)) {
                            entity.MeshData.Filename = modelName;

                            // Load new mesh data and update the flat scene mesh
                            Mesh newMesh = MeshLoader::LoadOBJ(modelName);
                            auto it      = m_HierarchyMapping.EntityToMeshIdx.find(&entity);
                            if (it != m_HierarchyMapping.EntityToMeshIdx.end()) {
                                uint32_t meshIdx = it->second;
                                if (meshIdx < m_Scene.StaticMeshes.size()) {
                                    // Keep transform and material index, update geometry
                                    newMesh.Transform             = m_Scene.StaticMeshes[meshIdx].Transform;
                                    newMesh.MaterialIndex         = m_Scene.StaticMeshes[meshIdx].MaterialIndex;
                                    m_Scene.StaticMeshes[meshIdx] = newMesh;
                                }
                            }

                            m_Renderer.ResetAccumulation();
                        }
                    }
                    ImGui::EndCombo();
                }
                break;
            }

            case EntityType::Procedural: {
                int matIdx = entity.ProceduralData.MaterialIndex;
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::DragInt(("Material Index##" + idStr).c_str(), &matIdx, 1.0f, 0,
                            (int) m_Scene.Materials.size() - 1)) {
                    entity.ProceduralData.MaterialIndex = matIdx;
                    m_Renderer.ResetAccumulation();
                }

                if (matIdx >= 0 && matIdx < (int) m_Scene.Materials.size()) {
                    Material& mat = m_Scene.Materials[matIdx];

                    ImGui::Separator();
                    ImGui::Text("Material: %s", mat.Name.c_str());

                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::ColorEdit3(("Albedo##" + idStr).c_str(), glm::value_ptr(mat.Albedo)))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::ColorEdit3(("Emission##" + idStr).c_str(), glm::value_ptr(mat.Emission)))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::ColorEdit3(("Extinction##" + idStr).c_str(), glm::value_ptr(mat.Extinction)))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Roughness##" + idStr).c_str(), &mat.Roughness, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Metallic##" + idStr).c_str(), &mat.Metallic, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Subsurface##" + idStr).c_str(), &mat.Subsurface, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Anisotropic##" + idStr).c_str(), &mat.Anisotropic, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Sheen##" + idStr).c_str(), &mat.Sheen, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Sheen Tint##" + idStr).c_str(), &mat.SheenTint, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Specular Tint##" + idStr).c_str(), &mat.SpecularTint, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Spec. Trans.##" + idStr).c_str(), &mat.SpecularTransmission, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Clearcoat##" + idStr).c_str(), &mat.Clearcoat, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Clearcoat Gloss##" + idStr).c_str(), &mat.ClearcoatGloss, 0.0f, 1.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Eta##" + idStr).c_str(), &mat.Eta, 1.0f, 3.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("At Distance##" + idStr).c_str(), &mat.AtDistance, 0.01f, 10.0f))
                        m_Renderer.ResetAccumulation();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat(("Step Scale##" + idStr).c_str(), &mat.StepScale, 0.01f, 2.0f))
                        m_Renderer.ResetAccumulation();
                }

                if (ImGui::Checkbox(("Analytic##" + idStr).c_str(), &entity.ProceduralData.IsAnalytic))
                    m_Renderer.ResetAccumulation();

                int primitiveType = (int) entity.ProceduralData.PrimitiveType;
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::DragInt(("Primitive Type##" + idStr).c_str(), &primitiveType, 1.0f, 0, 8)) {
                    entity.ProceduralData.PrimitiveType = (uint32_t) primitiveType;
                    m_Renderer.ResetAccumulation();
                }
                break;
            }

            case EntityType::Empty: {
                ImGui::Text("Empty group");
                break;
            }

            case EntityType::Camera: {
                ImGui::Text("Camera (not yet editable)");
                break;
            }

            default: break;
        }

        ImGui::Separator();
        ImGui::Text("Script");
        if (ImGui::BeginCombo(
                    ("Script##" + idStr).c_str(), entity.ScriptPath.empty() ? "(none)" : entity.ScriptPath.c_str())) {
            if (ImGui::Selectable("(none)", entity.ScriptPath.empty())) {
                entity.ScriptPath.clear();
                entity.ScriptInitialized = false;
            }
            for (const auto& scriptName : m_AvailableScripts) {
                if (ImGui::Selectable(scriptName.c_str(), entity.ScriptPath == scriptName)) {
                    entity.ScriptPath        = scriptName;
                    entity.ScriptInitialized = false;  // Force reload
                }
            }
            ImGui::EndCombo();
        }
    }

    void ClientLayer::FlattenHierarchyToScene(const SceneEntity& entity, const glm::mat4& parentWorld)
    {
        auto nearlyEqual     = [](float a, float b, float eps = 1e-5f) { return std::abs(a - b) <= eps; };
        auto vec3NearlyEqual = [&](const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) {
            return nearlyEqual(a.x, b.x, eps) && nearlyEqual(a.y, b.y, eps) && nearlyEqual(a.z, b.z, eps);
        };
        auto mat4NearlyEqual = [&](const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f) {
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    if (!nearlyEqual(a[c][r], b[c][r], eps)) return false;
                }
            }
            return true;
        };

        // Compute world transform for this entity
        glm::mat4 worldTransform = entity.LocalTransform.GetWorldMatrix(parentWorld);

        // Process based on entity type
        if (entity.Type == EntityType::Mesh) {
            // glTF entities are flattened into many static meshes at load time.
            // Do not map a single hierarchy transform back to one flat mesh index,
            // otherwise one imported sub-mesh gets an incorrect transform every frame.
            std::filesystem::path meshPath(entity.MeshData.Filename);
            std::string ext = meshPath.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const bool isGltfAggregate = (ext == ".gltf" || ext == ".glb");
            if (!isGltfAggregate) {
                // Find this entity in the mapping
                auto it = m_HierarchyMapping.EntityToMeshIdx.find(const_cast<SceneEntity*>(&entity));
                if (it != m_HierarchyMapping.EntityToMeshIdx.end()) {
                    uint32_t meshIdx = it->second;
                    if (meshIdx < m_Scene.StaticMeshes.size()) {
                        auto& mesh = m_Scene.StaticMeshes[meshIdx];
                        if (!mat4NearlyEqual(mesh.Transform, worldTransform)
                                || mesh.MaterialIndex != entity.MeshData.MaterialIndex) {
                            mesh.Transform     = worldTransform;
                            mesh.MaterialIndex = entity.MeshData.MaterialIndex;
                            m_SceneDirty       = true;
                            m_Renderer.MarkDirtyMeshes({ meshIdx });
                        }
                    }
                }
            }
        }
        else if (entity.Type == EntityType::Light) {
            // Find this entity in the mapping
            auto it = m_HierarchyMapping.EntityToLightIdx.find(const_cast<SceneEntity*>(&entity));
            if (it != m_HierarchyMapping.EntityToLightIdx.end()) {
                uint32_t lightIdx = it->second;
                if (lightIdx < m_Scene.Lights.size()) {
                    // Compute candidate values first
                    glm::vec3 newPos = glm::vec3(worldTransform[3]);
                    glm::vec3 newDir = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                    Light& light     = m_Scene.Lights[lightIdx];
                    if (!vec3NearlyEqual(light.Emission, entity.LightData.Emission)
                            || !nearlyEqual(light.Intensity, entity.LightData.Intensity)
                            || light.Type != entity.LightData.Type || !nearlyEqual(light.Size, entity.LightData.Size)
                            || !vec3NearlyEqual(light.Position, newPos) || !vec3NearlyEqual(light.Direction, newDir)) {
                        light.Emission  = entity.LightData.Emission;
                        light.Intensity = entity.LightData.Intensity;
                        light.Type      = entity.LightData.Type;
                        light.Size      = entity.LightData.Size;
                        light.Position  = newPos;
                        light.Direction = newDir;
                        m_SceneDirty    = true;
                        m_Renderer.MarkDirtyLights({ lightIdx });
                    }
                }
            }
        }
        else if (entity.Type == EntityType::Procedural) {
            auto it = m_HierarchyMapping.EntityToProceduralIdx.find(const_cast<SceneEntity*>(&entity));
            if (it != m_HierarchyMapping.EntityToProceduralIdx.end()) {
                uint32_t procIdx = it->second;
                if (procIdx < m_Scene.ProceduralEntities.size()) {
                    ProceduralEntity& pe  = m_Scene.ProceduralEntities[procIdx];
                    bool structureChanged = (pe.IsAnalytic != entity.ProceduralData.IsAnalytic)
                                            || (pe.PrimitiveType != entity.ProceduralData.PrimitiveType);
                    if (!mat4NearlyEqual(pe.Transform, worldTransform)
                            || pe.MaterialIndex != entity.ProceduralData.MaterialIndex
                            || pe.IsAnalytic != entity.ProceduralData.IsAnalytic
                            || pe.PrimitiveType != entity.ProceduralData.PrimitiveType) {
                        pe.Transform     = worldTransform;
                        pe.MaterialIndex = entity.ProceduralData.MaterialIndex;
                        pe.IsAnalytic    = entity.ProceduralData.IsAnalytic;
                        pe.PrimitiveType = entity.ProceduralData.PrimitiveType;
                        m_SceneDirty     = true;
                        m_Renderer.MarkDirtyMeshes({ procIdx });
                        if (structureChanged) m_Renderer.InvalidateSceneStructure();
                    }
                }
            }
        }
        // Empty and Camera types don't add to scene, just pass through

        // Recursively process children
        for (const auto& child : entity.Children) { FlattenHierarchyToScene(child, worldTransform); }
    }

    void ClientLayer::ImGuiRenderChatPanel()
    {
        if (ImGui::Begin("Chat", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::BeginChild("ChatHistory", ImVec2(400, 200), true);
            m_ChatMutex.lock();
            for (const auto& msg : m_ChatHistory) {
                ImGui::TextWrapped("%s: %s", msg.Username.c_str(), msg.Message.c_str());
            }
            m_ChatMutex.unlock();

            // Auto-scroll to bottom
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) { ImGui::SetScrollHereY(1.0f); }
            ImGui::EndChild();

            // Input field
            ImGui::Spacing();
            bool sendMessage = false;
            if (ImGui::InputText("##ChatInput", &m_ChatInputBuffer, ImGuiInputTextFlags_EnterReturnsTrue)) {
                sendMessage = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Send", ImVec2(80, 0))) { sendMessage = true; }

            if (sendMessage && !m_ChatInputBuffer.empty()) {
                SendChatMessage(m_ChatInputBuffer);
                m_ChatInputBuffer.clear();
                ImGui::SetKeyboardFocusHere(-1);  // Return focus to input
            }
        }
        ImGui::End();
    }

    void ClientLayer::SendChatMessage(const std::string& message)
    {
        if (m_Client.GetConnectionStatus() != Walnut::Client::ConnectionStatus::Connected) {
            WL_WARN_TAG("Client", "Cannot send message: not connected to server");
            return;
        }

        // Format username with player ID
        std::string usernameWithId = m_UserInfo.Username + " [" + std::to_string(m_PlayerID) + "]";

        Walnut::BufferStreamWriter stream(s_ScratchBuffer);
        stream.WriteRaw(PacketType::Message);
        stream.WriteString(usernameWithId);
        stream.WriteString(message);

        m_Client.SendBuffer(stream.GetBuffer());
        // Message will appear in chat when server broadcasts it back to all clients
    }
}  // namespace Vlkrt
