#pragma once

namespace mmh {

template <class T>
class ComPtr {
public:
    ComPtr(IUnknown* pUnk) : m_Unk(nullptr) { copy(pUnk); }

    ComPtr() : m_Unk(nullptr) {}

    ComPtr(const ComPtr<T>& p_source) : m_Unk(nullptr) { copy(p_source); }

    template <typename Q>
    ComPtr(const ComPtr<Q>& p_source) : m_Unk(nullptr)
    {
        copy(p_source);
    }

    ComPtr(ComPtr<T>&& p_source) noexcept : m_Unk(nullptr)
    {
        m_Unk = p_source.m_Unk;
        p_source.m_Unk = nullptr;
    }

    ~ComPtr() { release(); }

    void release()
    {
        if (m_Unk) {
            m_Unk->Release();
            m_Unk = nullptr;
        }
    }

    void copy(IUnknown* p_Unk)
    {
        release();
        if (p_Unk) {
            p_Unk->QueryInterface(&m_Unk);
        }
    }

    void attach(T* p_Unk) { m_Unk = p_Unk; }

    template <class Q>
    void copy(const ComPtr<Q>& p_source)
    {
        copy(p_source.get_ptr());
    }

    ComPtr<T>& operator=(const ComPtr<T>& p_Unk)
    {
        copy(p_Unk);
        return *this;
    }

    ComPtr<T>& operator=(ComPtr<T>&& p_Other) noexcept
    {
        m_Unk = p_Other.m_Unk;
        p_Other.m_Unk = nullptr;
        return *this;
    }

    template <class Q>
    ComPtr<T>& operator=(const ComPtr<Q>& p_Unk)
    {
        copy(p_Unk);
        return *this;
    }

    ComPtr<T>& operator=(IUnknown* p_Unk)
    {
        copy(p_Unk);
        return *this;
    }

    T* operator->() const
    {
        assert(m_Unk);
        return m_Unk;
    }

    operator void**()
    {
        release();
        return (void**)&m_Unk;
    }

    operator T**()
    {
        release();
        return &m_Unk;
    }

    T** get_pp()
    {
        release();
        return &m_Unk;
    }

    operator T*()
    {
        // assert(m_Unk);  //sometimes you may want to pass a NULL pointer as a function parameter
        return m_Unk;
    }

    T* get_ptr() const { return m_Unk; }
    bool is_valid() const { return m_Unk != nullptr; }
    bool is_empty() const { return m_Unk == nullptr; }

    bool operator==(const ComPtr<T>& p_item) const { return m_Unk == p_item.m_Unk; }
    bool operator!=(const ComPtr<T>& p_item) const { return m_Unk != p_item.m_Unk; }
    bool operator>(const ComPtr<T>& p_item) const { return m_Unk > p_item.m_Unk; }
    bool operator<(const ComPtr<T>& p_item) const { return m_Unk < p_item.m_Unk; }

    static void g_swap(ComPtr<T>& item1, ComPtr<T>& item2) { pfc::swap_t(item1.m_Unk, item2.m_Unk); }

    HRESULT instantiate(REFCLSID rclsid, LPUNKNOWN pUnkOuter = nullptr, DWORD dwClsContext = CLSCTX_ALL) throw()
    {
        return CoCreateInstance(rclsid, pUnkOuter, dwClsContext, __uuidof(T), *this);
    }

private:
    T* m_Unk;
};

} // namespace mmh
