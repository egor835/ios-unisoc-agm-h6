# Toolchain for the AGM H6 shim / XNU experiments
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu make python3 python3-pil device-tree-compiler \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
