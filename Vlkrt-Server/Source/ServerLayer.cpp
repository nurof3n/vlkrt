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
    {
        Walnut::BufferStreamWriter stream(s_ScratchBuffer);
        stream.WriteRaw(PacketType::ClientUpdate);

        m_PlayerDataMutex.lock();
        stream.WriteMap(m_PlayerData);
        m_PlayerDataMutex.unlock();

        m_Server.SendBufferToAllClients(stream.GetBuffer());

        // throttle updates to 20 times per second
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

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

        // Remove player data for disconnected client
        m_PlayerDataMutex.lock();
        m_PlayerData.erase(clientInfo.ID);
        m_PlayerDataMutex.unlock();
    }

    void ServerLayer::OnDataReceived(const Walnut::ClientInfo& clientInfo, const Walnut::Buffer& data)
    {
        Walnut::BufferStreamReader stream(data);

        PacketType type;
        stream.ReadRaw(type);

        switch (type) {
            case PacketType::ClientUpdate:
                m_PlayerDataMutex.lock();
                auto& playerData = m_PlayerData[clientInfo.ID];
                stream.ReadRaw<glm::vec2>(playerData.Position);
                stream.ReadRaw<glm::vec2>(playerData.Velocity);
                m_PlayerDataMutex.unlock();
                break;
            default:
                WL_WARN_TAG("Server", "Received unknown packet type {} from client {}", (uint32_t) type, clientInfo.ID);
                break;
        }
    }
}  // namespace Vlkrt