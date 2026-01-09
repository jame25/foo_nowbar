#pragma once

namespace mmh {

template <typename Size, typename Object>
constexpr Size sizeof_t()
{
    return gsl::narrow<Size>(sizeof(Object));
}

template <typename Size, typename Value>
constexpr Size sizeof_t(const Value& value)
{
    return gsl::narrow<Size>(sizeof value);
}

} // namespace mmh
