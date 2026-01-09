#pragma once

namespace fbh {
class LibraryCallback {
public:
    //! Called when new items are added to the Media Library.
    virtual void on_items_added(const pfc::list_base_const_t<metadb_handle_ptr>& p_data) = 0;
    //! Called when some items have been removed from the Media Library.
    virtual void on_items_removed(const pfc::list_base_const_t<metadb_handle_ptr>& p_data) = 0;
    //! Called when some items in the Media Library have been modified.
    virtual void on_items_modified(const pfc::list_base_const_t<metadb_handle_ptr>& p_data) = 0;
};

class LibraryCallbackAutoreg : public LibraryCallback {
public:
    LibraryCallbackAutoreg();
    ~LibraryCallbackAutoreg();
};

namespace library_callback_manager {
void register_callback(LibraryCallback* p_callback);
void deregister_callback(LibraryCallback* p_callback);
}; // namespace library_callback_manager
} // namespace fbh
