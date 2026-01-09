#include "stdafx.h"

using namespace mmh::literals::pcc;

namespace fbh {

void enable_wil_console_logging()
{
    static bool is_logging_configured{};

    if (is_logging_configured)
        return;

    wil::SetResultLoggingCallback([](const wil::FailureInfo& failure) noexcept {
        std::array<wchar_t, 2048> log_message{};

#ifdef NDEBUG
        if (failure.type != wil::FailureType::Log)
            return;
#endif

        if (FAILED(wil::GetFailureLogString(log_message.data(), log_message.size(), failure)))
            return;

        if (core_api::are_services_available())
            console::print(log_message.data());
        else
            OutputDebugString(log_message.data());
    });

    is_logging_configured = true;
}

} // namespace fbh
