@echo off
REM Compile all GLSL shaders in this directory to SPIR-V

if not defined VULKAN_SDK (
    echo Error: VULKAN_SDK environment variable not set
    exit /b 1
)

set GLSLC=%VULKAN_SDK%\Bin\glslc.exe

if not exist "%GLSLC%" (
    echo Error: glslc.exe not found at %GLSLC%
    exit /b 1
)

echo Compiling all shaders...
setlocal enabledelayedexpansion

set COMPILED=0
set FAILED=0

REM Compile ray tracing shaders
for %%f in (*.rgen) do (
    echo Compiling: %%f
    "%GLSLC%" --target-env=vulkan1.2 -fshader-stage=rgen "%%f" -o "%%f.spv"
    if !ERRORLEVEL! equ 0 (
        echo   OK: %%f.spv
        set /a COMPILED+=1
    ) else (
        echo   FAILED: %%f
        set /a FAILED+=1
    )
)

for %%f in (*.rmiss) do (
    echo Compiling: %%f
    "%GLSLC%" --target-env=vulkan1.2 -fshader-stage=rmiss "%%f" -o "%%f.spv"
    if !ERRORLEVEL! equ 0 (
        echo   OK: %%f.spv
        set /a COMPILED+=1
    ) else (
        echo   FAILED: %%f
        set /a FAILED+=1
    )
)

for %%f in (*.rchit) do (
    echo Compiling: %%f
    "%GLSLC%" --target-env=vulkan1.2 -fshader-stage=rchit "%%f" -o "%%f.spv"
    if !ERRORLEVEL! equ 0 (
        echo   OK: %%f.spv
        set /a COMPILED+=1
    ) else (
        echo   FAILED: %%f
        set /a FAILED+=1
    )
)

for %%f in (*.rahit) do (
    echo Compiling: %%f
    "%GLSLC%" --target-env=vulkan1.2 -fshader-stage=rahit "%%f" -o "%%f.spv"
    if !ERRORLEVEL! equ 0 (
        echo   OK: %%f.spv
        set /a COMPILED+=1
    ) else (
        echo   FAILED: %%f
        set /a FAILED+=1
    )
)

for %%f in (*.rint) do (
    echo Compiling: %%f
    "%GLSLC%" --target-env=vulkan1.2 -fshader-stage=rint "%%f" -o "%%f.spv"
    if !ERRORLEVEL! equ 0 (
        echo   OK: %%f.spv
        set /a COMPILED+=1
    ) else (
        echo   FAILED: %%f
        set /a FAILED+=1
    )
)

for %%f in (*.rcall) do (
    echo Compiling: %%f
    "%GLSLC%" --target-env=vulkan1.2 -fshader-stage=rcall "%%f" -o "%%f.spv"
    if !ERRORLEVEL! equ 0 (
        echo   OK: %%f.spv
        set /a COMPILED+=1
    ) else (
        echo   FAILED: %%f
        set /a FAILED+=1
    )
)

REM Compile fragment and vertex shaders
for %%f in (*.frag) do (
    echo Compiling: %%f
    "%GLSLC%" --target-env=vulkan1.2 -fshader-stage=frag "%%f" -o "%%f.spv"
    if !ERRORLEVEL! equ 0 (
        echo   OK: %%f.spv
        set /a COMPILED+=1
    ) else (
        echo   FAILED: %%f
        set /a FAILED+=1
    )
)

for %%f in (*.vert) do (
    echo Compiling: %%f
    "%GLSLC%" --target-env=vulkan1.2 -fshader-stage=vert "%%f" -o "%%f.spv"
    if !ERRORLEVEL! equ 0 (
        echo   OK: %%f.spv
        set /a COMPILED+=1
    ) else (
        echo   FAILED: %%f
        set /a FAILED+=1
    )
)

REM Compile compute shaders
for %%f in (*.comp) do (
    echo Compiling: %%f
    "%GLSLC%" --target-env=vulkan1.2 -fshader-stage=comp "%%f" -o "%%f.spv"
    if !ERRORLEVEL! equ 0 (
        echo   OK: %%f.spv
        set /a COMPILED+=1
    ) else (
        echo   FAILED: %%f
        set /a FAILED+=1
    )
)

echo.
echo Compilation complete: %COMPILED% succeeded, %FAILED% failed

if %FAILED% gtr 0 (
    exit /b 1
)

exit /b 0
