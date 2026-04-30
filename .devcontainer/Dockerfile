# Base image: Ubuntu
FROM ubuntu:22.04

ARG UBUNTU_PORTS_MIRROR_HTTP=http://mirror.yuki.net.uk/ubuntu-ports
ARG UBUNTU_PORTS_MIRROR_HTTPS=https://mirror.yuki.net.uk/ubuntu-ports

ENV TZ=Asia/Seoul LANG=ko_KR.UTF-8 LANGUAGE=ko_KR.UTF-8

# Install dependencies.
# On Apple Silicon hosts, we build inside an arm64 container and cross-compile
# Pintos as x86_64 with the GNU cross toolchain.
RUN export DEBIAN_FRONTEND=noninteractive && \
    sed -i "s|http://ports.ubuntu.com/ubuntu-ports|${UBUNTU_PORTS_MIRROR_HTTP}|g" /etc/apt/sources.list && \
    for attempt in 1 2 3; do \
        apt-get clean && rm -rf /var/lib/apt/lists/* && \
        apt-get update -o Acquire::Retries=5 -o Acquire::http::No-Cache=true && \
        apt-get install -y --no-install-recommends ca-certificates && \
        break; \
        if [ "$attempt" -eq 3 ]; then \
            exit 1; \
        fi; \
        sleep 5; \
    done && \
    sed -i "s|${UBUNTU_PORTS_MIRROR_HTTP}|${UBUNTU_PORTS_MIRROR_HTTPS}|g" /etc/apt/sources.list && \
    for attempt in 1 2 3; do \
        apt-get clean && rm -rf /var/lib/apt/lists/* && \
        apt-get update -o Acquire::Retries=5 -o Acquire::http::No-Cache=true -o Acquire::https::No-Cache=true && \
        apt-get install -y --no-install-recommends \
        locales tzdata \
        build-essential \
        gcc \
        gdb \
        vim \
        git \
        sudo \
        ca-certificates \
        qemu-system-x86 \
        python3 \
        gcc-x86-64-linux-gnu \
        binutils-x86-64-linux-gnu && \
        break; \
        if [ "$attempt" -eq 3 ]; then \
            exit 1; \
        fi; \
        sleep 5; \
    done && \
    locale-gen ko_KR.UTF-8 && \
    update-locale LANG=ko_KR.UTF-8 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

RUN useradd -m -s /bin/bash jungle && usermod -aG sudo jungle
RUN echo "jungle ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/jungle

USER jungle
WORKDIR /home/jungle

RUN echo "source /workspaces/krafton_jungle_pintos_threads_week09/pintos/activate" >> /home/jungle/.bashrc

# Clone the repository
# RUN git clone https://github.com/casys-kaist/pintos-kaist /home/jungle/pintos

# Set default shell for the `jungle` user
