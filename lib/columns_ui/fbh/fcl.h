#pragma once

namespace fbh::fcl {
/**
 * \brief Helper class. Used to write data to FCL files.
 */
class Writer {
public:
    Writer(stream_writer* p_out, abort_callback& p_abort) : m_output(p_out), m_abort(p_abort) {}

    /**
     * \brief Writes a POD object (without item ID or size).
     * \tparam t_item    Object type
     * \param item        Object to write
     */
    template <typename t_item>
    void write_raw(const t_item& item)
    {
        pfc::assert_raw_type<t_item>();
        m_output->write_lendian_t(item, m_abort);
    }

    /**
     * \brief Writes an object, with ID and size values, using a matching fcl_write_item() free function.
     *
     * This allows a custom type to be made serialisable by defining an fcl_write_item() function with
     * an appropriate signature.
     *
     * \tparam t_item    Type of object to write
     * \param id        Item ID
     * \param item        Object to write
     */
    template <typename t_item,
        decltype(fcl_write_item(std::declval<Writer&>(), std::declval<unsigned>(), std::declval<t_item&>()))* = nullptr>
    void write_item(unsigned id, const t_item& item)
    {
        fcl_write_item(*this, id, item);
    }

    /**
     * \brief Writes an integer or float.
     *
     * \tparam t_item    Type of object to write
     * \param id        Item ID
     * \param item        Integer or float to write
     */
    template <typename t_item, std::enable_if_t<std::is_arithmetic_v<t_item>>* = nullptr>
    void write_item(unsigned id, const t_item& item)
    {
        write_raw(id);
        write_raw(gsl::narrow<uint32_t>(sizeof(t_item)));
        write_raw(item);
    }

    /**
     * \brief Writes a sequence of bytes.
     *
     * \param id        Item ID
     * \param data        Data to write
     * \param size        Number of bytes to write
     */
    void write_item(unsigned id, const void* data, uint32_t size)
    {
        write_raw(id);
        write_raw(size);
        m_output->write(data, size, m_abort);
    }

    /**
     * \brief Writes a UTF-8 string.
     *
     * \param id    Item ID
     * \param item    Null-terminated string to write
     */
    void write_item(unsigned id, const char* item);

    /**
     * \brief Writes a GUID
     *
     * \param id    Item ID
     * \param item    GUID to write
     */
    void write_item(unsigned id, const GUID& item)
    {
        write_raw(id);
        constexpr uint32_t size{sizeof(GUID)};
        write_raw(size);
        write_raw(item);
    }

    /**
     * \brief Writes a set of font attributes.
     *
     * \param id        Item ID
     * \param lfc    LOGFONT structure to write
     */
    void write_item(unsigned id, const LOGFONT& lfc);

private:
    stream_writer* m_output;
    abort_callback& m_abort;
};

/**
 * \brief Helper class. Used to read data from FCL files.
 */
class Reader {
public:
    /**
     * \brief Reads an item.
     *
     * \tparam t_item    Item type
     * \return           Read item
     */
    template <typename t_item>
    t_item read_item()
    {
        t_item item{};
        read_item<t_item>(item);
        return item;
    }

    /**
     * \brief Reads an integer or float.
     *
     * \tparam t_item    Integral or floating-point type.
     * \param p_out        Object that receives the read value.
     */
    template <typename t_item, std::enable_if_t<std::is_arithmetic_v<t_item>>* = nullptr>
    void read_item(t_item& p_out)
    {
        p_out = read_raw_item<t_item>();
    }

    /**
     * \brief Reads a sequence of bytes.
     *
     * \param p_out        Output buffer
     * \param size        Number of bytes to read
     */
    void read(void* p_out, t_size size)
    {
        auto pread = m_input->read(p_out, size, m_abort);
        m_position += pread;
        if (pread != size)
            throw exception_io_data_truncation();
    }

    /**
     * \brief Reads a custom type using a matching fcl_read_item() free function.
     *
     * This allows a custom type to be made deserialisable by defining an fcl_read_item() function with
     * an appropriate signature.
     *
     * \tparam t_item    Type of object to read
     * \param p_out        Output object
     */
    template <typename t_item, decltype(fcl_read_item(std::declval<Reader&>(), std::declval<t_item&>()))* = nullptr>
    void read_item(t_item& p_out)
    {
        fcl_read_item(*this, p_out);
    }

    /**
     * \brief Reads a GUID.
     *
     * \param p_out        Output object
     */
    void read_item(GUID& p_out) { p_out = read_raw_item<GUID>(); }

    /**
     * \brief Reads a UTF-8 string.
     *
     * \param p_out        Output object
     * \param size        Number of bytes to read
     */
    void read_item(pfc::string8& p_out, t_size size);

    /**
     * \brief Reads an item using a temporary variable.
     *
     * \tparam t_int_type    The type of the object to read
     * \return                The read value
     */
    template <typename t_int_type>
    t_int_type read_raw_item()
    {
        pfc::assert_raw_type<t_int_type>();
        t_int_type temp;
        m_input->read_lendian_t(temp, m_abort);
        m_position += sizeof(t_int_type);
        return temp;
    }

    /**
     * \brief Reads a set of font attributes.
     *
     * \param lf_out        Output object
     */
    void read_item(LOGFONT& lf_out);

    /**
     * \brief Gets the number of remaining bytes in the stream.
     *
     * \return                Number of remaining bytes
     */
    t_size get_remaining() const { return m_position > m_size ? 0 : m_size - m_position; }

    /**
     * \brief Skips a specified number of bytes.
     *
     * \param delta            Number of bytes to skip
     */
    void skip(t_size delta)
    {
        auto read = static_cast<size_t>(m_input->skip(delta, m_abort));
        m_position += read;
        if (read != delta)
            throw exception_io_data_truncation();
    }

    /**
     * \brief Initialises the reader from a stream_reader.
     *
     * \param p_input        Source reader
     * \param size            Number of bytes in the stream
     * \param p_abort        Abort callback for all operatioins
     */
    Reader(stream_reader* p_input, t_size size, abort_callback& p_abort)
        : m_size(size)
        , m_position(0)
        , m_input(p_input)
        , m_abort(p_abort)
    {
    }

    /**
     * \brief Initialises the reader from another reader.
     *
     * \param p_reader        Source reader
     * \param size            Number of bytes in the stream
     * \param p_abort        Abort callback for all operatioins
     */
    Reader(Reader& p_reader, t_size size, abort_callback& p_abort)
        : m_size(size)
        , m_position(0)
        , m_input(p_reader.m_input)
        , m_abort(p_abort)
    {
        p_reader.m_position += size;
    }

private:
    t_size m_size, m_position;
    stream_reader* m_input;
    abort_callback& m_abort;
};

/**
 * \brief Writes an integer held in a cfg_int_t<> object to an FCL writer.
 *
 * \tparam t_int    Integral or floating-point type
 * \param writer    FCL writer
 * \param id        Item ID
 * \param item        Value to write
 */
template <typename t_int>
void fcl_write_item(Writer& writer, unsigned id, const cfg_int_t<t_int>& item)
{
    writer.write_item(id, static_cast<t_int>(item));
}

/**
 * \brief Reads an integer from an FCL reader to a cfg_int_t<> object.
 *
 * \tparam t_int    Integral or floating-point type
 * \param reader    FCL reader
 * \param item        Output object
 */
template <typename t_int>
void fcl_read_item(Reader& reader, cfg_int_t<t_int>& item)
{
    item = reader.read_raw_item<t_int>();
}

/**
 * \brief Reads a set of font attributes to a cfg_struct_t<LOGFONT> object.
 *
 * \param reader    FCL reader
 * \param item        Output object
 */
void fcl_read_item(Reader& reader, cfg_struct_t<LOGFONT>& item);
} // namespace fbh::fcl
