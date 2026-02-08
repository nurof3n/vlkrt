@echo off
echo Compiling ray tracing shaders...

if not defined VULKAN_SDK (
    echo Error: VULKAN_SDK environment variable not set
    exit /b 1
)

set GLSLC=%VULKAN_SDK%\Bin\glslc.exe

if not exist "%GLSLC%" (
    echo Error: glslc.exe not found at %GLSLC%
    exit /b 1
)

"%GLSLC%" --target-env=vulkan1.2 -fshader-stage=rgen raygen.rgen -o raygen.rgen.spv
if %ERRORLEVEL% neq 0 (
    echo Failed to compile raygen.rgen
    exit /b 1
)

"%GLSLC%" --target-env=vulkan1.2 -fshader-stage=rmiss miss.rmiss -o miss.rmiss.spv
if %ERRORLEVEL% neq 0 (
    echo Failed to compile miss.rmiss
    exit /b 1
)

"%GLSLC%" --target-env=vulkan1.2 -fshader-stage=rchit closesthit.rchit -o closesthit.rchit.spv
if %ERRORLEVEL% neq 0 (
    echo Failed to compile closesthit.rchit
    exit /b 1
)

"%GLSLC%" --target-env=vulkan1.2 -fshader-stage=rint intersection.rint -o intersection.rint.spv
if %ERRORLEVEL% neq 0 (
    echo Failed to compile intersection.rint
    exit /b 1
)

echo All shaders compiled successfully!
