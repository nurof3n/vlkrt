#pragma once

#include "Walnut/Layer.h"

#include "HeadlessConsole.h"

namespace Vlkrt
{
    class ServerLayer : public Walnut::Layer
    {
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
        Walnut::Server  m_Server{ 8192 };
    };
};  // namespace Vlkrt
