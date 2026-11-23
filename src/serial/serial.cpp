#include "serial.h"

namespace serial
{
    void hook_before_tx(const std::function<void(const char *, size_t)>& hook)
    {
        before_tx_hooks.push_back(hook);
    }

    void hook_before_rx(const std::function<void(const char *, size_t)>& hook)
    {
        before_rx_hooks.push_back(hook);
    }
} // namespace serial