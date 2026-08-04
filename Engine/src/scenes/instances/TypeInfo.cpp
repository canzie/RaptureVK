#include "TypeInfo.h"

#include "utils/rp_assert.h"

namespace Rapture {

TypeInfo::TypeInfo(std::string_view name, const TypeInfo *base) : name(name), base(base), depth(0), chain{}
{
    if (base != nullptr) {
        depth = static_cast<uint8_t>(base->depth + 1);
        for (uint8_t i = 0; i <= base->depth; i++) {
            chain[i] = base->chain[i];
        }
    }

    RP_ASSERT(depth < MAX_TYPE_DEPTH, "instance class hierarchy is deeper than MAX_TYPE_DEPTH");

    chain[depth] = this;
}

} // namespace Rapture
