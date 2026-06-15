#!/usr/bin/env bash

OUT="."
MODULES=(-I modules)
OPTS=(-O)

echo "Compiling Slang shaders with optimizations..."
ERRORS=0

compile_shader() {
    local src="$1"
    local entry="$2"
    local out_file="$3"
    local fail_label="$4"

    slangc "$src" -target spirv -entry "$entry" "${MODULES[@]}" "${OPTS[@]}" -o "$OUT/$out_file"
    if [ $? -ne 0 ]; then
        echo "[FAIL] $fail_label"
        ERRORS=1
    else
        echo "[OK]   $out_file"
    fi
}

compile_shader "raygen.slang" "RayGenMain" "raygen.rgen.spv" "raygen.slang"
compile_shader "raygen_temporal.slang" "RayGenTemporalMain" "raygen_temporal.rgen.spv" "raygen_temporal.slang"
compile_shader "miss.slang" "MissMain" "miss.rmiss.spv" "miss.slang (MissMain)"
compile_shader "miss.slang" "ShadowMissMain" "shadow_miss.rmiss.spv" "miss.slang (ShadowMissMain)"
compile_shader "closesthit.slang" "ClosestHitMain" "closesthit.rchit.spv" "closesthit.slang"
compile_shader "closesthit_aabb.slang" "AabbClosestHitMain" "closesthit_aabb.rchit.spv" "closesthit_aabb.slang"
compile_shader "intersect_analytic.slang" "AnalyticIntersectionMain" "intersect_analytic.rint.spv" "intersect_analytic.slang"
compile_shader "intersect_sdf.slang" "SdfIntersectionMain" "intersect_sdf.rint.spv" "intersect_sdf.slang"
compile_shader "compose_denoised.slang" "ComposeDenoisedMain" "compose_denoised.comp.spv" "compose_denoised.slang"

echo
if [ "$ERRORS" -eq 0 ]; then
    echo "All shaders compiled successfully."
else
    echo "One or more shaders FAILED to compile."
    exit 1
fi
