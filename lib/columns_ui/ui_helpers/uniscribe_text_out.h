#pragma once

namespace uih {

enum alignment {
    ALIGN_LEFT,
    ALIGN_CENTRE,
    ALIGN_RIGHT,
};

bool is_rect_null_or_reversed(const RECT* r);
int get_text_width(HDC dc, const char* src, int len);
void remove_color_marks(const char* src, pfc::string_base& out, size_t len = pfc_infinite);

} // namespace uih
