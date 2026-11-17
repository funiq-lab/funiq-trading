FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates python3 \
    libboost-system-dev libboost-thread-dev nlohmann-json3-dev \
    libsqlite3-dev libtinyxml2-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /app
RUN apt-get update && apt-get install -y --no-install-recommends libfmt-dev \
    && rm -rf /var/lib/apt/lists/*
