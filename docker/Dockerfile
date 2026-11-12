FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

# Install system dependencies
RUN apt-get update && apt-get install -y \
    # Basic tools
    curl wget git vim nano htop net-tools inotify-tools sudo \
    # Network and development tools
    software-properties-common ca-certificates gnupg lsb-release \
    # C++ dependencies
    libboost-all-dev nlohmann-json3-dev libfmt-dev \
    # tinyxml2 dependencies
    libtinyxml2-dev \
    # Other dependencies
    libssl-dev libcurl4-openssl-dev zlib1g-dev \
    # Clean cache
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Install GCC 7 and comprehensive debugging tools
RUN apt-get update && apt-get install -y \
    gcc-7 g++-7 cmake make pkg-config \
    # Debugging tools
    gdb valgrind strace ltrace \
    # Memory debugging
    libc6-dbg \
    # Performance profiling
    perf-tools-unstable linux-tools-generic \
    # Static analysis
    cppcheck clang-tidy \
    # Binary analysis
    binutils \
    # Network debugging
    tcpdump netstat-nat \
    # System monitoring
    sysstat iotop \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Set GCC 7 as default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-7 70 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-7 70 \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-7 70 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-7 70

# Check versions
RUN echo "Architecture: $(uname -m)" && \
    echo "GCC version: $(gcc --version | head -n1)" && \
    echo "G++ version: $(g++ --version | head -n1)" && \
    echo "GDB version: $(gdb --version | head -n1)" && \
    echo "Valgrind version: $(valgrind --version)"

# Create user with full permissions
RUN useradd -m -s /bin/bash developer && \
    echo 'developer ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers && \
    usermod -aG sudo developer

# Create working directory
WORKDIR /app
# Set directory permissions
RUN chown -R developer:developer /app

USER developer