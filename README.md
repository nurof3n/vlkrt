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
    make -j $(nproc)
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

### Hosting the server in Unikraft Cloud

[Unikraft Cloud](https://unikraft.cloud/), codenamed "the millisecond platform", is a blazing-fast cloud platform.
IT allows deploying applications inside microVMs that outperform containers in both security and performance, with boot times in the order of milliseconds.

To deploy the server on Unikraft Cloud, you first need to create an account on the [console](https://console.unikraft.cloud/).
Then, install the [kraft CLI](https://unikraft.com/docs/introduction) and make sure you have Docker Engine installed and running on your machine.
Also, grab your token from the console, and you're ready to go:

```bash
 kraft cloud deploy \
    -M 128 \
    -p 1337:1337/udp \
    --scale-to-zero on \
    --scale-to-zero-stateful \
    --rootfs-type erofs \
    .
```

You will get an output similar to this:

```text
[●] Deployed but instance is on standby!
 │
 ├────── name: vlkrt-beogr
 ├────── uuid: 227f20d8-b8f2-4d40-8ed2-146365773430
 ├───── metro: https://api.fra.unikraft.cloud/v1
 ├───── state: standby
 ├──── domain: late-bonobo-pgy3l5qy.fra.unikraft.app
 ├───── image: acioc/vlkrt@sha256:d273cc3b46bb6b248b24631e1486324c9344bc150bf56611bc595206ec0f11eb
 ├─ boot time: 66.82 ms
 ├──── memory: 128 MiB
 ├─── service: late-bonobo-pgy3l5qy
 ├ private ip: 10.0.2.169
 └────── args: LD_LIBRARY_PATH=/app/lib /app/Vlkrt-Server
```

Grab the FQDN from the output, and use it to connect to the server from the client.
The server will automatically scale down to zero when not in use, and scale up again when a client tries to connect, saving on resources and costs.
