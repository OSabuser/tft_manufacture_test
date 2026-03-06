# syntax=docker/dockerfile:1

# =============================================================================
# Stage 1 — toolchain
# Скачиваем ARM GCC тулчейн. Этот слой кэшируется надолго:
# инвалидируется только при изменении TOOLCHAIN_VERSION.
# =============================================================================
FROM ubuntu:22.04@sha256:3ba65aa20f86a0fad9df2b2c259c613df006b2e6d0bfcc8a146afb8c525a9751 AS toolchain

ARG TOOLCHAIN_VERSION=13.3.rel1

RUN apt-get update && apt-get install -y --no-install-recommends \
        wget xz-utils ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN set -eux && \
    UNAME_ARCH="$(uname -m)" && \
    case "${UNAME_ARCH}" in \
        "x86_64")  HOST_ARCH="x86_64" ;; \
        "aarch64") HOST_ARCH="aarch64" ;; \
        *) echo "Unsupported arch: ${UNAME_ARCH}" && exit 1 ;; \
    esac && \
    wget -qO- \
    "https://developer.arm.com/-/media/Files/downloads/gnu/${TOOLCHAIN_VERSION}/binrel/arm-gnu-toolchain-${TOOLCHAIN_VERSION}-${HOST_ARCH}-arm-none-eabi.tar.xz" \
    | tar -xJ -C /opt && \
    ln -s /opt/arm-gnu-toolchain-${TOOLCHAIN_VERSION}-${HOST_ARCH}-arm-none-eabi /opt/arm-toolchain


# =============================================================================
# Stage 2 — dev
# Устанавливаем инструменты разработки поверх готового тулчейна.
# Этот слой меняется чаще (обновление clang, cmake и т.д.),
# но слой Stage 1 при этом не инвалидируется и берётся из кэша.
# =============================================================================
FROM toolchain AS dev

LABEL maintainer="von.akimov@gmail.com" \
      version="0.1.1"

ENV DEBIAN_FRONTEND=noninteractive

# Копируем тулчейн из Stage 1
COPY --from=toolchain /opt /opt

ENV PATH="/opt/arm-toolchain/bin:${PATH}"

# Устанавливаем LLVM GPG ключ и репозиторий (llvm >= 16 для поддержки If в .clangd), затем все dev-инструменты
RUN apt-get update && apt-get install -y --no-install-recommends \
        gnupg ca-certificates wget && \
    wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
    | gpg --dearmor -o /usr/share/keyrings/llvm.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/jammy/ llvm-toolchain-jammy-17 main" \
    > /etc/apt/sources.list.d/llvm.list && \
    apt-get update && apt-get install -y --no-install-recommends \
        curl git cmake ninja-build bear \
        python3 python3-pip \
        clang-17 clangd-17 clang-tidy-17 clang-format-17 \
        gdb-multiarch \
        build-essential \
    && update-alternatives --install /usr/bin/clangd       clangd       /usr/bin/clangd-17       100 \
    && update-alternatives --install /usr/bin/clang        clang        /usr/bin/clang-17       100 \
    && update-alternatives --install /usr/bin/clang-tidy   clang-tidy   /usr/bin/clang-tidy-17   100 \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-17 100 \
    && pip3 install --no-cache-dir cmake-format==0.6.13 \
    && ln -s /usr/bin/clangd /usr/local/bin/clangd \
    && ln -s /usr/bin/clang  /usr/local/bin/clang \
    && rm -rf /var/lib/apt/lists/*


ARG USER_ID=1000
ARG GROUP_ID=1000
ARG WORKSPACE_PATH="/workspace"

# Создаем группу/пользователя с UID хоста
RUN groupadd --gid $GROUP_ID devgroup && \
    useradd --uid $USER_ID --gid devgroup \
            --create-home --shell /bin/bash devuser && \
    mkdir -p $WORKSPACE_PATH && \
    chown -R devuser:devgroup $WORKSPACE_PATH
    
USER devuser
WORKDIR $WORKSPACE_PATH