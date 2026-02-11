# Vlkrt

This repository contains the source code for a simple Vulkan multiplayer game engine.
It contains a client and a headless server, both implemented using the [Walnut framework](https://github.com/StudioCherno/Walnut).

It aims to provide a Hybrid approach to rendering, with rasterization and ray tracing.

## Kudos

This project was kickstarted from Cherno's [Cubed](https://github.com/TheCherno/Cubed) tutorial project, also available on [YouTube](https://www.youtube.com/watch?v=W1MSxy90BLg).
Many thanks to Cherno for providing such a great starting point and for his amazing content in general.

## Getting Started

### Prerequisites

- C++20 compatible compiler
- Visual Studio 2022 or later (for Windows)
- `make` (for Linux/Mac)

### Building

1. Clone the repository:

   ```bash
   git clone https://github.com/nurof3n/vlkrt.git
   cd vlkrt
   ```

2. Grab the submodules:

   ```bash
   git submodule update --init --recursive
   ```

3. Create build files:

    ```bash
    # PowerShell (Windows)
    .\scripts\Setup.bat
    # Linux/Mac
    ./scripts/setup.sh
    ```

4. Build the project:

    ```bash
    # PowerShell (Windows)
    msbuild Vlkrt.sln /p:Configuration=Release # or do it from Visual Studio
    # Linux/Mac
    make -j$(nproc)
    ```

5. Compile the shaders:

    ```bash
    # PowerShell (Windows)
    cd .\Vlkrt-Client\Source\Shaders\
    .\compile_shaders.bat
    # Linux/Mac
    cd ./Vlkrt-Client/Source/Shaders/ && ./compile_shaders.sh
    ```

### Running

The executables will be located in `bin/Release-<platform>-<arch>/Vlkrt-{Client,Server}`.
Run the server first, then launch one or more clients to connect to it.

### Hosting the Server in Unikraft Cloud

TODO
