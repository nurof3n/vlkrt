-- premake5.lua
workspace "Vlkrt-Server"
   architecture "x64"
   configurations { "Debug", "Release", "Dist" }
   startproject "Vlkrt-Server"

   -- Workspace-wide defines
   defines
   {
       "WL_HEADLESS"
   }

   -- Workspace-wide build options for MSVC
   filter "system:windows"
      buildoptions { "/EHsc", "/Zc:preprocessor", "/Zc:__cplusplus" }

-- Directories
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

if os.istarget("windows") then
   WalnutNetworkingBinDir = "Walnut/Walnut-Modules/Walnut-Networking/vendor/GameNetworkingSockets/bin/Windows/%{cfg.buildcfg}/"
else
   WalnutNetworkingBinDir = "Walnut/Walnut-Modules/Walnut-Networking/vendor/GameNetworkingSockets/bin/Linux/"
end

include "Walnut/Build-Walnut-Headless-External.lua"

group "App"
    include "Vlkrt-Common/Build-Vlkrt-Common-Headless.lua"
    include "Vlkrt-Server/Build-Vlkrt-Server.lua"
group ""