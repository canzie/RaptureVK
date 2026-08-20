#include "StaticMesh.h"

#include "core/utils/Log.h"

namespace Rapture {

StaticMesh::StaticMesh(MeshAllocatorParams &params) : Mesh(params)
{
    for (const BufferAttribute &attrib : params.bufferLayout.buffer_attribs) {
        if (attrib.name == BufferAttributeID::JOINTS_0 || attrib.name == BufferAttributeID::WEIGHTS_0 ||
            attrib.name == BufferAttributeID::JOINTS_1 || attrib.name == BufferAttributeID::WEIGHTS_1) {
            RP_CORE_ERROR("static mesh carries {}, which only a skeletal mesh deforms with",
                          bufferAttributeIDToString(attrib.name));
        }
    }
}

} // namespace Rapture
