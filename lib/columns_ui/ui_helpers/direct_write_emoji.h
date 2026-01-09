#pragma once

namespace uih::direct_write::emoji_font_fallback {

wil::com_ptr<IDWriteFontFallback> create_emoji_font_fallback(const wil::com_ptr<IDWriteFontCollection>& font_collection,
    wil::com_ptr<IDWriteFontFallback> base_fallback, const wchar_t* emoji_family_name,
    const wchar_t* monochrome_family_name);

}
