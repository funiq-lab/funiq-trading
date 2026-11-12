#!/bin/bash

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# 默认容器名，可以通过环境变量覆盖支持多开
CONTAINER_NAME="${FUNIQ_CONTAINER_NAME:-funiq-trading-dev}"

# 端口配置
HOST_PORT="${FUNIQ_HOST_PORT:-8080}"
CONTAINER_PORT="${FUNIQ_CONTAINER_PORT:-8080}"

# Parse arguments
COMMAND=""
FORCE_NEW=""
BUILD_TYPE="Release"
ENABLE_ASAN=""
ENABLE_UBSAN=""
ENABLE_TSAN=""
BUILD_PRODUCTION="OFF"

while [[ $# -gt 0 ]]; do
    case $1 in
        build|build-debug|build-release|build-production|clean|dev|debug)
            COMMAND="$1"
            shift
            ;;
        -n|--name)
            CONTAINER_NAME="$2"
            shift 2
            ;;
        -p|--port)
            HOST_PORT="$2"
            shift 2
            ;;
        --container-port)
            CONTAINER_PORT="$2"
            shift 2
            ;;
        --new)
            FORCE_NEW="true"
            shift
            ;;
        --asan)
            ENABLE_ASAN="ON"
            shift
            ;;
        --ubsan)
            ENABLE_UBSAN="ON"
            shift
            ;;
        --tsan)
            ENABLE_TSAN="ON"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [COMMAND] [OPTIONS]"
            echo ""
            echo "Commands:"
            echo "  build                  Build project (Release/Production mode)"
            echo "  build-debug            Build project in Debug mode"
            echo "  build-release          Build project in Release/Production mode"
            echo "  build-production       Build project in Production mode (same as release)"
            echo "  clean                  Clean build files"
            echo "  dev                    Development mode with file watching"
            echo "  debug                  Enter container with debug tools"
            echo ""
            echo "Options:"
            echo "  -n, --name NAME        Container name (default: funiq-trading-dev)"
            echo "  -p, --port PORT        Host port mapping (default: 8080)"
            echo "      --container-port PORT  Container port (default: 8080)"
            echo "      --new              Force create new container"
            echo "      --asan             Enable AddressSanitizer (Debug only)"
            echo "      --ubsan            Enable UndefinedBehaviorSanitizer (Debug only)"
            echo "      --tsan             Enable ThreadSanitizer (Debug only, exclusive with ASAN)"
            echo ""
            echo "Build Modes:"
            echo "  Debug:     Fast compilation, debugging symbols, no optimization"
            echo "  Release:   Production ready, static linking, optimized"
            echo ""
            echo "Examples:"
            echo "  $0                     # Enter/create default container (port 8080)"
            echo "  $0 -p 9090             # Use host port 9090"
            echo "  $0 build -p 8081      # Build and map to port 8081"
            echo "  $0 build-debug --asan # Build in Debug mode with AddressSanitizer"
            echo "  $0 build-production   # Build for production (static linking)"
            echo "  $0 debug               # Enter container with debug setup"
            echo "  $0 -n dev-2 -p 9090   # Use different container name and port"
            echo "  $0 --new              # Force create new container"
            echo ""
            echo "Environment Variables:"
            echo "  FUNIQ_CONTAINER_NAME   Override default container name"
            echo "  FUNIQ_HOST_PORT        Override default host port (8080)"
            echo "  FUNIQ_CONTAINER_PORT   Override default container port (8080)"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use '$0 --help' for help"
            exit 1
            ;;
    esac
done

# Set build type and production mode based on command
case $COMMAND in
    build-debug)
        BUILD_TYPE="Debug"
        BUILD_PRODUCTION="OFF"
        COMMAND="build"
        ;;
    build-release|build-production)
        BUILD_TYPE="Release"
        BUILD_PRODUCTION="ON"
        COMMAND="build"
        ;;
    build)
        # Default build is production-ready Release
        BUILD_TYPE="Release"
        BUILD_PRODUCTION="ON"
        ;;
esac

# Helper functions
container_exists() {
    docker ps -a --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"
}

container_running() {
    docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"
}

create_container() {
    echo "🚀 Creating new container: $CONTAINER_NAME"
    echo "🌐 Port mapping: $HOST_PORT:$CONTAINER_PORT"
    
    docker run -d -it \
        --name "$CONTAINER_NAME" \
        --platform linux/amd64 \
        --privileged \
        --security-opt seccomp=unconfined \
        --cap-add=SYS_PTRACE \
        --cap-add=SYS_ADMIN \
        -v "$PROJECT_DIR:/app" \
        -p "$HOST_PORT:$CONTAINER_PORT" \
        funiq-trading-dev \
        bash
    
    if [ $? -eq 0 ]; then
        echo "✅ Container created successfully (with debug capabilities)"
        echo "🌐 Network API available at: http://localhost:$HOST_PORT"
    else
        echo "❌ Failed to create container"
        exit 1
    fi
}

start_container() {
    echo "🔄 Starting container: $CONTAINER_NAME"
    docker start "$CONTAINER_NAME"
}

enter_container() {
    echo "🚪 Entering container: $CONTAINER_NAME"
    docker exec -it "$CONTAINER_NAME" bash
}

execute_in_container() {
    local cmd="$1"
    docker exec -it -w /app "$CONTAINER_NAME" bash -c "$cmd"
}

# Main logic
echo "📦 Container: $CONTAINER_NAME"
echo "📂 Project: $PROJECT_DIR"

# Force new container if requested
if [ "$FORCE_NEW" = "true" ]; then
    if container_exists; then
        echo "🗑️  Removing existing container..."
        docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1
    fi
fi

# Ensure container exists and is running
if ! container_exists; then
    create_container
elif ! container_running; then
    start_container
fi

# Execute command or enter shell
case $COMMAND in
    build)
        if [ "$BUILD_PRODUCTION" = "ON" ]; then
            echo "🏭 Building project in Production mode (Release + Static Linking)..."
        else
            echo "🔨 Building project in $BUILD_TYPE mode..."
        fi
        
        # Build cmake command
        CMAKE_CMD="cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE"
        
        # Add production mode
        if [ "$BUILD_PRODUCTION" = "ON" ]; then
            CMAKE_CMD="$CMAKE_CMD -DBUILD_PRODUCTION=ON"
            echo "  📦 Static linking enabled for portable deployment"
            echo "  🎯 Output: self-contained libquote_service.so"
        fi
        
        if [ "$BUILD_TYPE" = "Debug" ]; then
            if [ -n "$ENABLE_ASAN" ]; then
                CMAKE_CMD="$CMAKE_CMD -DENABLE_ASAN=$ENABLE_ASAN"
                echo "  🐛 AddressSanitizer enabled"
            fi
            if [ -n "$ENABLE_UBSAN" ]; then
                CMAKE_CMD="$CMAKE_CMD -DENABLE_UBSAN=$ENABLE_UBSAN"
                echo "  🐛 UndefinedBehaviorSanitizer enabled"
            fi
            if [ -n "$ENABLE_TSAN" ]; then
                CMAKE_CMD="$CMAKE_CMD -DENABLE_TSAN=$ENABLE_TSAN"
                echo "  🐛 ThreadSanitizer enabled"
            fi
        fi
        
        execute_in_container "
            mkdir -p build && cd build &&
            echo 'Running: $CMAKE_CMD ..'
            $CMAKE_CMD .. && make -j\$(nproc)
            
        "
        ;;
    clean)
        echo "🧹 Cleaning build files in container..."
        execute_in_container "rm -rf build"
        ;;
    dev)
        echo "🔧 Starting development mode..."
        execute_in_container "
            echo '🚀 Development mode - use inotifywait for file watching'
            echo '🐛 Debug capabilities enabled (GDB, Valgrind, etc.)'
            echo ''
            echo 'Available build commands:'
            echo '  🔨 Debug build:       mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j\$(nproc)'
            echo '  🏭 Production build:  mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_PRODUCTION=ON .. && make -j\$(nproc)'
            echo '  🐛 With AddressSanitizer: cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON ..'
            echo ''
            echo 'Build modes:'
            echo '  Debug:      Fast compilation, debugging symbols, dynamic linking'
            echo '  Production: Optimized, static linking, portable deployment'
            echo ''
            echo 'Type exit to leave container'
            bash
        "
        ;;
    debug)
        echo "🐛 Starting debug environment..."
        execute_in_container "
            echo '🐛 Debug Environment Ready'
            echo ''
            echo 'Available debugging tools:'
            echo '  - gdb: GNU Debugger'
            echo '  - valgrind: Memory error detector'
            echo '  - strace: System call tracer'
            echo '  - AddressSanitizer: Runtime memory error detector'
            echo ''
            echo 'Quick commands:'
            echo '  gdb ./your_program'
            echo '  valgrind --tool=memcheck --leak-check=full ./your_program'
            echo '  strace -o trace.log ./your_program'
            echo ''
            echo 'Core dumps:'
            echo '  ulimit -c unlimited  # Enable core dumps'
            echo '  echo core.%p | sudo tee /proc/sys/kernel/core_pattern'
            echo ''
            echo 'Production debugging:'
            echo '  # Build with debug info: cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_PRODUCTION=ON ..'
            echo ''
            echo 'Type exit to leave container'
            cd /app
            bash
        "
        ;;
    *)
        echo "🚪 Entering development shell..."
        echo "🐛 Debug capabilities enabled"
        echo ""
        echo "Quick start:"
        echo "  ./docker/run.sh build           # Production build (recommended)"
        echo "  ./docker/run.sh build-debug     # Development build"
        enter_container
        ;;
esac