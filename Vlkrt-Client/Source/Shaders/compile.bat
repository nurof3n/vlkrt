@echo off
setlocal

set OUT=.
set MODULES=-I modules
set OPTS=-O

echo Compiling Slang shaders with optimizations...
set ERRORS=0

slangc raygen.slang -target spirv -entry RayGenMain %MODULES% %OPTS% -o %OUT%\raygen.rgen.spv
if %ERRORLEVEL% neq 0 ( echo [FAIL] raygen.slang & set ERRORS=1 ) else echo [OK]   raygen.rgen.spv

slangc raygen_temporal.slang -target spirv -entry RayGenTemporalMain %MODULES% %OPTS% -o %OUT%\raygen_temporal.rgen.spv
if %ERRORLEVEL% neq 0 ( echo [FAIL] raygen_temporal.slang & set ERRORS=1 ) else echo [OK]   raygen_temporal.rgen.spv

slangc miss.slang -target spirv -entry MissMain %MODULES% %OPTS% -o %OUT%\miss.rmiss.spv
if %ERRORLEVEL% neq 0 ( echo [FAIL] miss.slang (MissMain) & set ERRORS=1 ) else echo [OK]   miss.rmiss.spv

slangc miss.slang -target spirv -entry ShadowMissMain %MODULES% %OPTS% -o %OUT%\shadow_miss.rmiss.spv
if %ERRORLEVEL% neq 0 ( echo [FAIL] miss.slang (ShadowMissMain) & set ERRORS=1 ) else echo [OK]   shadow_miss.rmiss.spv

slangc closesthit.slang -target spirv -entry ClosestHitMain %MODULES% %OPTS% -o %OUT%\closesthit.rchit.spv
if %ERRORLEVEL% neq 0 ( echo [FAIL] closesthit.slang & set ERRORS=1 ) else echo [OK]   closesthit.rchit.spv

slangc closesthit_aabb.slang -target spirv -entry AabbClosestHitMain %MODULES% %OPTS% -o %OUT%\closesthit_aabb.rchit.spv
if %ERRORLEVEL% neq 0 ( echo [FAIL] closesthit_aabb.slang & set ERRORS=1 ) else echo [OK]   closesthit_aabb.rchit.spv

slangc intersect_analytic.slang -target spirv -entry AnalyticIntersectionMain %MODULES% %OPTS% -o %OUT%\intersect_analytic.rint.spv
if %ERRORLEVEL% neq 0 ( echo [FAIL] intersect_analytic.slang & set ERRORS=1 ) else echo [OK]   intersect_analytic.rint.spv

slangc intersect_sdf.slang -target spirv -entry SdfIntersectionMain %MODULES% %OPTS% -o %OUT%\intersect_sdf.rint.spv
if %ERRORLEVEL% neq 0 ( echo [FAIL] intersect_sdf.slang & set ERRORS=1 ) else echo [OK]   intersect_sdf.rint.spv

slangc compose_denoised.slang -target spirv -entry ComposeDenoisedMain %MODULES% %OPTS% -o %OUT%\compose_denoised.comp.spv
if %ERRORLEVEL% neq 0 ( echo [FAIL] compose_denoised.slang & set ERRORS=1 ) else echo [OK]   compose_denoised.comp.spv

echo.
if %ERRORS%==0 (
    echo All shaders compiled successfully.
) else (
    echo One or more shaders FAILED to compile.
    exit /b 1
)
endlocal
