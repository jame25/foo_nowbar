#pragma once

namespace uih {

struct ColouredTextSegment {
    COLORREF colour{};
    size_t start_character{};
    size_t character_count{};
};

std::optional<COLORREF> parse_colour_code(std::wstring_view text, bool selected);
std::tuple<std::wstring, std::vector<ColouredTextSegment>> process_colour_codes(std::wstring_view, bool selected);
std::string remove_colour_codes(std::string_view text);

} // namespace uih
