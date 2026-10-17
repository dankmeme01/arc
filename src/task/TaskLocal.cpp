#include <arc/task/TaskLocal.hpp>

namespace arc {

std::atomic<uint64_t> _g_nextTaskLocalKey{0};

}
