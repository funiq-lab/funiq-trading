#!/bin/bash

echo "🐳 Building funiq-trading development environment..."

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    echo "❌ Docker not found"
    exit 1
fi

# Build image with platform specification (force x86_64 even on ARM)
echo "📦 Building for linux/amd64 platform..."
docker build --platform linux/amd64 -t funiq-trading-dev .

if [ $? -eq 0 ]; then
    echo "✅ Build successful"
    echo ""
    echo "Usage:"
    echo "  ./run.sh          # Interactive shell"
    echo "  ./run.sh build    # Build project"
    echo ""
    echo "ℹ️  Container will run in x86_64 emulation mode on ARM hosts"
else
    echo "❌ Build failed"
    exit 1
fi