#include "BufferLayout.h"

namespace Rapture {

BufferAttributeID stringToBufferAttributeID(const std::string &str)
{
    if (str == "POSITION") return BufferAttributeID::POSITION;
    else if (str == "NORMAL") return BufferAttributeID::NORMAL;
    else if (str == "TANGENT") return BufferAttributeID::TANGENT;
    else if (str == "BITANGENT") return BufferAttributeID::BITANGENT;
    else if (str == "COLOR") return BufferAttributeID::COLOR;
    else if (str == "WEIGHTS_0") return BufferAttributeID::WEIGHTS_0;
    else if (str == "WEIGHTS_1") return BufferAttributeID::WEIGHTS_1;
    else if (str == "JOINTS_0") return BufferAttributeID::JOINTS_0;
    else if (str == "JOINTS_1") return BufferAttributeID::JOINTS_1;
    else if (str == "TEXCOORD_0") return BufferAttributeID::TEXCOORD_0;
    else if (str == "TEXCOORD_1") return BufferAttributeID::TEXCOORD_1;
    else {
        throw std::runtime_error("Invalid buffer attribute ID: " + str);
    }
}

std::string bufferAttributeIDToString(BufferAttributeID id)
{
    switch (id) {
    case BufferAttributeID::POSITION:
        return "POSITION";
    case BufferAttributeID::NORMAL:
        return "NORMAL";
    case BufferAttributeID::TANGENT:
        return "TANGENT";
    case BufferAttributeID::BITANGENT:
        return "BITANGENT";
    case BufferAttributeID::COLOR:
        return "COLOR";
    case BufferAttributeID::WEIGHTS_0:
        return "WEIGHTS_0";
    case BufferAttributeID::WEIGHTS_1:
        return "WEIGHTS_1";
    case BufferAttributeID::JOINTS_0:
        return "JOINTS_0";
    case BufferAttributeID::JOINTS_1:
        return "JOINTS_1";
    case BufferAttributeID::TEXCOORD_0:
        return "TEXCOORD_0";
    case BufferAttributeID::TEXCOORD_1:
        return "TEXCOORD_1";
    default:
        return "UNKNOWN";
    }
}

} // namespace Rapture