_Pragma("once");

#include "co_ctx_factory.h"
#include "co_define.h"
#include "co_event.h"
#include "co_flag_manager.h"
#include "co_noncopyable.h"
#include "co_object_pool.h"
#include "co_return_value.h"
#include "co_scheduler.h"
#include "co_sleep_controller.h"
#include "co_spinlock.h"
#include "co_stack_factory.h"
#include "co_state_manager.h"
#include "co_type.h"

#include <atomic>
#include <bitset>
#include <chrono>
#include <condition_variable>
#include <future>
#include <list>
#include <optional>
#include <shared_mutex>
#include <thread>

CO_NAMESPACE_BEGIN

class co_manager;
class co_ctx;

class co_env final : private co_noncopyable,
                     public co_flag_manager<CO_ENV_FLAG_MAX>,
                     public co_state_manager<co_env_state, co_env_state::created, co_env_state::destorying>
{
    RegCoEvent(task_finished);
    RegCoEvent(ctx_added, co_ctx*);                        // 被加入的ctx
    RegCoEvent(wait_ctx_timeout, co_ctx*);                 // 等待的ctx
    RegCoEvent(wait_ctx_finished, co_ctx*);                // 等待的ctx
    RegCoEvent(state_changed, co_env_state, co_env_state); // 原状态，当期状态
    RegCoEvent(switched_to, co_ctx*);                      // 切换到
    RegCoEvent(ctx_removed, co_ctx*);                      // 删除的ctx
    RegCoEvent(schedule_stopped);
    RegCoEvent(schedule_started);
    RegCoEvent(idle_waited);
    RegCoEvent(idle_waked);
    RegCoEvent(all_ctx_removed);
    RegCoEvent(scheduled_flag_reset);
    RegCoEvent(schedule_locked);
    RegCoEvent(schedule_unlocked);
    RegCoEvent(ctx_taken, co_ctx*);
    RegCoEvent(ctx_initted, co_ctx*);
    RegCoEvent(shared_stack_saved, co_ctx*);
    RegCoEvent(shared_stack_restored, co_ctx*);
    RegCoEvent(moveable_ctx_taken, std::list<co_ctx*>);
    RegCoEvent(this_thread_converted_to_schedule_thread, std::thread::id);

private:
    std::future<void> worker__;

    co_sleep_controller sleep_controller__;

    co_ctx_factory* const   ctx_factory__ { co_ctx_factory::instance() };
    co_stack_factory* const stack_factory__ { co_stack_factory::instance() };

    co_stack*     shared_stack__ { nullptr };
    co_ctx* const idle_ctx__ { nullptr };

    co_tid schedule_thread_tid__ {};

    bool safepoint__ { false };

    std::vector<std::list<co_ctx*>> all_normal_ctx__ { CO_MAX_PRIORITY };
    std::unordered_set<co_ctx*>     blocked_ctx__;
    mutable co_spinlock             mu_normal_ctx__ { co_spinlock::lock_type::in_thread };
    mutable co_spinlock             mu_blocked_ctx__ { co_spinlock::lock_type::in_thread };
    co_ctx*                         curr_obj__ { nullptr };
    int                             min_priority__ = 0;

    co_env(co_stack* shared_stack, co_ctx* idle_ctx, bool create_new_thread);

    void    start_schedule_routine__();
    void    remove_detached_ctx__();
    void    remove_all_ctx__();
    co_ctx* next_ctx__();

    void               save_shared_stack__(co_ctx* ctx);
    void               restore_shared_stack__(co_ctx* ctx);
    void               switch_shared_stack_ctx__();
    void               switch_normal_ctx__();
    bool               need_sleep__();
    co_ctx*            choose_ctx__();
    std::list<co_ctx*> all_ctx__();
    std::list<co_ctx*> all_scheduleable_ctx__() const;
    bool               can_schedule__() const;
    void               update_min_priority__(int priority);

    static size_t get_valid_stack_size__(co_ctx* ctx);
    static void   update_ctx_state__(co_ctx* curr, co_ctx* next);

    struct
    {
        co_ctx* from { nullptr };
        co_ctx* to { nullptr };
        bool    need_switch { false };
    } shared_stack_switch_context__;

public:
    void add_ctx(co_ctx* ctx);
    void receive_ctx(co_ctx* ctx);

    std::optional<co_return_value> wait_ctx(co_ctx*                         ctx,
                                            const std::chrono::nanoseconds& timeout);
    co_return_value                wait_ctx(co_ctx* ctx);
    int                            workload() const;
    void                           schedule_switch(bool safe_return);
    void                           remove_ctx(co_ctx* ctx);
    co_ctx*                        current_ctx() const;
    void                           stop_schedule();
    void                           start_schedule();
    void                           schedule_in_this_thread();
    void                           reset_scheduled_flag();
    bool                           can_auto_destroy() const;
    co_tid                         schedule_thread_tid() const;
    bool                           can_schedule_ctx() const;
    bool                           is_blocked() const;
    bool                           prepare_to_switch(co_ctx*& from, co_ctx*& to);
    void                           set_safepoint();
    void                           reset_safepoint();
    bool                           safepoint() const;
    void                           change_priority(int old, co_ctx* ctx);
    void                           ctx_leave_wait_state(co_ctx* ctx);
    void                           ctx_enter_wait_state(co_ctx* ctx);
    std::list<co_ctx*>             take_all_movable_ctx();

    CoMemberMethodProxy(&sleep_controller__, sleep_if_need);
    CoMemberMethodProxy(&sleep_controller__, wake_up);

    friend class co_object_pool<co_env>;
    friend class co_env_factory;
};

extern thread_local co_env* current_env__;

CO_NAMESPACE_END