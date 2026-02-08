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
        // Material for current player (green)
        Material& playerMat = m_Scene.Materials.emplace_back();
        playerMat.Albedo    = { 0.2f, 1.0f, 0.2f };
        playerMat.Roughness = 0.3f;

        // Material for other players (red)
        Material& otherPlayerMat = m_Scene.Materials.emplace_back();
        otherPlayerMat.Albedo    = { 1.0f, 0.2f, 0.2f };
        otherPlayerMat.Roughness = 0.3f;

        // Material for ground
        Material& groundMat = m_Scene.Materials.emplace_back();
        groundMat.Albedo    = { 0.3f, 0.3f, 0.4f };
        groundMat.Roughness = 0.8f;

        // Material for bunny (white)
        Material& bunnyMat = m_Scene.Materials.emplace_back();
        bunnyMat.Albedo    = { 1.0f, 1.0f, 1.0f };
        bunnyMat.Roughness = 0.5f;

        // Add ground plane as a large quad
        Mesh groundPlane          = MeshLoader::GenerateQuad(20.0f);
        groundPlane.MaterialIndex = 2;  // ground material
        groundPlane.Transform     = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
        m_Scene.StaticMeshes.push_back(groundPlane);

        // Load bunny OBJ as a static object
        Mesh bunnyMesh          = MeshLoader::LoadOBJ("../resources/obj/bunny.obj");
        bunnyMesh.MaterialIndex = 3;  // bunny material (white)
        // Scale and position the bunny - bunny is very small so needs large scaling
        bunnyMesh.Transform = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f))
                              * glm::scale(glm::mat4(1.0f), glm::vec3(5.0f));
        m_Scene.StaticMeshes.push_back(bunnyMesh);

        // Setup lights
        // Directional light
        Light dirLight;
        dirLight.Type      = 0.0f;  // Directional
        dirLight.Direction = glm::normalize(glm::vec3(1.0f, -1.0f, 1.0f));
        dirLight.Color     = glm::vec3(1.0f, 1.0f, 0.95f);  // Slight warming
        dirLight.Intensity = 0.8f;
        m_Scene.Lights.push_back(dirLight);

        // Point light
        Light pointLight;
        pointLight.Type      = 1.0f;  // Point
        pointLight.Position  = glm::vec3(5.0f, 5.0f, 5.0f);
        pointLight.Color     = glm::vec3(0.5f, 0.7f, 1.0f);  // Cool blue
        pointLight.Intensity = 1.0f;
        pointLight.Radius    = 20.0f;
        m_Scene.Lights.push_back(pointLight);
    }

    void ClientLayer::OnDetach()
    {}

    void ClientLayer::OnUpdate(float ts)
    {
        // Check if camera control mode (right-click held)
        bool cameraControlMode = Walnut::Input::IsMouseButtonDown(Walnut::MouseButton::Right);

        if (cameraControlMode) {
            // When holding right-click: camera moves with WASD, player stays still
            m_PlayerVelocity = {};
            m_Camera.OnUpdate(ts);
        }
        else {
            // Normal mode: WASD moves player, camera only moves on mouse drag
            glm::vec2 dir{ 0.0f };
            if (Walnut::Input::IsKeyDown(Walnut::KeyCode::W))
                dir.y = -1.0f;
            else if (Walnut::Input::IsKeyDown(Walnut::KeyCode::S))
                dir.y = 1.0f;

            if (Walnut::Input::IsKeyDown(Walnut::KeyCode::A))
                dir.x = -1.0f;
            else if (Walnut::Input::IsKeyDown(Walnut::KeyCode::D))
                dir.x = 1.0f;

            bool playerMoving = (dir.x != 0.0f || dir.y != 0.0f);

            if (playerMoving) {
                dir              = glm::normalize(dir);
                m_PlayerVelocity = dir * m_Speed;
            }
            else
                m_PlayerVelocity = {};

            m_PlayerPosition += m_PlayerVelocity * ts;

            // Camera only moves when player is not moving
            if (!playerMoving) {
                m_Camera.OnUpdate(ts);
            }
        }

        // Client update
        if (m_Client.GetConnectionStatus() == Walnut::Client::ConnectionStatus::Connected) {
            Walnut::BufferStreamWriter stream(s_ScratchBuffer);
            stream.WriteRaw(PacketType::ClientUpdate);
            stream.WriteRaw<glm::vec2>(m_PlayerPosition);
            stream.WriteRaw<glm::vec2>(m_PlayerVelocity);
            m_Client.SendBuffer(stream.GetBuffer());
        }

        // Only update scene if something actually changed
        size_t currentPlayerCount = m_PlayerData.size() + 1;  // +1 for local player
        if (m_PlayerPosition != m_LastPlayerPosition || currentPlayerCount != m_LastPlayerCount
                || m_NetworkDataChanged) {
            UpdateScene();
            m_Renderer.InvalidateScene();
            m_LastPlayerPosition = m_PlayerPosition;
            m_LastPlayerCount    = currentPlayerCount;
            m_NetworkDataChanged = false;
        }
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
            //   Get the ImGui viewport size (accounts for dockspace/menus)
            ImGuiViewport* viewport          = ImGui::GetMainViewport();
            uint32_t       newViewportWidth  = (uint32_t) viewport->Size.x;
            uint32_t       newViewportHeight = (uint32_t) viewport->Size.y;

            // Render at half resolution for performance
            uint32_t renderWidth  = newViewportWidth / 2;
            uint32_t renderHeight = newViewportHeight / 2;

            // Only resize if viewport size changed (avoid expensive reallocation every frame)
            if (newViewportWidth != m_ViewportWidth || newViewportHeight != m_ViewportHeight) {
                m_ViewportWidth  = newViewportWidth;
                m_ViewportHeight = newViewportHeight;
                m_Renderer.OnResize(renderWidth, renderHeight);
                m_Camera.OnResize(renderWidth, renderHeight);
            }

            // Render raytraced image as background
            auto image = m_Renderer.GetFinalImage();
            if (image) {
                ImGui::GetBackgroundDrawList()->AddImage(image->GetDescriptorSet(), viewport->Pos,
                        ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), ImVec2(0, 1),
                        ImVec2(1, 0));
            }

            // Stats panel overlay
            ImGui::Begin("Stats");
            ImGui::Text("Last render: %.3fms", m_LastRenderTime);
            ImGui::Text("Player ID: %u", m_PlayerID);
            ImGui::Text("Players: %zu", m_PlayerData.size());
            ImGui::End();

            // Lighting controls panel
            ImGui::Begin("Lighting");
            ImGui::Text("Directional Light");
            ImGui::Separator();
            ImGui::SliderFloat("Dir Intensity##dir", &m_Scene.Lights[0].Intensity, 0.0f, 2.0f);
            ImGui::ColorEdit3("Dir Color##dir", &m_Scene.Lights[0].Color[0]);
            ImGui::SliderFloat("Dir X##x", &m_Scene.Lights[0].Direction.x, -1.0f, 1.0f);
            ImGui::SliderFloat("Dir Y##y", &m_Scene.Lights[0].Direction.y, -1.0f, 1.0f);
            ImGui::SliderFloat("Dir Z##z", &m_Scene.Lights[0].Direction.z, -1.0f, 1.0f);
            // Normalize direction
            if (glm::length(m_Scene.Lights[0].Direction) > 0.0f) {
                m_Scene.Lights[0].Direction = glm::normalize(m_Scene.Lights[0].Direction);
            }

            ImGui::Text("Point Light");
            ImGui::Separator();
            ImGui::SliderFloat("Point Intensity##point", &m_Scene.Lights[1].Intensity, 0.0f, 2.0f);
            ImGui::ColorEdit3("Point Color##point", &m_Scene.Lights[1].Color[0]);
            ImGui::SliderFloat("Point X##px", &m_Scene.Lights[1].Position.x, -10.0f, 10.0f);
            ImGui::SliderFloat("Point Y##py", &m_Scene.Lights[1].Position.y, 0.0f, 15.0f);
            ImGui::SliderFloat("Point Z##pz", &m_Scene.Lights[1].Position.z, -10.0f, 10.0f);
            ImGui::SliderFloat("Point Radius##radius", &m_Scene.Lights[1].Radius, 1.0f, 50.0f);
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
        // Clear only dynamic meshes (players), keep static meshes
        m_Scene.DynamicMeshes.clear();

        // Convert 2D positions to 3D world space for raytracing
        // Map 2D game space (0-800 or similar) to 3D space
        const float scale    = 0.01f;  // Scale down from screen space
        const float cubeSize = 1.0f;

        // Add current player as cube mesh (raised to sit on ground)
        Mesh playerMesh          = MeshLoader::GenerateCube(cubeSize);
        playerMesh.MaterialIndex = 0;  // green material
        glm::vec3 playerPos
                = { (m_PlayerPosition.x - 400.0f) * scale, cubeSize * 0.5f, (m_PlayerPosition.y - 300.0f) * scale };
        playerMesh.Transform = glm::translate(glm::mat4(1.0f), playerPos);
        m_Scene.DynamicMeshes.push_back(playerMesh);

        // Add other players as cube meshes
        m_PlayerDataMutex.lock();
        for (const auto& [playerID, playerData] : m_PlayerData) {
            if (playerID == m_PlayerID)
                continue;

            Mesh otherPlayerMesh          = MeshLoader::GenerateCube(cubeSize);
            otherPlayerMesh.MaterialIndex = 1;  // red material
            glm::vec3 otherPos            = { (playerData.Position.x - 400.0f) * scale, cubeSize * 0.5f,
                           (playerData.Position.y - 300.0f) * scale };
            otherPlayerMesh.Transform     = glm::translate(glm::mat4(1.0f), otherPos);
            m_Scene.DynamicMeshes.push_back(otherPlayerMesh);
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
                m_NetworkDataChanged = true;
                m_PlayerDataMutex.unlock();
                break;
            default:
                WL_WARN_TAG("Client", "Received unknown packet type: {}", (int) type);
                break;
        }
    }
};  // namespace Vlkrt