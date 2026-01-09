#pragma once

namespace fbh {
class InitQuitDynamic {
public:
    virtual void on_init() {}
    virtual void on_quit() {}
};

class InitQuitManager : public initquit {
public:
    void register_instance(InitQuitDynamic* ptr) { m_instances.add_item(ptr); }
    void deregister_instance(InitQuitDynamic* ptr) { m_instances.remove_item(ptr); }
    static void s_register_instance(InitQuitDynamic* ptr);
    static void s_deregister_instance(InitQuitDynamic* ptr);

private:
    void on_init() noexcept override;
    void on_quit() noexcept override;

    pfc::ptr_list_t<InitQuitDynamic> m_instances;
};
} // namespace fbh
