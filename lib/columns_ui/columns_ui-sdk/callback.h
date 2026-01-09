#pragma once

namespace cui {

class NOVTABLE callback_token : public service_base {
public:
    FB2K_MAKE_SERVICE_INTERFACE(callback_token, service_base)
};

class NOVTABLE lambda_callback_token : public callback_token {
public:
    lambda_callback_token(std::function<void()> release_func) : m_release_func(std::move(release_func)) {}

    ~lambda_callback_token() noexcept { m_release_func(); }

    std::function<void()> m_release_func;
};

class NOVTABLE basic_callback : public service_base {
public:
    virtual void operator()() = 0;

    FB2K_MAKE_SERVICE_INTERFACE(basic_callback, service_base)
};

class NOVTABLE lambda_basic_callback : public basic_callback {
public:
    lambda_basic_callback(std::function<void()> func) : m_func(std::move(func)) {}
    void operator()() override { m_func(); }

private:
    std::function<void()> m_func;
};

} // namespace cui
