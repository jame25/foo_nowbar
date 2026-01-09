#pragma once

namespace mmh {

template <class Container>
auto get_container_size(Container&& container)
{
    if constexpr (requires() { container.size(); }) {
        return container.size();
    } else {
        return container.get_size();
    }
}

template <class Container>
auto get_container_data(Container&& container)
{
    if constexpr (requires() { container.data(); }) {
        return container.data();
    } else {
        return container.get_ptr();
    }
}

class StringAdaptor : public pfc::string_base {
public:
    StringAdaptor(std::string& data) : m_data(data) {}

    [[nodiscard]] const char* get_ptr() const override { return m_data.data(); }
    void add_string(const char* p_string, t_size p_length) override
    {
        m_data.append(p_string, strnlen(p_string, p_length));
    }
    void set_string(const char* p_string, t_size p_length) override
    {
        m_data = std::string_view(p_string, strnlen(p_string, p_length));
    }
    void truncate(t_size len) override { m_data.resize(len); }
    [[nodiscard]] t_size get_length() const override { return m_data.size(); }
    [[nodiscard]] char* lock_buffer(t_size p_requested_length) override
    {
        m_data.resize(p_requested_length);
        return m_data.data();
    }
    void unlock_buffer() override
    {
        const auto new_size = strnlen(m_data.data(), m_data.size());
        m_data.resize(new_size);
    }

private:
    std::string& m_data;
};

} // namespace mmh
