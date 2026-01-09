#pragma once

namespace fbh {

constexpr auto max_size_t = std::numeric_limits<size_t>::max();

constexpr std::optional<size_t> as_optional(size_t index)
{
    if (index != max_size_t) {
        return index;
    }

    return {};
}

} // namespace fbh
