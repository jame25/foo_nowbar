#include "stdafx.h"

using namespace std::string_view_literals;

namespace uih {

std::optional<COLORREF> parse_colour_code(std::wstring_view text, bool selected)
{
    const auto bar_index = text.find(L'|');
    const auto non_selected_colour_hex = text.substr(0, bar_index);

    if (non_selected_colour_hex.empty())
        return {};

    COLORREF non_selected_colour
        = mmh::strtoul_n(non_selected_colour_hex.data(), std::min(size_t{6}, non_selected_colour_hex.length()), 0x10);

    if (!selected)
        return non_selected_colour;

    if (bar_index == std::wstring_view::npos)
        return 0xffffff - non_selected_colour;

    const auto selected_colour_hex = text.substr(bar_index + 1);
    return mmh::strtoul_n(selected_colour_hex.data(), std::min(size_t{6}, selected_colour_hex.length()), 0x10);
}

std::tuple<std::wstring, std::vector<ColouredTextSegment>> process_colour_codes(std::wstring_view text, bool selected)
{
    auto result = std::tuple<std::wstring, std::vector<ColouredTextSegment>>{};
    auto& [stripped_text, coloured_segments] = result;

    size_t offset{};
    std::optional<COLORREF> cr_current;

    while (true) {
        const size_t index = text.find(L'\3', offset);

        const auto fragment_length = index == std::wstring_view::npos ? std::wstring_view::npos : index - offset;
        const auto fragment = text.substr(offset, fragment_length);

        if (!fragment.empty()) {
            if (cr_current)
                coloured_segments.emplace_back(*cr_current, stripped_text.size(), fragment.length());

            stripped_text.append(fragment);
        }

        if (index == std::wstring_view::npos)
            break;

        offset = text.find(L'\3', index + 1);

        if (offset == std::wstring_view::npos)
            break;

        const auto colour_code = text.substr(index + 1, offset - index - 1);
        cr_current = parse_colour_code(colour_code, selected);

        ++offset;
    }

    return result;
}

std::string remove_colour_codes(std::string_view text)
{
    std::string stripped_text;
    size_t offset{};

    while (true) {
        const size_t index = text.find("\3"sv, offset);

        const auto fragment_length = index == std::string_view::npos ? std::string_view::npos : index - offset;
        const auto fragment = text.substr(offset, fragment_length);

        if (!fragment.empty())
            stripped_text.append(fragment);

        if (index == std::string_view::npos)
            break;

        offset = text.find("\3"sv, index + 1);

        if (offset == std::string_view::npos)
            break;

        ++offset;
    }

    return stripped_text;
}

} // namespace uih
