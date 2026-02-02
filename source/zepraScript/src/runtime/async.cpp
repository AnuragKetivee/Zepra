#include <zeprascript/runtime/async.hpp>

namespace Zepra::Runtime {

AsyncContext::AsyncContext(Promise* promise)
    : promise_(promise), state_(0) {}

} // namespace Zepra::Runtime
