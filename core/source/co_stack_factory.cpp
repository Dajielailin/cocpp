#include "co_stack_factory.h"
#include "co_define.h"
#include "co_stack.h"

CO_NAMESPACE_BEGIN

co_stack* co_stack_factory::create_stack(size_t size)
{
    auto ret = stack_pool__.create_obj(size);
    // CO_O_DEBUG("create stack %p", ret);
    return ret;
}

void co_stack_factory::destroy_stack(co_stack* stack)
{
    // CO_O_DEBUG("destory stack %p", stack);
    stack_pool__.destroy_obj(stack);
}

CO_NAMESPACE_END