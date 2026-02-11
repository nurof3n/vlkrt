#pragma once

#include "Scene.h"

#include <sol/sol.hpp>
#include <string>
#include <memory>

namespace Vlkrt
{
    /**
     * @brief Manages Lua scripting for scene entities, allowing scripts to manipulate entity properties and respond to
     * update events. Each entity can have an optional Lua script that is loaded and executed on update.
     */
    class ScriptEngine
    {
    public:
        static void Init();
        static void Shutdown();

        static void LoadScript(SceneEntity& entity);
        static void CallOnUpdate(SceneEntity& entity, float ts);

    private:
        static void RegisterBindings();

    private:
        static std::unique_ptr<sol::state> s_LuaState;
    };
}  // namespace Vlkrt
