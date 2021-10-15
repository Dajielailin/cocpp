#pragma once
#include "co_define.h"
#include "co_nocopy.h"
#include "co_singleton.h"
#include <cstddef>

CO_NAMESPACE_BEGIN

class co_stack;
class co_manager;

class co_stack_factory final : public co_singleton<co_stack_factory>
{
public:
    co_stack* create_stack(size_t size);
    void      destroy_stack(co_stack* stack);
};

CO_NAMESPACE_END