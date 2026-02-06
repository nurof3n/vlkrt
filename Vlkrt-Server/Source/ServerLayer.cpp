#include "ServerLayer.h"

#include "Walnut/Core/Log.h"

#include "Walnut/Serialization/BufferStream.h"
#include "ServerPacket.h"


namespace Vlkrt
{
    static Walnut::Buffer s_ScratchBuffer{};

    void ServerLayer::OnAttach()
    {
        s_ScratchBuffer.Allocate(10 * 1024 * 1024);  // 10 MB scratch buffer

        m_Console.SetMessageSendCallback([this](std::string_view message) { OnConsoleMessage(message); });

        m_Server.SetClientConnectedCallback(
                [this](const Walnut::ClientInfo& clientInfo) { OnClientConnected(clientInfo); });
        m_Server.SetClientDisconnectedCallback(
                [this](const Walnut::ClientInfo& clientInfo) { OnClientDisconnected(clientInfo); });
        m_Server.SetDataReceivedCallback([this](const Walnut::ClientInfo& clientInfo, const Walnut::Buffer& data) {
            OnDataReceived(clientInfo, data);
        });

        m_Server.Start();
    }

    void ServerLayer::OnDetach()
    {
        m_Server.Stop();
    }

    void ServerLayer::OnUpdate(float ts)
    {}

    void ServerLayer::OnRender()
    {}

    void ServerLayer::OnUIRender()
    {}

    void ServerLayer::OnConsoleMessage(std::string_view message)
    {
        if (message.starts_with('/')) {
            // Handle commands
            std::cout << "This is a command: " << message << std::endl;
        }
    }

    void ServerLayer::OnClientConnected(const Walnut::ClientInfo& clientInfo)
    {
        WL_INFO_TAG("Server", "Client Connected: {}", clientInfo.ID);

        Walnut::BufferStreamWriter stream(s_ScratchBuffer);
        stream.WriteRaw(PacketType::ClientConnect);
        stream.WriteRaw(clientInfo.ID);

        m_Server.SendBufferToClient(clientInfo.ID, stream.GetBuffer());
    }

    void ServerLayer::OnClientDisconnected(const Walnut::ClientInfo& clientInfo)
    {
        WL_INFO_TAG("Server", "Client Disconnected: {}", clientInfo.ID);
    }

    void ServerLayer::OnDataReceived(const Walnut::ClientInfo& clientInfo, const Walnut::Buffer& data)
    {
        Walnut::BufferStreamReader stream(data);

        PacketType type;
        stream.ReadRaw(type);

        switch (type) {
            case PacketType::ClientUpdate: {
                glm::vec2 pos, vel;
                stream.ReadRaw<glm::vec2>(pos);
                stream.ReadRaw<glm::vec2>(vel);

                WL_INFO_TAG("Server", "Received ClientUpdate from {}: Pos({}, {}), Vel({}, {})", clientInfo.ID, pos.x,
                        pos.y, vel.x, vel.y);
            }
        }
    }
}  // namespace Vlkrt