#include "stdafx.h"

namespace mmh {
t_size power_of_ten(t_size raiseTo)
{
    t_size ret = 1, i;
    for (i = 0; i < raiseTo; i++)
        ret *= 10;
    return ret;
}

const char* literals::pcc::operator""_pcc(const char8_t* str, size_t)
{
    return reinterpret_cast<const char*>(str);
}

std::string format_integer(t_uint64 size)
{
    // Calculate number of digits safely.
    t_size digits = 0;
    t_uint64 size_temp = size;

    while (size_temp) {
        size_temp /= 10;
        digits++;
    }

    // Get thousands separator
    WCHAR wide_separator[4] = L",";
    GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, wide_separator, tabsize(wide_separator));
    pfc::stringcvt::string_utf8_from_wide separator(wide_separator, tabsize(wide_separator));
    t_size separator_len = strlen(separator.get_ptr());

    // Format the number
    if (!digits)
        ++digits;

    t_size separators = (digits - 1) / 3;

    std::string buffer;
    buffer.resize(digits + separators * separator_len);

    t_size pos = digits + separators * separator_len - 1;
    size_temp = size;

    for (t_size i = 0; i < digits; i++) {
        buffer[pos] = pfc::format_hex_char(size_temp % 10);
        size_temp /= 10;
        // In theory we should use LOCALE_SGROUPING...
        if (i && i + 1 < digits && (i + 1) % 3 == 0) {
            PFC_ASSERT(pos > separator_len);
            pos -= separator_len;
            memcpy(&buffer[pos], separator, separator_len);
        }
        pos--;
    }

    return buffer;
}

const char* convert_utf16_to_ascii(const WCHAR* str_utf16, t_size len, pfc::string_base& p_out)
{
    const char* replacement = "_";
    int len_max = gsl::narrow<int>(std::min(wcslen(str_utf16), len));
    if (len_max) {
        int size_ascii = WideCharToMultiByte(20'127, NULL, str_utf16, len_max, nullptr, NULL, replacement, nullptr);
        if (!size_ascii)
            throw exception_win32(GetLastError());

        pfc::array_t<char> str_ascii;
        str_ascii.set_size(size_ascii + 1);
        str_ascii.fill_null();
        int ret = WideCharToMultiByte(
            20'127, NULL, str_utf16, len_max, str_ascii.get_ptr(), size_ascii, replacement, nullptr);
        if (!ret)
            throw exception_win32(GetLastError());
        p_out.set_string(str_ascii.get_ptr(), size_ascii);
    } else
        p_out.reset();
    return p_out.get_ptr();
}

const char* convert_utf8_to_ascii(const char* p_source, pfc::string_base& p_out)
{
    pfc::stringcvt::string_wide_from_utf8 str_utf16(p_source);
    convert_utf16_to_ascii(str_utf16, pfc_infinite, p_out);
    return p_out.get_ptr();
}

char format_digit(unsigned p_val)
{
    PFC_ASSERT(p_val < 16);
    return (p_val < 10) ? p_val + '0' : p_val - 10 + 'A';
}

std::string format_file_size(uint64_t size)
{
    t_uint64 scale = 1024;
    const char* unit = "kB";
    const char* const unitTable[] = {"B", "kB", "MB", "GB", "TB"};
    for (t_size walk = 2; walk < std::size(unitTable); ++walk) {
        t_uint64 next = scale * 1024;
        if (size < next)
            break;
        scale = next;
        unit = unitTable[walk];
    }
    uint64_t major = (size / scale), minor = 0;
    uint32_t minor_digits = 0;

    bool b_minor = major <= 99 && size;

    uint64_t remainder_raw = size % scale;

    uint64_t remainder = (remainder_raw * 1000) / scale;
    constexpr size_t max_remainder_digits = 3;

    if (b_minor) {
        minor_digits = 1;
        if (major < 10)
            minor_digits++;

        minor = remainder / (power_of_ten(max_remainder_digits - minor_digits));

        if (max_remainder_digits > minor_digits
            && ((remainder % power_of_ten(max_remainder_digits - minor_digits))
                   / power_of_ten(max_remainder_digits - minor_digits - 1))
                >= 5) {
            if ((minor % 10 < 9) || (minor_digits == 2 && (((minor % 100) / 10) < 9)))
                minor++;
            else {
                major++;
                minor = 0;
            }
        }
    } else if ((remainder / power_of_ten(max_remainder_digits - 1)) >= 5)
        major++;

    std::string result = pfc::format_uint(major).c_str();

    if (b_minor) {
        WCHAR separator[4] = {'.', 0, 0, 0};
        GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, separator, tabsize(separator));

        result += to_utf8({separator, std::size(separator)});
        result += pfc::format_uint(minor, minor_digits);
    }

    result += " ";
    result += unit;
    return result;
}

int compare_string_partial_case_insensitive(const char* str, const char* substr)
{
    for (;;) {
        uint32_t char1{};
        uint32_t char2{};

        const auto dec_bytes_1 = pfc::utf8_decode_char(str, char1);
        const auto dec_bytes_2 = pfc::utf8_decode_char(substr, char2);

        char1 = pfc::charLower(char1);
        char2 = pfc::charLower(char2);

        if (char2 == 0)
            return 0;
        if (char1 < char2)
            return -1;
        if (char1 > char2)
            return 1;
        // Carry on, as char1 == char2, and char2 != 0

        str += dec_bytes_1;
        substr += dec_bytes_2;
    }
}

std::wstring to_utf16(std::string_view utf8_value)
{
    const auto utf16_estimated_size = pfc::stringcvt::estimate_utf8_to_wide(utf8_value.data(), utf8_value.size());
    std::wstring utf16_value;
    utf16_value.resize(utf16_estimated_size);

    const auto utf16_actual_size = pfc::stringcvt::convert_utf8_to_wide(
        utf16_value.data(), utf16_estimated_size, utf8_value.data(), utf8_value.size());
    utf16_value.resize(utf16_actual_size);

    return utf16_value;
}

std::string to_utf8(std::wstring_view utf16_value)
{
    const auto utf8_estimated_size = pfc::stringcvt::estimate_wide_to_utf8(utf16_value.data(), utf16_value.size());
    std::string utf8_value;
    utf8_value.resize(utf8_estimated_size);

    const auto utf8_actual_size = pfc::stringcvt::convert_wide_to_utf8(
        utf8_value.data(), utf8_estimated_size, utf16_value.data(), utf16_value.size());
    utf8_value.resize(utf8_actual_size);

    return utf8_value;
}

} // namespace mmh
