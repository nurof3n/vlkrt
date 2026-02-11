-- premake5.lua
workspace "Vlkrt-Client"
   architecture "x64"
   configurations { "Debug", "Release", "Dist" }
   startproject "Vlkrt-Client"

   -- Workspace-wide build options for MSVC
   filter "system:windows"
      buildoptions { "/EHsc", "/Zc:preprocessor", "/Zc:__cplusplus" }

   defines { "IMGUI_DEFINE_MATH_OPERATORS", "TINYOBJLOADER_IMPLEMENTATION" }

-- Directories
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

if os.istarget("windows") then
   WalnutNetworkingBinDir = "Walnut/Walnut-Modules/Walnut-Networking/vendor/GameNetworkingSockets/bin/Windows/%{cfg.buildcfg}/"
else
   WalnutNetworkingBinDir = "Walnut/Walnut-Modules/Walnut-Networking/vendor/GameNetworkingSockets/bin/Linux/"
end

include "Walnut/Build-Walnut-External.lua"

group "App"
   include "Vlkrt-Common/Build-Vlkrt-Common.lua"
   include "Vlkrt-Client/Build-Vlkrt-Client.lua"
group ""