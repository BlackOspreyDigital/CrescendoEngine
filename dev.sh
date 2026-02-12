#!/bin/bash
cd build
make -j$(nproc)

if [ $? -eq 0 ]; then
    echo "--- LAUNCHING ENGINE ---"
    cd ..
    ./build/crescendo_engine # <--- Fixed name
else
    echo "--- BUILD FAILED ---"
fi