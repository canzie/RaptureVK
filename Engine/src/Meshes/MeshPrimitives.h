#pragma once

#include "Mesh.h"


#include <vector>
#include <cstdint>
#include <memory>


// A : factory that shits out meshes
// B : inherit from a mesh just for the initialisation after which the mesh takes over <- shit
// C : Composition of a mesh


// Do we generate a unit primitive or adjust the vertices themselves?


namespace Rapture {


class Primitives {
    public:
        static Mesh CreateCube();
        static Mesh CreateSphere(float radius, uint32_t segments);
        static Mesh CreatePlane(float segments=1);
        static Mesh CreateLine(float start, float end);



    private:



};



}