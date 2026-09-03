FROM debian:stable-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        nasm \
        gcc-multilib \
        binutils-multiarch \
        grub-pc-bin \
        xorriso \
        qemu-system-i386 \
        python3 \
        wget \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

CMD ["/bin/bash"]
