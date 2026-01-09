#include "ui_extension.h"

namespace cui::fonts {

std::optional<LOGFONT> get_log_font(GUID font_id)
{
    if (manager::ptr api; fb2k::std_api_try_get(api)) {
        LOGFONT log_font{};

        if (font_id == items_font_id)
            api->get_font(font_type_items, log_font);
        else if (font_id == labels_font_id)
            api->get_font(font_type_labels, log_font);
        else
            api->get_font(font_id, log_font);

        return log_font;
    }

    return {};
}

LOGFONT get_log_font_with_fallback(GUID font_id)
{
    if (const auto log_font = get_log_font(font_id))
        return *log_font;

    LOGFONT log_font{};

    if (font_id == labels_font_id)
        uGetMenuFont(&log_font);
    else
        uGetIconFont(&log_font);

    return log_font;
}

HFONT create_hfont_with_fallback(GUID font_id)
{
    const auto log_font = get_log_font_with_fallback(font_id);
    return CreateFontIndirect(&log_font);
}

#if CUI_SDK_DWRITE_ENABLED
font::ptr get_font(GUID font_id)
{
    if (manager_v3::ptr api; fb2k::std_api_try_get(api)) {
        return api->get_font(font_id);
    }

    return {};
}
#endif

namespace {

class lambda_common_callback : public common_callback {
public:
    explicit lambda_common_callback(std::function<void(uint32_t changed_items_mask)> callback_func)
        : m_callback_func(std::move(callback_func))
    {
    }

    lambda_common_callback(lambda_common_callback&&) = default;
    lambda_common_callback& operator=(lambda_common_callback&&) = default;

    void on_font_changed(uint32_t changed_items_mask) const override { m_callback_func(changed_items_mask); }

private:
    std::function<void(uint32_t changed_items_mask)> m_callback_func;
};

} // namespace

callback_token::ptr on_common_font_changed(std::function<void(uint32_t changed_items_mask)> callback_func)
{
    manager::ptr api;

    if (!fb2k::std_api_try_get(api))
        return {};

    auto callback = std::make_shared<lambda_common_callback>(std::move(callback_func));

    api->register_common_callback(&*callback);

    auto deregister = [callback{std::move(callback)}, api]() mutable { api->deregister_common_callback(&*callback); };

    return fb2k::service_new<lambda_callback_token>(std::move(deregister));
}

} // namespace cui::fonts
