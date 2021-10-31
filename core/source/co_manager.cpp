#include "co_manager.h"
#include "co_ctx.h"
#include "co_ctx_factory.h"
#include "co_env.h"
#include "co_env_factory.h"
#include "co_stack_factory.h"
#include "co_vos.h"
#include <cassert>
#include <cstddef>
#include <future>
#include <mutex>

CO_NAMESPACE_BEGIN

co_env* co_manager::get_best_env__()
{
    std::lock_guard<std::recursive_mutex> lck(mu_env_list__);
    if (env_list__.empty())
    {
        return create_env(true);
    }

    auto   best_env               = env_list__.front();
    auto   min_workload           = best_env->workload();
    size_t can_schedule_env_count = 0;
    for (auto& env : env_list__)
    {
        if (env->state() == co_env_state::idle)
        {
            best_env_got().pub(env);
            return env;
        }
        if (!env->can_schedule_ctx())
        {
            continue;
        }
        // 统计可用于调度的env数量
        ++can_schedule_env_count;
        if (env->workload() < min_workload)
        {
            min_workload = env->workload();
            best_env     = env;
        }
    }
    // 如果没有可用的env，就创建
    if (best_env == nullptr)
    {
        auto ret = create_env(true);
        best_env_got().pub(ret);
        return ret;
    }

    // 如果可用于调度的env数量小于基础线程数量，创建一个来调度新的ctx
    if (can_schedule_env_count < base_thread_count__)
    {
        auto ret = create_env(true);
        best_env_got().pub(ret);
        return ret;
    }
    best_env_got().pub(best_env);
    return best_env;
}

void co_manager::sub_env_event__(co_env* env)
{
    env->env_task_finished().sub([this, env]() {
        remove_env__(env);
    });
}

void co_manager::sub_ctx_event__(co_ctx* ctx)
{
    ctx->priority_changed().sub([ctx](int old, int new_) {
        ctx->env()->scheduler()->change_priority(old, ctx);
    });
}

co_env* co_manager::create_env(bool dont_auto_destory)
{
    assert(!clean_up__);
    auto env = env_factory__->create_env(default_shared_stack_size__);

    sub_env_event__(env);

    if (dont_auto_destory)
    {
        env->set_flag(CO_ENV_FLAG_DONT_AUTO_DESTORY);
    }
    std::lock_guard<std::recursive_mutex> lck(mu_env_list__);
    env_list__.push_back(env);
    ++exist_env_count__;
    // CO_O_DEBUG("create env : %p", env);

    env_created().pub(env);
    return env;
}

void co_manager::set_env_shared_stack_size(size_t size)
{
    default_shared_stack_size__ = size;
    env_shared_stack_size_set().pub(size);
}

void co_manager::create_background_task__()
{
    background_task__.emplace_back(std::async([this]() {
        clean_env_routine__();
    }));
    background_task__.emplace_back(std::async([this]() {
        timing_routine__();
    }));

    background_task_created().pub();
}

void co_manager::sub_manager_event__()
{
    timing_routine_timout().sub([this] {
        // 每两次超时重新分配一次
        static bool need_redistribute_ctx = false;
        if (need_redistribute_ctx)
        {
            redistribute_ctx__();
        }
        need_redistribute_ctx = !need_redistribute_ctx;
    });
    timing_routine_timout().sub([this] {
        destroy_redundant_env__();
    });
    timing_routine_timout().sub([this] {
        free_mem__();
    });
}

void co_manager::free_mem__()
{
    static size_t pass_tick_count = 0;

    pass_tick_count = (pass_tick_count + 1) % TICKS_COUNT_OF_FREE_MEM;
    if (pass_tick_count == 0)
    {
        if (need_free_mem_cb__())
        {
            env_factory__->free_obj_pool();
        }
        if (need_free_mem_cb__())
        {
            ctx_factory__->free_obj_pool();
        }
        if (need_free_mem_cb__())
        {
            stack_factory__->free_obj_pool();
        }
        if (need_free_mem_cb__())
        {
            stack_factory__->free_stack_mem_pool();
        }
    }
}

co_manager::co_manager()
{
    sub_manager_event__();
    create_background_task__();

    setup_switch_handler();
}

void co_manager::remove_env__(co_env* env)
{
    std::scoped_lock lock(mu_env_list__, mu_clean_up__);
    env_list__.remove(env);
    std::lock_guard<std::recursive_mutex> lck(mu_clean_up__);
    // CO_O_DEBUG("push to clean up: %p", env);
    expired_env__.push_back(env);
    cond_expired_env__.notify_one();

    env_removed().pub(env);
}

void co_manager::create_env_from_this_thread__()
{
    std::lock_guard<std::recursive_mutex> lck(mu_env_list__);
    current_env__ = env_factory__->create_env_from_this_thread(default_shared_stack_size__);

    sub_env_event__(current_env__);

    std::lock_guard<std::recursive_mutex> lock(mu_env_list__);
    env_list__.push_back(current_env__);
    ++exist_env_count__;
    // CO_O_DEBUG("create env from this thread : %p", current_env__);

    env_from_this_thread_created().pub(current_env__);
}

co_env* co_manager::current_env()
{
    if (current_env__ == nullptr)
    {
        create_env_from_this_thread__();
    }
    return current_env__;
}

void co_manager::set_clean_up__()
{
    std::scoped_lock lock(mu_clean_up__, mu_env_list__);
    // CO_O_DEBUG("set clean up!!!");
    clean_up__         = true;
    auto env_list_back = env_list__; // 在下面的清理操作中需要删除list中的元素导致迭代器失效，此处创建一个副本（也可以直接加入过期列表，然后清空env_list__，但是这样表达力会好些）
    for (auto& env : env_list_back)
    {
        if (env->test_flag(CO_ENV_FLAG_NO_SCHE_THREAD))
        {
            // 对于没有调度线程的env，无法将自己加入销毁队列，需要由manager__加入
            remove_env__(env);
            // CO_O_DEBUG("push to clean up: %p", env);
            continue;
        }
        // CO_O_DEBUG("call stop_schedule on %p", env);
        env->stop_schedule(); // 注意：没有调度线程的env不能调用stop_schedule
    }
    cond_expired_env__.notify_one();

    clean_up_set().pub();
}

void co_manager::clean_env_routine__()
{
    std::unique_lock<std::recursive_mutex> lck(mu_clean_up__);
    while (!clean_up__ || exist_env_count__ != 0)
    {
        // CO_O_DEBUG("wait to wake up ...");
        cond_expired_env__.wait(lck);
        // CO_O_DEBUG("wake up clean, exist_env_count: %d, expire count: %lu", (int)exist_env_count__, expired_env__.size());
        for (auto& p : expired_env__)
        {
            // CO_O_DEBUG("clean up an env: %p", p);
            env_factory__->destroy_env(p);
            --exist_env_count__;
        }
        expired_env__.clear();
    }
    // CO_O_DEBUG("clean up env finished\n");

    env_routine_cleaned().pub();
}

void co_manager::set_base_schedule_thread_count(size_t base_thread_count)
{
    if (base_thread_count == 0)
    {
        base_thread_count = 1;
    }
    base_thread_count__ = base_thread_count;
    base_thread_count_set().pub(base_thread_count__);
}

void co_manager::set_max_schedule_thread_count(size_t max_thread_count)
{
    if (max_thread_count == 0)
    {
        max_thread_count = 1;
    }
    max_thread_count__ = max_thread_count;
    max_thread_count_set().pub(max_thread_count__);
}

void co_manager::redistribute_ctx__()
{
    // 此处也需要锁定mu_env_list__，上层锁定
    std::lock_guard<std::recursive_mutex> lock(mu_clean_up__);
    if (clean_up__)
    {
        return;
    }

    std::list<co_ctx*> moved_ctx_list; // 需要被移动的ctx

    auto merge_list = [](std::list<co_ctx*>& target, const std::list<co_ctx*>& src) {
        target.insert(target.end(), src.begin(), src.end());
    };

    for (auto& env : env_list__)
    {
        // 如果检测到某个env被阻塞了，先锁定对应env的调度，防止在操作的时候发生调度，然后收集可转移的ctx
        if (env->is_blocked())
        {
            // 设置阻塞状态，后续的add_ctx不会将ctx加入到此env

            // fixme: 设置阻塞状态
            // env->set_state(co_env_state::blocked);

            // CO_O_DEBUG("env %p is blocked, redistribute ctx", env);
            merge_list(moved_ctx_list, env->take_moveable_ctx()); // 将阻塞的env中可移动的ctx收集起来
        }
        env->reset_scheduled_flag();
    }
    // 重新选择合适的env进行调度
    for (auto& ctx : moved_ctx_list)
    {
        get_best_env__()->add_ctx(ctx);
    }

    ctx_redistributed().pub();
}

void co_manager::destroy_redundant_env__()
{
    std::lock_guard<std::recursive_mutex> lock(mu_env_list__);
    // 然后删除多余的处于idle状态的env
    size_t               can_schedule_env_count = 0;
    std::vector<co_env*> idle_env_list;
    idle_env_list.reserve(env_list__.size());
    for (auto& env : env_list__)
    {
        if (env->can_schedule_ctx())
        {
            ++can_schedule_env_count;
        }
        if (env->state() == co_env_state::idle && env->can_auto_destroy()) // 如果状态是空闲，并且可以可以被自动销毁线程选中
        {
            idle_env_list.push_back(env);
        }
    }
    // 超出max_thread_count__，需要销毁env
    if (can_schedule_env_count > max_thread_count__)
    {
        auto should_destroy_count = can_schedule_env_count - max_thread_count__;
        for (size_t i = 0; i < should_destroy_count && i < idle_env_list.size(); ++i)
        {
            idle_env_list[i]->stop_schedule();
        }
    }
    redundant_env_destroyed().pub();
}

void co_manager::timing_routine__()
{
    while (!clean_up__)
    {
        std::this_thread::sleep_for(timing_duration());

        timing_routine_timout().pub();
    }
    timing_routine_finished().pub();
}

void co_manager::set_timing_tick_duration(
    const std::chrono::high_resolution_clock::duration& duration)
{
    std::lock_guard<std::mutex> lock(mu_timing_duration__);
    if (duration < std::chrono::milliseconds(DEFAULT_TIMING_TICK_DURATION_IN_MS))
    {
        timing_duration__ = std::chrono::milliseconds(DEFAULT_TIMING_TICK_DURATION_IN_MS);
    }
    else
    {
        timing_duration__ = duration;
    }
    timing_duration_set().pub();
}

const std::chrono::high_resolution_clock::duration& co_manager::timing_duration() const
{
    std::lock_guard<std::mutex> lock(mu_timing_duration__);
    return timing_duration__;
}

co_manager::~co_manager()
{
    set_clean_up__();
    wait_background_task__(); // 此处所有的流程都已经结束了，可以清理一些单例的资源了
    destroy_all_factory__();
}

void co_manager::destroy_all_factory__()
{
    delete scheduler_factory__;
    co_stack_factory::destroy_instance();
    co_ctx_factory::destroy_instance();
    co_env_factory::destroy_instance();
    all_factory_destroyed().pub();
}

void co_manager::wait_background_task__()
{
    for (auto& task : background_task__)
    {
        task.wait();
    }
    background_task_finished().pub();
}

co_ctx* co_manager::create_and_schedule_ctx(const co_ctx_config& config, bool lock_destroy)
{
    auto ctx = ctx_factory__->create_ctx(config);
    sub_ctx_event__(ctx);
    if (lock_destroy)
    {
        ctx->lock_destroy();
    }
    auto bind_env = ctx->config().bind_env;
    if (bind_env != nullptr)
    {
        bind_env->add_ctx(ctx);
    }
    else
    {
        get_best_env__()->add_ctx(ctx);
    }

    ctx_created().pub(ctx);
    return ctx;
}

void co_manager::set_if_gc_callback(std::function<bool()> cb)
{
    need_free_mem_cb__ = cb;
}

CO_NAMESPACE_END
