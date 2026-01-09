#pragma once

namespace uih {

class UniscribeTextRenderer {
public:
    UniscribeTextRenderer() { initialise(); }

    ~UniscribeTextRenderer() { cleanup(); }

    UniscribeTextRenderer(HDC dc, const wchar_t* p_str, int p_str_len, int max_cx, bool b_clip,
        bool enable_tabs = false, int tab_origin = 0)
    {
        initialise();
        analyse(dc, p_str, p_str_len, max_cx, b_clip, enable_tabs, tab_origin);
    }

    void analyse(HDC dc, const wchar_t* p_str, int p_str_len, int max_cx, bool b_clip, bool enable_tabs = false,
        int tab_origin = 0)
    {
        if (m_ssa) {
            cleanup();
            initialise();
        }

        m_string_length = p_str_len;

        if (!m_string_length)
            return;

        SCRIPT_TABDEF tab_def{0, 4, nullptr, tab_origin};

        ScriptStringAnalyse(dc, p_str, p_str_len, NULL, -1,
            SSA_FALLBACK | SSA_GLYPHS | SSA_LINK | (b_clip ? SSA_CLIP : NULL) | (enable_tabs ? SSA_TAB : NULL), max_cx,
            &m_sc, &m_ss, nullptr, enable_tabs ? &tab_def : nullptr, nullptr, &m_ssa);
    }

    void get_output_size(SIZE& p_sz)
    {
        const SIZE* sz = m_ssa ? ScriptString_pSize(m_ssa) : nullptr;
        if (sz)
            p_sz = *sz;
        else {
            p_sz.cx = (p_sz.cy = 0);
        }
    }

    int get_output_width()
    {
        SIZE sz;
        get_output_size(sz);
        return sz.cx;
    }

private:
    void initialise()
    {
        m_scache = nullptr;
        m_ssa = nullptr;
        m_string_length = NULL;
        memset(&m_sc, 0, sizeof(m_sc));
        memset(&m_ss, 0, sizeof(m_ss));

        if (!m_sdg_valid) {
            ScriptRecordDigitSubstitution(LOCALE_USER_DEFAULT, &m_sdg);
            m_sdg_valid = true;
        }
        ScriptApplyDigitSubstitution(&m_sdg, &m_sc, &m_ss);
    }

    void cleanup()
    {
        if (m_ssa) {
            ScriptStringFree(&m_ssa);
            m_ssa = nullptr;
        }
        if (m_scache) {
            ScriptFreeCache(&m_scache);
            m_scache = nullptr;
        }
    }

    SCRIPT_STRING_ANALYSIS m_ssa;
    SCRIPT_CONTROL m_sc;
    SCRIPT_STATE m_ss;
    SCRIPT_CACHE m_scache;
    size_t m_string_length;

    inline static SCRIPT_DIGITSUBSTITUTE m_sdg{};
    inline static bool m_sdg_valid{};
};

} // namespace uih
