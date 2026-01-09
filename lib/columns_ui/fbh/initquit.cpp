#include "stdafx.h"

namespace fbh {
initquit_factory_t<InitQuitManager> g_initquit_multiplexer;

void InitQuitManager::on_init() noexcept
{
    for (t_size i = 0, count = m_instances.get_count(); i < count; i++)
        m_instances[i]->on_init();
}
void InitQuitManager::on_quit() noexcept
{
    for (t_size i = 0, count = m_instances.get_count(); i < count; i++)
        m_instances[i]->on_quit();
}

void InitQuitManager::s_register_instance(InitQuitDynamic* ptr)
{
    g_initquit_multiplexer.get_static_instance().register_instance(ptr);
}
void InitQuitManager::s_deregister_instance(InitQuitDynamic* ptr)
{
    g_initquit_multiplexer.get_static_instance().deregister_instance(ptr);
}

} // namespace fbh
