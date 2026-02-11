FROM ubuntu:24.04 AS build

WORKDIR /src

RUN apt-get update \
        ; apt-get install -y --no-install-recommends \
                build-essential \
                ca-certificates \
                git \
                make \
                python3 \
        ; rm -rf /var/lib/apt/lists/*

COPY . .

RUN chmod +x ./Walnut/vendor/bin/premake/Linux/premake5 \
        ; ./Walnut/vendor/bin/premake/Linux/premake5 --cc=gcc --file=Build-Server.lua gmake2 \
        ; make config=release -j"$(nproc)" \
        ; strip --strip-unneeded ./bin/Release-linux-x86_64/Vlkrt-Server/Vlkrt-Server \
        ; strip --strip-unneeded ./Walnut/Walnut-Modules/Walnut-Networking/vendor/GameNetworkingSockets/bin/Linux/libGameNetworkingSockets.so \
        ; strip --strip-unneeded ./Walnut/Walnut-Modules/Walnut-Networking/vendor/GameNetworkingSockets/bin/Linux/libprotobuf.so.23

FROM build AS deps

RUN mkdir -p /deps/app/lib /deps/etc/ssl/certs \
        ; cp ./bin/Release-linux-x86_64/Vlkrt-Server/Vlkrt-Server /deps/app/ \
        ; cp ./Walnut/Walnut-Modules/Walnut-Networking/vendor/GameNetworkingSockets/bin/Linux/libGameNetworkingSockets.so /deps/app/lib/ \
        ; cp ./Walnut/Walnut-Modules/Walnut-Networking/vendor/GameNetworkingSockets/bin/Linux/libprotobuf.so.23 /deps/app/lib/ \
        ; cp /etc/ssl/certs/ca-certificates.crt /deps/etc/ssl/certs/ \
        ; LD_LIBRARY_PATH=/src/Walnut/Walnut-Modules/Walnut-Networking/vendor/GameNetworkingSockets/bin/Linux \
          ldd /src/bin/Release-linux-x86_64/Vlkrt-Server/Vlkrt-Server \
          | awk '{for (i = 1; i <= NF; i++) if ($i ~ /^\//) print $i}' \
          | grep -v '/src/Walnut/' \
          | xargs -r -I{} cp --parents "{}" /deps

FROM scratch AS runtime

WORKDIR /app

COPY --from=deps /deps/ /
COPY --from=deps /lib/x86_64-linux-gnu/libtinfo.so.6 /lib/x86_64-linux-gnu/libtinfo.so.6

COPY --from=deps /bin/sh /bin/sh
COPY --from=deps /bin/bash /bin/bash

ENV LD_LIBRARY_PATH=/app/lib

CMD ["/app/Vlkrt-Server"]
