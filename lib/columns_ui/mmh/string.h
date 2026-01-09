#pragma once

namespace mmh {

namespace literals::pcc {

const char* operator"" _pcc(const char8_t* str, size_t);

} // namespace literals::pcc

/**
 * Note: GetNumberFormat is not used as it will format with decimal places (unless a custom
 * NUMBERFMT is passed, which means bypassing the user's own settings anyway).
 */
std::string format_integer(uint64_t value);

std::string format_file_size(uint64_t size);

const char* convert_utf16_to_ascii(const WCHAR* str_utf16, t_size len, pfc::string_base& p_out);
const char* convert_utf8_to_ascii(const char* p_source, pfc::string_base& p_out);

/**
 * \brief Partially compares two UTF-8 strings.
 *
 * Replacement for stricmp_utf8_partial in shared.dll (based on similar functions in pfc).
 *
 * If the substring is shorter than the main string, and the two strings are equal until that point,
 * they are considered equal for the purposes of this function. Otherwise, this functions like a
 * normal case-insensitive string comparison.
 *
 * \param str       The main string being checked against a substring
 * \param substr    The substring to compare the main string with
 * \return          0 if considered equal
 *                  -1 if str < substr
 *                  1 if str > substr
 */
int compare_string_partial_case_insensitive(const char* str, const char* substr);
char format_digit(unsigned p_val);
t_size power_of_ten(t_size raiseTo);

template <typename TChar, typename TInteger = uint32_t>
TInteger strtoul_n(const TChar* p_val, size_t p_val_length, unsigned base = 10)
{
    TInteger rv = 0;
    const TChar* ptr = p_val;

    while (t_size(ptr - p_val) < p_val_length && *ptr) {
        if (*ptr >= '0' && *ptr <= '9') {
            rv *= base;
            rv += *ptr - '0';
        } else if (base > 10 && *ptr >= 'a' && *ptr < TChar('a' + base - 10)) {
            rv *= base;
            rv += *ptr - 'a' + 10;
        } else if (base > 10 && *ptr >= 'A' && *ptr < TChar('A' + base - 10)) {
            rv *= base;
            rv += *ptr - 'A' + 10;
        } else
            break;
        ++ptr;
    }
    return rv;
}

template <typename TChar>
uint64_t strtoul64_n(const TChar* p_val, unsigned p_val_length, unsigned base = 10)
{
    return strtoul_n<TChar, uint64_t>(p_val, p_val_length, base);
}

template <typename TChar, typename TInteger = uint32_t>
TInteger strtol_n(const TChar* p_val, unsigned p_val_length, unsigned base = 10)
{
    TInteger rv = 0;
    const TChar* ptr = p_val;

    t_int8 sign = 1;

    if (*ptr == '-') {
        sign = -1;
        ptr++;
    } else if (*ptr == '+')
        ptr++;

    while (t_size(ptr - p_val) < p_val_length && *ptr) {
        if (*ptr >= '0' && *ptr <= '9') {
            rv *= base;
            rv += *ptr - '0';
        } else if (base > 10 && *ptr >= 'a' && *ptr < static_cast<TChar>('a' + base - 10)) {
            rv *= base;
            rv += *ptr - 'a' + 10;
        } else if (base > 10 && *ptr >= 'A' && *ptr < static_cast<TChar>('A' + base - 10)) {
            rv *= base;
            rv += *ptr - 'A' + 10;
        } else
            break;
        ++ptr;
    }
    rv *= sign;
    return rv;
}

template <typename TChar>
int64_t strtol64_n(const TChar* p_val, unsigned p_val_length, unsigned base = 10)
{
    return strtol_n<TChar, int64_t>(p_val, p_val_length, base);
}

template <typename String>
requires requires(String value) { std::string_view(value.get_ptr(), value.length()); }
std::string_view to_string_view(const String& value)
{
    return {value.get_ptr(), value.length()};
}

std::wstring to_utf16(std::string_view utf8_value);
std::string to_utf8(std::wstring_view utf16_value);

} // namespace mmh
