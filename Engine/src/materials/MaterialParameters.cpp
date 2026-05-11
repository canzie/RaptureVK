#include "MaterialParameters.h"

namespace Rapture {

std::string_view parameterIdToString(ParameterID id)
{
    const ParamInfo* info = getParamInfo(id);
    return info ? info->name : "UNKNOWN";
}

} // namespace Rapture
