FROM debian:stable-slim

ENV DEBIAN_FRONTEND=noninteractive

# Pinned Limine 12.6.0 binary release. The checksum is from its signed GitHub
# release asset and is verified before its UEFI removable-media loader is used.
ARG LIMINE_VERSION=12.6.0
ARG LIMINE_SHA256=44fb480294463c15823bba5378c955b46d15d08fc7b5a74de49fef2953d1878d

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        git \
        nasm \
        gcc-multilib \
        binutils-multiarch \
        grub-pc-bin \
        ovmf \
        mtools \
        gdisk \
        xorriso \
        qemu-system-x86 \
        netcat-openbsd \
        python3 \
        wget \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN wget -q -O /tmp/limine.tar.xz "https://github.com/limine-bootloader/limine/releases/download/v${LIMINE_VERSION}/limine-binary.tar.xz" \
    && echo "${LIMINE_SHA256}  /tmp/limine.tar.xz" | sha256sum -c - \
    && mkdir -p /opt/limine \
    && tar -xJf /tmp/limine.tar.xz -C /opt/limine --strip-components=1 \
    && rm -f /tmp/limine.tar.xz

WORKDIR /work

CMD ["/bin/bash"]
