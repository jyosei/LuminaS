#pragma once
#include <atomic>
#include <memory>

// A request owns the source; its immutable job snapshot owns a token. Cancellation
// never revokes ownership of the frame or buffers still used by that job.
using CancellationToken = std::shared_ptr<const std::atomic_bool>;
class CancellationSource {
public:
    CancellationSource():state(std::make_shared<std::atomic_bool>(false)){}
    CancellationToken token() const{return state;}
    void cancel() const{state->store(true,std::memory_order_relaxed);}
private:
    std::shared_ptr<std::atomic_bool> state;
};
inline bool isCanceled(const CancellationToken &token){
    return token&&token->load(std::memory_order_relaxed);
}
