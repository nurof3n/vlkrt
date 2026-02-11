#pragma once

#include "Walnut/Layer.h"
#include "Walnut/Networking/Server.h"

#include "HeadlessConsole.h"

#include <glm/glm.hpp>

namespace Vlkrt
{
    class ServerLayer : public Walnut::Layer
    {
    public:
        struct PlayerData
        {
            glm::vec3 Position;
            glm::vec3 Velocity;
        };

    public:
        void OnAttach() override;
        void OnDetach() override;

        void OnUpdate(float ts) override;
        void OnRender() override;
        void OnUIRender() override;

    private:
        void OnConsoleMessage(std::string_view message);

        void OnClientConnected(const Walnut::ClientInfo& clientInfo);
        void OnClientDisconnected(const Walnut::ClientInfo& clientInfo);
        void OnDataReceived(const Walnut::ClientInfo& clientInfo, const Walnut::Buffer& data);

    private:
        HeadlessConsole m_Console;
        Walnut::Server m_Server{ 1337 };

        std::mutex m_PlayerDataMutex;
        std::map<uint32_t, PlayerData> m_PlayerData;

        float m_UpdateAccumulator{ 0.0f };
    };
};  // namespace Vlkrt
