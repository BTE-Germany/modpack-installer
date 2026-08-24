#ifndef CANCELTOKEN_H
#define CANCELTOKEN_H

#include <atomic>
#include <memory>

/**
 * Cooperative cancellation flag shared between the GUI thread (which requests
 * the cancellation) and the worker thread (which polls it).
 */
class CancelToken
{
public:
    void cancel() { cancelled.store(true, std::memory_order_relaxed); }
    void reset() { cancelled.store(false, std::memory_order_relaxed); }
    bool isCancelled() const { return cancelled.load(std::memory_order_relaxed); }

private:
    std::atomic_bool cancelled{false};
};

using CancelTokenPtr = std::shared_ptr<CancelToken>;

#endif // CANCELTOKEN_H
