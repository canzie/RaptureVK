#pragma once

#include "StaticMesh.h"

#include <cstdint>
#include <memory>
#include <vector>

// A : factory that shits out meshes
// B : inherit from a mesh just for the initialisation after which the mesh takes over <- shit
// C : Composition of a mesh

// Do we generate a unit primitive or adjust the vertices themselves?

namespace Rapture {

class Primitives {
  public:
    static StaticMesh CreateCube();
    static StaticMesh CreateSphere(float radius, uint32_t segments);
    static StaticMesh CreatePlane(float segments = 1);
    static StaticMesh CreateLine(float start, float end);

  private:
};

} // namespace Rapture
