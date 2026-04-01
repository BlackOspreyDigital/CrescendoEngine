#!/bin/bash
set -e

echo "Building Crescendo Engine for WebAssembly"
mkdir -p build_web
cd build_web

# Run Emscripten wrapper
emcmake cmake .. 

# Compile 
emmake make -j$(nproc)

echo "Build Complete"
echo "Starting Local server on https://localhost:8000/crescendo_engine.html"
echo "(Press Ctrl+C to stop)"

# emrun automatically handles the COOP and COEP security headers
emrun --port 8000 crescendo_engine.html