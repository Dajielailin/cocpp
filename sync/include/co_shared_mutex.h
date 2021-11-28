_Pragma("once");

#include <list>
#include <mutex>

#include "co_define.h"
#include "co_mutex.h"
#include "co_noncopyable.h"

CO_NAMESPACE_BEGIN

class co_ctx* ctx;

class co_shared_mutex : private co_noncopyable
{
private:
    enum class lock_type
    {
        unique,
        shared
    };

    struct lock_context
    {
        lock_type type;
        co_ctx*   ctx;

        bool operator==(const lock_context& other) const;
    };

    co_mutex                mu_lock__;
    std::list<lock_context> wait_list__;
    std::list<lock_context> owners__;

    void unlock_reader__(co_ctx* ctx);
    void unlock_writer__(co_ctx* ctx);
    void reader_wait__(co_ctx* ctx, std::unique_lock<co_mutex>& lck);
    void unlock__(const lock_context& context);

public:
    void lock();
    void unlock();
    bool try_lock();
    void lock_shared();
    void unlock_shared();
    bool try_lock_shared();
};

CO_NAMESPACE_END