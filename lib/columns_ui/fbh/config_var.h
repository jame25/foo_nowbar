#pragma once

#include "fcl.h"

namespace fbh {
template <typename ValueType, typename ImplType = cfg_int_t<ValueType>>
class ConfigItem {
public:
    using Type = ConfigItem<ValueType, ImplType>;

    void reset() { set(m_default_value); }

    void set(ValueType new_value)
    {
        ValueType old_value{};
        if constexpr (std::is_arithmetic_v<ValueType>) {
            old_value = m_value.get_value();
        }
        m_value = new_value;

        if (m_on_change)
            m_on_change(m_value, old_value);
    }

    ValueType get() const { return m_value; }

    ValueType get_default_value() const { return m_default_value; }

    operator ValueType() const { return m_value; }

    Type& operator=(const ValueType& newValue)
    {
        set(newValue);
        return *this;
    }

    ConfigItem(const GUID& guid, ValueType default_value,
        std::function<void(const ValueType& new_value, const ValueType& old_value)> on_change)
    requires std::integral<ValueType> || std::floating_point<ValueType>
        : m_value(guid, default_value)
        , m_default_value{default_value}
        , m_on_change{on_change}
    {
    }

    ConfigItem(
        const GUID& guid, ValueType default_value, std::function<void(const ValueType& new_value)> on_change = nullptr)
        : m_value(guid, default_value)
        , m_default_value{default_value}
        , m_on_change{[on_change](const ValueType& new_value, const ValueType& old_value) {
            if (on_change) {
                on_change(new_value);
            }
        }}
    {
    }

    virtual ~ConfigItem() {}

private:
    ImplType m_value;
    ValueType m_default_value;
    std::function<void(const ValueType&, const ValueType&)> m_on_change;
};

using ConfigUint32 = ConfigItem<uint32_t>;
using ConfigInt32 = ConfigItem<int32_t>;
using ConfigBool = ConfigItem<bool>;
using ConfigString = ConfigItem<const char*, cfg_string>;

template <typename TInteger>
class ConfigIntegerDpiAware : public cfg_var {
public:
    using ValueType = uih::IntegerAndDpi<TInteger>;
    using Type = ConfigIntegerDpiAware<TInteger>;

    // X and Y DPIs are always the same for 'Windows apps', according to MSDN.
    // https://msdn.microsoft.com/en-us/library/windows/desktop/dn312083%28v=vs.85%29.aspx
    // https://msdn.microsoft.com/en-us/library/windows/desktop/dn280510%28v=vs.85%29.aspx
    void set(TInteger value, uint32_t dpi = uih::get_system_dpi_cached().cx)
    {
        m_value.set(value, dpi);
        on_change();
    }
    void set(uih::IntegerAndDpi<TInteger> value)
    {
        m_value = value;
        on_change();
    }
    Type& operator=(TInteger value)
    {
        set(value);
        return *this;
    }
    Type& operator=(uih::IntegerAndDpi<TInteger> value)
    {
        set(value);
        return *this;
    }

    operator TInteger() const { return m_value.get_scaled_value(); }
    const ValueType& get_raw_value() const { return m_value; }

    TInteger get_scaled_value() const { return m_value.get_scaled_value(); }

    virtual void on_change() {}
    ConfigIntegerDpiAware(const GUID& guid, TInteger value) : cfg_var(guid), m_value(ValueType(value)) {}

protected:
    void get_data_raw(stream_writer* p_stream, abort_callback& p_abort) override
    {
        p_stream->write_lendian_t(m_value.value, p_abort);
        p_stream->write_lendian_t(m_value.dpi, p_abort);
    }
    void set_data_raw(stream_reader* p_stream, t_size p_sizehint, abort_callback& p_abort) override
    {
        p_stream->read_lendian_t(m_value.value, p_abort);
        // Allow migration from older config variables
        if (p_sizehint > sizeof(TInteger))
            p_stream->read_lendian_t(m_value.dpi, p_abort);
        else
            m_value.dpi
                = uih::get_system_dpi_cached()
                      .cx; // If migrating from an older config var, assume it was set using the current system DPI.
    }

private:
    uih::IntegerAndDpi<TInteger> m_value;
};

using ConfigUint32DpiAware = ConfigIntegerDpiAware<uint32_t>;
using ConfigInt32DpiAware = ConfigIntegerDpiAware<int32_t>;

namespace fcl {
/**
 * \brief Writes an integer held in a ConfigItem<> object to an FCL writer.
 *
 * \tparam t_int    Integral or floating-point type
 * \param writer    FCL writer
 * \param id        Item ID
 * \param item      Value to write
 */
template <typename t_int>
void fcl_write_item(Writer& writer, unsigned id, const ConfigItem<t_int>& item)
requires std::integral<t_int> || std::floating_point<t_int>
{
    writer.write_item(id, static_cast<t_int>(item));
}

/**
 * \brief Reads an integer from an FCL reader to a ConfigItem<> object.
 *
 * \tparam t_int    Integral or floating-point type
 * \param reader    FCL reader
 * \param item      Output object
 */
template <typename t_int>
void fcl_read_item(Reader& reader, ConfigItem<t_int>& item)
requires std::integral<t_int> || std::floating_point<t_int>
{
    item = reader.read_raw_item<t_int>();
}

/**
 * \brief Writes an integer and DPI held in a ConfigIntegerDpiAware<> object to an FCL writer.
 *
 * \tparam t_int    Integral or floating-point type
 * \param writer    FCL writer
 * \param id        Item ID
 * \param item      Value to write
 */
template <typename t_int>
void fcl_write_item(Writer& writer, unsigned id, const ConfigIntegerDpiAware<t_int>& item)
{
    const auto raw_value = item.get_raw_value();
    static_assert(sizeof(raw_value.dpi) == sizeof(uint32_t));
    writer.write_raw(id);
    constexpr uint32_t size{sizeof(t_int) + sizeof(uint32_t)};
    writer.write_raw(size);
    writer.write_raw(raw_value.value);
    writer.write_raw(raw_value.dpi);
}

/**
 * \brief Reads an integer and DPI from an FCL reader to a ConfigIntegerDpiAware<> object.
 *
 * \tparam t_int    Integral or floating-point type
 * \param reader    FCL reader
 * \param item      Output object
 */
template <typename t_int>
void fcl_read_item(Reader& reader, ConfigIntegerDpiAware<t_int>& item)
{
    const auto value = reader.read_raw_item<t_int>();
    const auto dpi = reader.read_raw_item<uint32_t>();
    item.set(value, dpi);
}
} // namespace fcl
} // namespace fbh
