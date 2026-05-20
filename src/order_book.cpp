// OrderBook is a header-only template; this TU exists so CMake has something
// to compile and so we can pin an explicit instantiation if we ever want one.
#include "nanomatch/order_book.hpp"

namespace nanomatch {
// Explicit instantiation with the default parameters used by main/bench.
template class OrderBook<>;
} // namespace nanomatch
