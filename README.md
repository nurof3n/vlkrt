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
- Vulkan SDK installed and available in `PATH`
- CMake 3.30+ (required by NRD dependency build)
- Git with submodule support (`git submodule update --init --recursive`)

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

### Hosting the server

#### Docker

You need to have Docker installed and running on your machine.
Then, you can build the server image using the provided `Dockerfile`:

```bash
docker build -t vlkrt-server .
```

After the image is built, you can run the server container:

```bash
docker run --rm -p 1337:1337/udp vlkrt-server
```

You can then connect to it on `localhost:1337`.

#### Unikraft Cloud

> **Note**: The server needs to communicate with the clients over UDP.
> Currently, UDP services are a preview feature and are NOT supported on public Unikraft Cloud metros.

[Unikraft Cloud](https://unikraft.com/), codenamed "the millisecond platform", is a blazing-fast cloud platform.
IT allows deploying applications inside microVMs that outperform containers in both security and performance, with boot times in the order of milliseconds.

To deploy the server on Unikraft Cloud, you first need to create an account on the [console](https://console.unikraft.cloud/).
Then, install the [unikraft CLI](https://unikraft.com/docs/introduction) and make sure you have Docker installed and running on your machine.
You also need a [BuildKit](https://github.com/moby/buildkit) builder, which usually comes bundled with Docker.

First, ensure you are logged in to the CLI:

```bash
unikraft login
```

Then, build and push the server image to the Unikraft Cloud registry:

```bash
unikraft build . -o <my-org>/vlkrt-server:latest
```

Next, create the server and attach it to a UDP service:

```bash
unikraft api /v1/instances -d '
{
  "name": "vlkrt-server",
  "image": "<my-org>/vlkrt-server:latest",
  "service_group": {
    "services": [
      {
        "port": 1337,
        "destination_port": 1337,
        "protocol": "udp",
        "ip": "<user-ip>"
      }
    ],
    "domains": [
      {
        "name": "vlkrt-server"
      }
    ]
  },
  "memory_mb": 128,
  "scale_to_zero": {
    "policy": "on",
    "stateful": true,
    "cooldown_time_ms": 1000
  },
  "autostart": true
}'
```

The `<user-ip>` field should be replaced with an IP address leased to your Unikraft Cloud user.

Grab the FQDN from the output, and use it to connect to the server from the client.
The server will automatically scale down to zero when not in use (i.e., after all the clients have disconnected), and scale up again in **milliseconds** when a client tries to connect, saving on resources and costs.
