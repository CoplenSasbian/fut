// The definitions behind services.hpp. No annotations here: the declarations
// in the header are what the scan sees.

#include "services.hpp"

namespace demo {

boost::asio::io_context* g_io = nullptr;
fut::log::group_ref log{};
fut::log::group_ref audit_log{};

} // namespace demo
