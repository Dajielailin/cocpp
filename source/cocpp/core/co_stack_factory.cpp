#include "cocpp/core/co_stack_factory.h"
#include "cocpp/core/co_define.h"
#include "cocpp/core/co_stack.h"
#include "cocpp/core/co_type.h"

CO_NAMESPACE_BEGIN

co_stack* co_stack_factory::create_stack(size_t size)
{
    co_byte* mem = nullptr;
    if (size != 0)
    {
        mem = mem_pool__.alloc_mem(size);
    }
    auto ret = stack_pool__.create_obj(mem, size);
    // CO_O_DEBUG("create stack %p", ret);
    return ret;
}

void co_stack_factory::destroy_stack(co_stack* stack)
{
    // CO_O_DEBUG("destory stack %p", stack);
    if (stack->stack_size() != 0)
    {
        mem_pool__.free_mem(stack->stack(), stack->stack_size());
    }
    stack_pool__.destroy_obj(stack);
}

void co_stack_factory::free_stack_mem_pool()
{
    mem_pool__.free_pool();
}

void co_stack_factory::free_obj_pool()
{
    stack_pool__.clear_free_object();
}

CO_NAMESPACE_END