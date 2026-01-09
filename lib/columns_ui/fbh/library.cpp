#include "stdafx.h"

namespace fbh {
class LibraryCallbackManager : public library_callback {
public:
    void register_callback(LibraryCallback* p_callback);
    void deregister_callback(LibraryCallback* p_callback);

private:
    void on_items_added(const pfc::list_base_const_t<metadb_handle_ptr>& p_data) noexcept override;
    void on_items_removed(const pfc::list_base_const_t<metadb_handle_ptr>& p_data) noexcept override;
    void on_items_modified(const pfc::list_base_const_t<metadb_handle_ptr>& p_data) noexcept override;

    pfc::ptr_list_t<LibraryCallback> m_callbacks;
};

void LibraryCallbackManager::register_callback(LibraryCallback* p_callback)
{
    m_callbacks.add_item(p_callback);
}

void LibraryCallbackManager::deregister_callback(LibraryCallback* p_callback)
{
    m_callbacks.remove_item(p_callback);
}

void LibraryCallbackManager::on_items_added(const pfc::list_base_const_t<metadb_handle_ptr>& p_data) noexcept
{
    t_size i, count = m_callbacks.get_count();
    for (i = 0; i < count; i++)
        m_callbacks[i]->on_items_added(p_data);
}

void LibraryCallbackManager::on_items_removed(const pfc::list_base_const_t<metadb_handle_ptr>& p_data) noexcept
{
    t_size i, count = m_callbacks.get_count();
    for (i = 0; i < count; i++)
        m_callbacks[i]->on_items_removed(p_data);
}

void LibraryCallbackManager::on_items_modified(const pfc::list_base_const_t<metadb_handle_ptr>& p_data) noexcept
{
    t_size i, count = m_callbacks.get_count();
    for (i = 0; i < count; i++)
        m_callbacks[i]->on_items_modified(p_data);
}

library_callback_factory_t<LibraryCallbackManager> g_library_callback_manager;

namespace library_callback_manager {
void register_callback(LibraryCallback* p_callback)
{
    g_library_callback_manager.get_static_instance().register_callback(p_callback);
}
void deregister_callback(LibraryCallback* p_callback)
{
    g_library_callback_manager.get_static_instance().deregister_callback(p_callback);
}
}; // namespace library_callback_manager

LibraryCallbackAutoreg::LibraryCallbackAutoreg()
{
    library_callback_manager::register_callback(this);
}

LibraryCallbackAutoreg::~LibraryCallbackAutoreg()
{
    library_callback_manager::deregister_callback(this);
}
} // namespace fbh
