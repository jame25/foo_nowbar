#pragma once

namespace uih::emoji {

struct EmojiProperties {
    bool emoji{true};
    bool emoji_presentation{};
    bool has_variation{};
    bool component{};
};

extern const std::unordered_map<char32_t, EmojiProperties> emojis;

} // namespace uih::emoji
