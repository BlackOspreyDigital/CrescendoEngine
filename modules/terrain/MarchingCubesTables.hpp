#pragma once 

namespace Crescendo {
namespace Terrain {

    // Edge table - Tells us which of the 12 edges of the cube are intersected
    // based on our 8-bit cube index (0-255)

    extern const int edgeTable[256];

    // Tri table - Tells us which vertices to connect to form the traingles.
    // -1 means we are done drawing triangles for this cube configuration.

    extern const int triTable[256][16];

}
}