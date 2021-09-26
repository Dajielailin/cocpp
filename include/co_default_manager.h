#pragma once

#include "co_default_ctx_factory.h"
#include "co_default_env_factory.h"
#include "co_default_stack_factory.h"
#include "co_define.h"
#include "co_manager.h"
#include "co_singleton.h"

#include <condition_variable>
#include <future>
#include <list>
#include <map>
#include <mutex>

class co_default_manager : public co_manager,
                           public co_singleton<co_default_manager>
{
private:
    std::list<co_env*>   env_list__;
    std::recursive_mutex mu_env_list__;

    std::atomic<bool> clean_up__ = { false };

    std::list<std::future<void>> background_task__;
    std::list<co_env*>           expired_env__;
    mutable std::recursive_mutex mu_clean_up__;
    std::condition_variable_any  cond_expired_env__;

    std::atomic<unsigned int> exist_env_count__ { 0 };
    std::atomic<unsigned int> base_thread_count__ { std::thread::hardware_concurrency() };
    std::atomic<unsigned int> max_thread_count__ { std::thread::hardware_concurrency() * 2 };

    co_scheduler_factory* const scheduler_factory__ { nullptr };
    co_stack_factory* const     stack_factory__ { nullptr };
    co_ctx_factory* const       ctx_factory__ { nullptr };
    co_env_factory* const       env_factory__ { nullptr };

    mutable std::mutex                           mu_timing_duration__;
    std::chrono::high_resolution_clock::duration timing_duration__ { std::chrono::milliseconds(10) };

    size_t default_shared_stack_size__ = CO_DEFAULT_STACK_SIZE;

    co_env* create_env__();
    bool    can_schedule_ctx__(co_env* env) const;

    void clean_env_routine__();
    void timing_routine__();
    bool is_blocked__(co_env* env) const;

    void redistribute_ctx__();
    void destroy_redundant_env__();

    co_default_manager(co_scheduler_factory* scheduler_factory,
                       co_stack_factory*     stack_factory,
                       co_ctx_factory*       ctx_factory,
                       co_env_factory*       env_factory);

public:
    co_env*               get_best_env() override;
    void                  set_env_shared_stack_size(size_t size) override;
    co_env_factory*       env_factory() override;
    co_ctx_factory*       ctx_factory() override;
    co_stack_factory*     stack_factory() override;
    co_scheduler_factory* scheduler_factory() override;
    void                  remove_env(co_env* env) override;
    void                  create_env_from_this_thread() override;
    co_env*               current_env() override;
    void                  set_clean_up() override;
    bool                  clean_up() const override;
    void                  set_base_schedule_thread_count(size_t base_thread_count) override;
    void                  set_max_schedule_thread_count(size_t max_thread_count) override;
    void                  set_timing_duration(
                         const std::chrono::high_resolution_clock::duration& duration) override;
    const std::chrono::high_resolution_clock::duration& timing_duration() const override;

    friend class co_singleton<co_default_manager>;
};