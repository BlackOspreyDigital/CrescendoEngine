#!/bin/bash

# 1. Check if build folder exists. If not, create it and run CMake.
if [ ! -d "build" ]; then
    echo "[Script] Build directory missing. Generating..."
    mkdir build
    cd build
    cmake ..
else
    cd build
fi

# 2. Compile
make -j$(nproc)

# 3. Run if successful
if [ $? -eq 0 ]; then
    echo "--- LAUNCHING ENGINE ---"
    cd ..
    ./build/crescendo_engine 
else
    echo "--- BUILD FAILED ---"
fi