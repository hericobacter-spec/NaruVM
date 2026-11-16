# Pinned host compiler environment; Android cross-compilation uses the pinned Mac NDK.
FROM ubuntu@sha256:2198e4c65076e9cb0b3d695219df667c0e3b0072da64ca3c2648aca17dcecaf5
COPY linux-build-requirements.lock /tmp/henge-build-requirements.lock
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential=12.10ubuntu1 \
    ca-certificates=20260816ubuntu0.24.04.2 \
    curl=8.5.0-2ubuntu10.16 \
    git=1:2.43.0-1ubuntu7.4 \
    cmake=3.28.3-1build7 \
    pkg-config=1.8.1-2build1 \
    bison=2:3.8.2+dfsg-1ubuntu0.24.04.1 \
    flex=2.6.4-8.2build1 \
    ninja-build=1.11.1-2 \
    python3=3.12.3-0ubuntu2.1 \
    python3-venv=3.12.3-0ubuntu2.1 \
    python3-mako=1.3.2-1ubuntu0.1 \
    python3-packaging=24.0-1 \
    python3-yaml=6.0.1-2build2 \
    llvm-18-dev=1:18.1.3-1ubuntu1 \
    libclang-18-dev=1:18.1.3-1ubuntu1 \
    clang-18=1:18.1.3-1ubuntu1 \
    libclc-18-dev=1:18.1.3-1ubuntu1 \
    libllvmspirvlib-18-dev=18.1.8-1~ubuntu0.24.04.1 \
    libdrm-dev=2.4.125-1ubuntu0.1~24.04.2 \
    libexpat1-dev=2.6.1-2ubuntu0.6 \
    zlib1g-dev=1:1.3.dfsg-3.1ubuntu2.3 \
    libzstd-dev=1.5.5+dfsg2-2build1.1 \
    glslang-tools=15.1.0-2~ubuntu0.24.04.2
RUN python3 -m venv --system-site-packages /opt/henge-tools && \
    /opt/henge-tools/bin/pip install --require-hashes -r /tmp/henge-build-requirements.lock
ENV PATH="/opt/henge-tools/bin:/usr/lib/llvm-18/bin:${PATH}"
