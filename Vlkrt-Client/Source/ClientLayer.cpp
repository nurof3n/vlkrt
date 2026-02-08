#include "ClientLayer.h"

#include "Walnut/Application.h"
#include "Walnut/Input/Input.h"
#include "Walnut/ImGui/ImGuiTheme.h"
#include "Walnut/Timer.h"

#include "Walnut/Serialization/BufferStream.h"
#include "ServerPacket.h"

#include <glm/gtc/type_ptr.hpp>

#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

namespace Vlkrt
{
    static Walnut::Buffer s_ScratchBuffer{};

    ClientLayer::ClientLayer() : m_Camera(45.0f, 0.1f, 100.0f)
    {}

    void ClientLayer::OnAttach()
    {
        s_ScratchBuffer.Allocate(10 * 1024 * 1024);  // 10 MB

        m_Client.SetDataReceivedCallback([this](const Walnut::Buffer& buffer) { OnDataReceived(buffer); });

        // Setup raytracing scene materials
        // Material for current player (red)
        Material& playerMat = m_Scene.Materials.emplace_back();
        playerMat.Albedo    = { 1.0f, 0.2f, 0.2f };
        playerMat.Roughness = 0.3f;

        // Material for other players (green)
        Material& otherPlayerMat = m_Scene.Materials.emplace_back();
        otherPlayerMat.Albedo    = { 0.2f, 1.0f, 0.2f };
        otherPlayerMat.Roughness = 0.3f;

        // Material for ground
        Material& groundMat = m_Scene.Materials.emplace_back();
        groundMat.Albedo    = { 0.3f, 0.3f, 0.4f };
        groundMat.Roughness = 0.8f;

        // Add ground sphere
        Sphere ground;
        ground.Position      = { 0.0f, -1000.5f, 0.0f };
        ground.Radius        = 1000.0f;
        ground.MaterialIndex = 2;  // ground material
        m_Scene.Spheres.push_back(ground);
    }

    void ClientLayer::OnDetach()
    {}

    void ClientLayer::OnUpdate(float ts)
    {
        // Network player movement
        glm::vec2 dir{ 0.0f };
        if (Walnut::Input::IsKeyDown(Walnut::KeyCode::W))
            dir.y = -1.0f;
        else if (Walnut::Input::IsKeyDown(Walnut::KeyCode::S))
            dir.y = 1.0f;

        if (Walnut::Input::IsKeyDown(Walnut::KeyCode::A))
            dir.x = -1.0f;
        else if (Walnut::Input::IsKeyDown(Walnut::KeyCode::D))
            dir.x = 1.0f;

        if (dir.x != 0.0f || dir.y != 0.0f) {
            dir              = glm::normalize(dir);
            m_PlayerVelocity = dir * m_Speed;
        }
        else
            m_PlayerVelocity = {};

        m_PlayerPosition += m_PlayerVelocity * ts;

        // Client update
        if (m_Client.GetConnectionStatus() == Walnut::Client::ConnectionStatus::Connected) {
            Walnut::BufferStreamWriter stream(s_ScratchBuffer);
            stream.WriteRaw(PacketType::ClientUpdate);
            stream.WriteRaw<glm::vec2>(m_PlayerPosition);
            stream.WriteRaw<glm::vec2>(m_PlayerVelocity);
            m_Client.SendBuffer(stream.GetBuffer());
        }

        // Camera update (only when not controlling player movement)
        m_Camera.OnUpdate(ts);

        // Update scene with network player positions
        UpdateScene();
    }

    void ClientLayer::OnRender()
    {
        if (m_Client.GetConnectionStatus() == Walnut::Client::ConnectionStatus::Connected) {
            Walnut::Timer timer;
            m_Renderer.Render(m_Scene, m_Camera);
            m_LastRenderTime = timer.ElapsedMillis();
        }
    }

    void ClientLayer::OnUIRender()
    {
        auto connectionStatus = m_Client.GetConnectionStatus();
        if (connectionStatus == Walnut::Client::ConnectionStatus::Connected) {
            // Get the ImGui viewport size (accounts for dockspace/menus)
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            m_ViewportWidth         = (uint32_t) viewport->Size.x;
            m_ViewportHeight        = (uint32_t) viewport->Size.y;

            // Render at half resolution for performance
            uint32_t renderWidth  = m_ViewportWidth / 2;
            uint32_t renderHeight = m_ViewportHeight / 2;

            m_Renderer.OnResize(renderWidth, renderHeight);
            m_Camera.OnResize(renderWidth, renderHeight);

            // Render raytraced image as background
            auto image = m_Renderer.GetFinalImage();
            if (image) {
                ImGui::GetBackgroundDrawList()->AddImage(image->GetDescriptorSet(), viewport->Pos,
                        ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), ImVec2(0, 1),
                        ImVec2(1, 0));
            }

            // Settings panel overlay
            ImGui::Begin("Settings");
            ImGui::Text("Last render: %.3fms", m_LastRenderTime);
            ImGui::Text("Player ID: %u", m_PlayerID);
            ImGui::Text("Players: %zu", m_PlayerData.size());
            ImGui::End();
        }
        else {
            auto readOnly = (connectionStatus == Walnut::Client::ConnectionStatus::Connecting);

            ImGui::Begin("Connect to Server");

            ImGui::InputText("Server Address", &m_ServerAddress, readOnly ? ImGuiInputTextFlags_ReadOnly : 0);
            if (connectionStatus == Walnut::Client::ConnectionStatus::Connecting) {
                ImGui::TextColored(ImColor(Walnut::UI::Colors::Theme::textDarker), "Connecting to server...");
            }
            else if (connectionStatus == Walnut::Client::ConnectionStatus::FailedToConnect) {
                ImGui::TextColored(ImColor(Walnut::UI::Colors::Theme::error), "Failed to connect to server.");
            }
            if (ImGui::Button("Connect")) {
                m_Client.ConnectToServer(m_ServerAddress);
            }

            ImGui::End();
        }
    }

    void ClientLayer::UpdateScene()
    {
        // Keep only the ground sphere (index 0)
        m_Scene.Spheres.resize(1);

        // Convert 2D positions to 3D world space for raytracing
        // Map 2D game space (0-800 or similar) to 3D space
        const float scale        = 0.01f;  // Scale down from screen space
        const float sphereRadius = 0.5f;

        // Add current player
        Sphere playerSphere;
        playerSphere.Position = { (m_PlayerPosition.x - 400.0f) * scale, 0.0f, (m_PlayerPosition.y - 300.0f) * scale };
        playerSphere.Radius   = sphereRadius;
        playerSphere.MaterialIndex = 0;  // red material
        m_Scene.Spheres.push_back(playerSphere);

        // Add other players
        m_PlayerDataMutex.lock();
        for (const auto& [playerID, playerData] : m_PlayerData) {
            if (playerID == m_PlayerID)
                continue;

            Sphere otherSphere;
            otherSphere.Position
                    = { (playerData.Position.x - 400.0f) * scale, 0.0f, (playerData.Position.y - 300.0f) * scale };
            otherSphere.Radius        = sphereRadius;
            otherSphere.MaterialIndex = 1;  // green material
            m_Scene.Spheres.push_back(otherSphere);
        }
        m_PlayerDataMutex.unlock();
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
            case PacketType::ClientUpdate:
                m_PlayerDataMutex.lock();
                stream.ReadMap(m_PlayerData);
                m_PlayerDataMutex.unlock();
                break;
            default:
                WL_WARN_TAG("Client", "Received unknown packet type: {}", (int) type);
                break;
        }
    }
};  // namespace Vlkrt