#ifndef RAPTURE__ASSERT_H
#define RAPTURE__ASSERT_H

#include <cassert>

namespace Rapture {

#ifdef _NDEBUG

#define RP_ASSERT(asserting, msg) ;
#else

#define RP_ASSERT(expr, ...) assert((expr) && "" __VA_ARGS__);

#endif //_NDBUG

} // namespace Rapture

#endif //  RAPTURE__ASSERT_H
