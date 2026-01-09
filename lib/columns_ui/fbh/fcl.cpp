#include "stdafx.h"

namespace fbh::fcl {
void Writer::write_item(unsigned id, const char* item)
{
    m_output->write_lendian_t(id, m_abort);
    m_output->write_string(item, m_abort);
}

void Writer::write_item(unsigned id, const LOGFONT& lfc)
{
    LOGFONT lf = lfc;
    t_size face_len = pfc::wcslen_max(lf.lfFaceName, tabsize(lf.lfFaceName));

    if (face_len < tabsize(lf.lfFaceName)) {
        WCHAR* ptr = lf.lfFaceName;
        ptr += face_len;
        memset(ptr, 0, sizeof(WCHAR) * (tabsize(lf.lfFaceName) - face_len));
    }

    m_output->write_lendian_t(id, m_abort);
    m_output->write_lendian_t(mmh::sizeof_t<uint32_t>(lf), m_abort);

    m_output->write_lendian_t(lf.lfHeight, m_abort);
    m_output->write_lendian_t(lf.lfWidth, m_abort);
    m_output->write_lendian_t(lf.lfEscapement, m_abort);
    m_output->write_lendian_t(lf.lfOrientation, m_abort);
    m_output->write_lendian_t(lf.lfWeight, m_abort);

    // meh endianness
    m_output->write(&lf.lfItalic, 8 + sizeof(lf.lfFaceName), m_abort);
}

void Reader::read_item(pfc::string8& p_out, t_size size)
{
    pfc::array_t<char> temp;
    temp.set_size(size + 1);
    temp.fill(0);

    if (size) {
        const auto read = m_input->read(temp.get_ptr(), size, m_abort);
        if (read != size)
            throw exception_io_data_truncation();
    }
    p_out = temp.get_ptr();
    m_position += size;
}

void Reader::read_item(LOGFONT& lf_out)
{
    LOGFONT lf;
    memset(&lf, 0, sizeof(LOGFONT));

    read_item(lf.lfHeight);
    read_item(lf.lfWidth);
    read_item(lf.lfEscapement);
    read_item(lf.lfOrientation);
    read_item(lf.lfWeight);

    // meh endianness
    read(&lf.lfItalic, 8 + sizeof(lf.lfFaceName));

    lf_out = lf;
}

void fcl_read_item(Reader& reader, cfg_struct_t<LOGFONT>& item)
{
    reader.read_item(item.get_value());
}
} // namespace fbh::fcl
