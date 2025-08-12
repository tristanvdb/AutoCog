#!/bin/bash -ex

# Simple AutoCog Setup Script
# Creates local .venv with llama.cpp and AutoCog

echo "Setting up AutoCog in local .venv..."

# Create and activate virtual environment
python3 -m venv .venv
source .venv/bin/activate

# Upgrade pip and install build dependencies
pip install --upgrade pip setuptools wheel pybind11 numpy

exit 0

# Build and install llama.cpp to .venv
echo "Building llama.cpp..."
cd vendors/llama
cmake -B build \
    -DCMAKE_INSTALL_PREFIX="$(pwd)/../../.venv" \
    -DLLAMA_BUILD_COMMON=OFF \
    -DLLAMA_LOG_DISABLE=ON \
    -DCMAKE_BUILD_TYPE=Release

make -C build -j$(nproc) install
cd ../..

# Set library path for AutoCog build
export LD_LIBRARY_PATH="$(pwd)/.venv/lib:$LD_LIBRARY_PATH"
export CMAKE_PREFIX_PATH="$(pwd)/.venv"
export CMAKE_BUILD_PARALLEL_LEVEL=$(nproc)

# Build and install AutoCog
echo "Building AutoCog..."
pip install -e .

# Test installation
echo "Testing installation..."
python3 -c "import autocog.llama; print('Success!')"

echo "Setup complete!"
echo ""
echo "To use AutoCog:"
echo "  source .venv/bin/activate"
echo "  python3 tests/autocog/llama/roundtrip_tokenization.py path/to/model.gguf"

