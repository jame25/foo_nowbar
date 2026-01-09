#pragma once

namespace mmh {

template <class Container, class Delimiter, class Value = typename std::decay_t<Container>::value_type>
Value join(Container&& container, Delimiter&& delimiter)
{
    Value joined_value;

    for (auto iter = container.begin(); iter != container.end(); ++iter) {
        if (std::distance(container.begin(), iter) != 0)
            joined_value += delimiter;
        joined_value += *iter;
    }

    return joined_value;
}

} // namespace mmh
