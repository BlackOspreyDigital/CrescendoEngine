#!/bin/bash

# Exit on any error
set -e

echo "Building Crescendo Engine for WebAssembly"

# Create a seperate build folder for the web
mkdir -p build_web
cd build_web

# Run Emscripten wrapper
emcmake cmake .. 

# Compile 
emmake make -j$(nproc)

echo "Build Complete"
echo "Starting Local server on http://localhost:8000/crescendo_engine.html"
echo "(Press Ctrl+C to stop)"

# Start a local python web server to host the files
python3 -m http.server 8000