#pragma once

#define DDWM_UPDATEWINDOW (WM_USER + 3)

namespace uih::ole {
CLIPFORMAT get_clipboard_format_drop_description();
HRESULT set_blob(IDataObject* pdtobj, CLIPFORMAT cf, const void* pvBlob, UINT cbBlob);
HRESULT set_drop_description(IDataObject* pdtobj, DROPIMAGETYPE dit, const char* msg, const char* insert);
HRESULT set_using_default_drag_image(IDataObject* pdtobj, BOOL value = TRUE);
HRESULT set_is_showing_text(IDataObject* pdtobj, BOOL value = TRUE);
HRESULT set_disable_drag_text(IDataObject* pdtobj, BOOL value);
HRESULT set_is_computing_image(IDataObject* pdtobj, BOOL value = TRUE);
HRESULT do_drag_drop(HWND wnd, WPARAM initialKeyState, IDataObject* pDataObject, DWORD permittedEffects,
    DWORD preferredEffect, DWORD* pdwEffect, SHDRAGIMAGE* lpsdi = nullptr);

class IDropSourceGeneric : public IDropSource {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) noexcept override;
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD dwEffect) noexcept override;
    IDropSourceGeneric(HWND wnd, IDataObject* pDataObj, DWORD initial_key_state, bool b_allowdropdescriptiontext = true,
        SHDRAGIMAGE* lpsdi = nullptr); // careful, some fb2k versions have broken IDataObject
private:
    long m_refcount;
    DWORD m_initial_key_state;
    bool m_prev_is_showing_layered;
    wil::com_ptr<IDragSourceHelper> m_drag_source_helper;
    wil::com_ptr<IDataObject> m_data_object;
};

} // namespace uih::ole
