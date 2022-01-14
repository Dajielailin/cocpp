_Pragma("once");

#include "cocpp/core/co_define.h"
#include "cocpp/core/co_type.h"
#include "cocpp/sync/co_spinlock.h"
#include "cocpp/utils/co_noncopyable.h"

#include <functional>
#include <list>
#include <map>
#include <utility>

CO_NAMESPACE_BEGIN

template <typename... Args>
class co_event final : private co_noncopyable
{
private:
    // FIXME: 此处如果使用unordered_map，则会有低概率的崩溃问题，原因未知
    std::map<int, std::function<void(Args... args)>> cb_list__;
    mutable co_spinlock                              mu_cb_list__ { co_spinlock::lock_type::in_thread };
    co_event_handler                                 current_handler__ { 0 };

public:
    co_event_handler sub(std::function<void(Args... args)> cb);
    void             pub(Args... args) const;
    void             unsub(co_event_handler h);
};

// 模板实现

template <typename... Args>
co_event_handler co_event<Args...>::sub(std::function<void(Args... args)> cb)
{
    std::lock_guard<co_spinlock> lck(mu_cb_list__);
    cb_list__[current_handler__] = cb;
    return current_handler__++;
}

template <typename... Args>
void co_event<Args...>::unsub(co_event_handler h)
{
    std::lock_guard<co_spinlock> lck(mu_cb_list__);
    cb_list__.erase(h);
}

template <typename... Args>
void co_event<Args...>::pub(Args... args) const
{
    std::lock_guard<co_spinlock> lck(mu_cb_list__);
    for (auto& [_, cb] : cb_list__)
    {
        cb(args...);
    }
}

#define RegCoEvent(eventName, ...)       \
private:                                 \
    co_event<__VA_ARGS__> eventName##__; \
                                         \
public:                                  \
    co_event<__VA_ARGS__>& eventName()   \
    {                                    \
        return eventName##__;            \
    }

CO_NAMESPACE_END