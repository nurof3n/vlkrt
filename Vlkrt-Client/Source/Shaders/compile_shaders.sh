#!/bin/bash
# Compile all GLSL shaders in this directory to SPIR-V

SHADER_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
GLSLC="glslc"

# Check if glslc is available
if ! command -v $GLSLC &> /dev/null; then
    echo "Error: glslc not found in PATH"
    exit 1
fi

echo "Compiling shaders in: $SHADER_DIR"

# Array of shader extensions and their stages
declare -A SHADER_STAGES
SHADER_STAGES[rgen]="rgen"
SHADER_STAGES[rmiss]="rmiss"
SHADER_STAGES[rchit]="rchit"
SHADER_STAGES[rahit]="rahit"
SHADER_STAGES[rint]="rint"
SHADER_STAGES[rcall]="rcall"
SHADER_STAGES[frag]="frag"
SHADER_STAGES[vert]="vert"
SHADER_STAGES[comp]="comp"

# Counter for compiled shaders
COMPILED=0
FAILED=0

# Find and compile all shader files
for shader_file in "$SHADER_DIR"/*; do
    filename=$(basename "$shader_file")

    # Skip directories and compiled files
    if [[ ! -f "$shader_file" ]] || [[ "$filename" == *.spv ]]; then
        continue
    fi

    # Get file extension
    ext="${filename##*.}"

    # Check if it's a known shader type
    if [[ -v SHADER_STAGES[$ext] ]]; then
        stage="${SHADER_STAGES[$ext]}"
        output="$shader_file.spv"

        echo "Compiling: $filename (stage: $stage)"

        if $GLSLC --target-env=vulkan1.2 -fshader-stage=$stage "$shader_file" -o "$output"; then
            echo "  ✓ $output"
            ((COMPILED++))
        else
            echo "  ✗ Failed to compile $filename"
            ((FAILED++))
        fi
    fi
done

echo ""
echo "Compilation complete: $COMPILED succeeded, $FAILED failed"

if [ $FAILED -gt 0 ]; then
    exit 1
fi

exit 0
