_Pragma("once");

#include "cocpp/event/co_event.h"
#include "cocpp/core/co_ctx.h"
#include "cocpp/core/co_ctx_config.h"
#include "cocpp/core/co_define.h"
#include "cocpp/core/co_env.h"
#include "cocpp/core/co_manager.h"
#include "cocpp/core/co_return_value.h"
#include "cocpp/core/co_timer.h"
#include "cocpp/utils/co_defer.h"
#include "cocpp/utils/co_noncopyable.h"

#include <chrono>
#include <functional>
#include <initializer_list>
#include <optional>
#include <thread>

CO_NAMESPACE_BEGIN

class co_manager;
class co_ctx;
class co;

template <typename T>
concept CoIsVoid = std::is_same_v<std::decay_t<T>,
                                  void>;

template <typename T>
concept CoIsNotVoid = !std::is_same_v<std::decay_t<T>, void>;

class co final
{
private:
    mutable co_ctx *ctx__;
    template <typename Func, typename... Args> //
    void init__(co_ctx_config config, Func &&func, Args &&...args);

    co(const co &) = delete;
    co &operator=(const co &) = delete;

public:
    CoMemberMethodProxyStatic(co_manager::instance(), create_env);
    CoMemberMethodProxyStatic(co_manager::instance(), current_env);
    CoMemberMethodProxyStatic(co_manager::instance(), set_env_shared_stack_size);
    CoMemberMethodProxyStatic(co_manager::instance(), set_base_schedule_thread_count);
    CoMemberMethodProxyStatic(co_manager::instance(), set_max_schedule_thread_count);
    CoMemberMethodProxyStatic(co_manager::instance(), set_timer_tick_duration);
    CoMemberMethodProxyStatic(co_manager::instance(), timing_duration);

    CoMemberMethodProxyStatic(CoCurrentEnv(), schedule_in_this_thread);

    CoMemberMethodProxyWithPrefix(ctx__, state, ctx_);
    CoMemberMethodProxyWithPrefix(ctx__, config, ctx_);
    CoMemberMethodProxyWithPrefix(ctx__, env, ctx_);
    CoMemberMethodProxyWithPrefix(ctx__, set_priority, ctx_);
    CoMemberMethodProxyWithPrefix(ctx__, priority, ctx_);
    std::string name() const;
    co_id id() const;

    CoMemberMethodProxyWithPrefix(ctx__, finished, ctx_);
    CoMemberMethodProxyWithPrefix(ctx__, priority_changed, ctx_);

    template <typename Func, typename... Args>
    co(Func &&func, Args &&...args);
    template <typename Func, typename... Args>
    co(std::initializer_list<std::function<void(co_ctx_config &)> > opts, Func &&func, Args &&...args);
    template <CoIsNotVoid Ret>
    Ret wait() const;
    template <CoIsVoid Ret>
    Ret wait() const;
    template <class Rep, class Period>
    std::optional<co_return_value> wait_for(const std::chrono::duration<Rep, Period> &wait_duration) const;
    void detach();
    void join();

    co(co &&other) noexcept;
    co &operator=(co &&other) noexcept;

    ~co();
};

namespace this_co
{
co_id id();
std::string name();
void yield();
template <class Rep, class Period>
void sleep_for(const std::chrono::duration<Rep, Period> &sleep_duration);
template <class Clock, class Duration>
void sleep_until(const std::chrono::time_point<Clock, Duration> &abs_time);
}

#define CoLocal(name, type) []() -> type & {                                    \
    return cocpp::co::current_env()->current_ctx()->local_storage<type>(#name); \
}()

///

template <typename Func, typename... Args>
void co::init__(co_ctx_config config, Func &&func, Args &&...args)
{
    std::function<void(co_any &)> entry;
    if constexpr (std::is_same_v<std::invoke_result_t<std::decay_t<Func>, std::decay_t<Args>...>, void>)
    {
        entry = [... args = std::forward<Args>(args), func = std::forward<Func>(func)](co_any &ret) mutable {
            std::forward<Func>(func)(std::forward<Args>(args)...);
        };
    }
    else
    {
        entry = [... args = std::forward<Args>(args), func = std::forward<Func>(func)](co_any &ret) mutable {
            ret = std::forward<Func>(func)(std::forward<Args>(args)...);
        };
    }

    ctx__ = co_manager::instance()->create_and_schedule_ctx(config, entry, true);
}

template <typename Func, typename... Args>
co::co(Func &&func, Args &&...args)
{
    init__(co_ctx_config {}, std::forward<Func>(func), std::forward<Args>(args)...);
}

template <typename Func, typename... Args>
co::co(std::initializer_list<std::function<void(co_ctx_config &)> > opts, Func &&func, Args &&...args)
{
    co_ctx_config config;
    for (auto &&cb : opts)
    {
        cb(config);
    }
    init__(config, std::forward<Func>(func), std::forward<Args>(args)...);
}

template <CoIsNotVoid Ret>
Ret co::wait() const
{
    return CoCurrentEnv()->wait_ctx(ctx__);
}

template <CoIsVoid Ret>
Ret co::wait() const
{
    if (!ctx__)
    {
        return;
    }
    CoCurrentEnv()->wait_ctx(ctx__);
}

template <typename Rep, typename Period>
std::optional<co_return_value> co::wait_for(const std::chrono::duration<Rep, Period> &wait_duration) const
{
    return CoCurrentEnv()->wait_ctx(ctx__, std::chrono::duration_cast<std::chrono::nanoseconds>(wait_duration));
}

template <class Rep, class Period>
void this_co::sleep_for(const std::chrono::duration<Rep, Period> &sleep_duration)
{
    return sleep_until(std::chrono::steady_clock::now() + sleep_duration);
}

template <class Clock, class Duration>
void this_co::sleep_until(const std::chrono::time_point<Clock, Duration> &abs_time)
{
    auto env = CoCurrentEnv();
    auto ctx = env->current_ctx();
    auto timer = co_timer::create(nullptr, abs_time);
    timer->set_expire_callback([ctx] {
        ctx->leave_wait_resource_state(ctx);
    });
    env->lock_schedule();
    ctx->enter_wait_resource_state(ctx);
    timer->start();
    env->unlock_schedule();
    yield();
}

CO_NAMESPACE_END
