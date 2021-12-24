_Pragma("once");
#include "co_ctx_config.h"
#include "co_object_pool.h"
#include "co_singleton.h"
#include "co_stack.h"

CO_NAMESPACE_BEGIN

class co_ctx_factory final : public co_singleton<co_ctx_factory>
{

private:
    co_object_pool<co_ctx> ctx_pool__ { MAX_CTX_CACHE_COUNT };                             // 协程对象池
public:                                                                                    //
    co_ctx* create_ctx(const co_ctx_config& config, std::function<void(std::any&)> entry); // 创建协程
    void    destroy_ctx(co_ctx* ctx);                                                      // 销毁协程
    void    free_obj_pool();                                                               // 释放对象池
};

CO_NAMESPACE_END