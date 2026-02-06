#include "ClientLayer.h"

#include "Walnut/Input/Input.h"
#include "Walnut/ImGui/ImGuiTheme.h"

#include "Walnut/Serialization/BufferStream.h"
#include "ServerPacket.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

namespace Vlkrt
{
    static Walnut::Buffer s_ScratchBuffer{};

    static void DrawRect(glm::vec2 position, glm::vec2 size, ImU32 color)
    {
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        ImVec2      min      = ImGui::GetWindowPos() + ImVec2(position.x, position.y);
        ImVec2      max      = min + ImVec2(size.x, size.y);
        drawList->AddRectFilled(min, max, color);
    }

    void ClientLayer::OnAttach()
    {
        s_ScratchBuffer.Allocate(10 * 1024 * 1024);  // 10 MB

        m_Client.SetDataReceivedCallback([this](const Walnut::Buffer& buffer) { OnDataReceived(buffer); });
    }

    void ClientLayer::OnDetach()
    {}

    void ClientLayer::OnUpdate(float ts)
    {
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
    }

    void ClientLayer::OnRender()
    {}

    void ClientLayer::OnUIRender()
    {
        auto connectionStatus = m_Client.GetConnectionStatus();
        if (connectionStatus == Walnut::Client::ConnectionStatus::Connected) {
            DrawRect(m_PlayerPosition, glm::vec2{ 50.0f, 50.0f }, IM_COL32(255, 0, 0, 255));
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
                glm::vec2 pos, vel;
                stream.ReadRaw<glm::vec2>(pos);
                stream.ReadRaw<glm::vec2>(vel);
                break;
        }
    }
};  // namespace Vlkrt