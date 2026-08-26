ARG BUILD_ARCH=
ARG TARGETARCH
ARG BUILD_VERSION=1.10.1

FROM debian:bookworm-slim AS build
ARG BUILD_ARCH
ARG TARGETARCH

RUN apt-get update \
    && apt-get install --no-install-recommends -y build-essential \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN case "${BUILD_ARCH:-${TARGETARCH:-amd64}}" in \
        amd64) platform=x86_64 ;; \
        aarch64|arm64) platform=aarch64 ;; \
        *) echo "Unsupported architecture: ${BUILD_ARCH:-${TARGETARCH:-amd64}}" >&2; exit 1 ;; \
    esac \
    && mkdir -p "bin/linux/${platform}" \
    && make -C airhass HOST=linux PLATFORM="${platform}" CC=gcc "../bin/airhass-linux-${platform}-static" \
    && install -Dm755 "bin/airhass-linux-${platform}-static" /out/airhass

FROM scratch
ARG BUILD_ARCH
ARG TARGETARCH
ARG BUILD_VERSION

LABEL \
    io.hass.version="${BUILD_VERSION}" \
    io.hass.type="app" \
    io.hass.arch="${BUILD_ARCH:-${TARGETARCH:-amd64}}"

COPY --from=build /out/airhass /airhass
ENTRYPOINT [ "/airhass", "-Z" ]
