#include "Walnut/Application.h"
#include "Walnut/EntryPoint.h"

#include "ClientLayer.h"


Walnut::Application* Walnut::CreateApplication(int argc, char** argv)
{
    Walnut::ApplicationSpecification spec;
    spec.Name           = "Vlkrt";
    spec.CustomTitlebar = true;
    spec.UseDockspace   = false;

    auto app = new Walnut::Application(spec);
    app->PushLayer<Vlkrt::ClientLayer>();

    return app;
}