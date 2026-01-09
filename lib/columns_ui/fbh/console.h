#pragma once

namespace fbh {

template <typename... Args>
void print_to_console(Args&&... args)
{
    console::formatter formatter;
    auto add_value = [&formatter](auto&& value) { formatter << value; };
    (add_value(std::forward<Args>(args)), ...);
}

} // namespace fbh
