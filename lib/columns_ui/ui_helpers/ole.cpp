#include "stdafx.h"

namespace uih::ole {

CLIPFORMAT get_clipboard_format_drop_description()
{
    static const auto cfRet = (CLIPFORMAT)RegisterClipboardFormat(CFSTR_DROPDESCRIPTION);
    return cfRet;
}

CLIPFORMAT DragWindowFormat()
{
    static const auto cfRet = (CLIPFORMAT)RegisterClipboardFormat(L"DragWindow");
    return cfRet;
}

CLIPFORMAT IsShowingLayeredFormat()
{
    static const auto cfRet = (CLIPFORMAT)RegisterClipboardFormat(L"IsShowingLayered");
    return cfRet;
}

CLIPFORMAT IsShowingTextFormat()
{
    static const auto cfRet = (CLIPFORMAT)RegisterClipboardFormat(L"IsShowingText");
    return cfRet;
}

CLIPFORMAT DisableDragTextFormat()
{
    static const auto cfRet = (CLIPFORMAT)RegisterClipboardFormat(L"DisableDragText");
    return cfRet;
}

CLIPFORMAT IsComputingImageFormat()
{
    static const auto cfRet = (CLIPFORMAT)RegisterClipboardFormat(L"IsComutingImage");
    return cfRet;
}

CLIPFORMAT UsingDefaultDragImageFormat()
{
    static const auto cfRet = (CLIPFORMAT)RegisterClipboardFormat(L"UsingDefaultDragImage");
    return cfRet;
}

CLIPFORMAT PreferredDropEffectFormat()
{
    static const auto cfRet = (CLIPFORMAT)RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);
    return cfRet;
}

template <typename T>
HRESULT GetDataObjectDataSimple(IDataObject* pDataObj, CLIPFORMAT cf, T& p_out)
{
    HRESULT hr;

    FORMATETC fe = {0};
    fe.cfFormat = cf;
    fe.dwAspect = DVASPECT_CONTENT;
    fe.lindex = -1;
    fe.tymed = TYMED_HGLOBAL;

    STGMEDIUM stgm = {0};
    if (SUCCEEDED(hr = pDataObj->GetData(&fe, &stgm))) {
        void* pData = GlobalLock(stgm.hGlobal);
        if (pData) {
            p_out = *static_cast<T*>(pData);
            GlobalUnlock(pData);
        }
        ReleaseStgMedium(&stgm);
    }
    return hr;
}

HRESULT GetDragWindow(IDataObject* pDataObj, HWND& p_wnd)
{
    HRESULT hr;
    DWORD dw;
    if (SUCCEEDED(hr = GetDataObjectDataSimple(pDataObj, DragWindowFormat(), dw)))
        p_wnd = (HWND)ULongToHandle(dw);

    return hr;
}

HRESULT GetIsShowingLayered(IDataObject* pDataObj, BOOL& p_out)
{
    return GetDataObjectDataSimple(pDataObj, IsShowingLayeredFormat(), p_out);
}

HRESULT set_blob(IDataObject* pdtobj, CLIPFORMAT cf, const void* pvBlob, UINT cbBlob)
{
    HRESULT hr = E_OUTOFMEMORY;
    void* pv = GlobalAlloc(GPTR, cbBlob);
    if (pv) {
        CopyMemory(pv, pvBlob, cbBlob);

        FORMATETC fmte = {cf, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};

        // The STGMEDIUM structure is used to define how to handle a global memory transfer.
        // This structure includes a flag, tymed, which indicates the medium
        // to be used, and a union comprising pointers and a handle for getting whichever
        // medium is specified in tymed.
        STGMEDIUM medium = {0};
        medium.tymed = TYMED_HGLOBAL;
        medium.hGlobal = pv;

        hr = pdtobj->SetData(&fmte, &medium, TRUE);
        if (FAILED(hr)) {
            GlobalFree(pv);
        }
    }
    return hr;
}

HRESULT set_drop_description(IDataObject* pdtobj, DROPIMAGETYPE dit, const char* msg, const char* insert)
{
    if (mmh::is_windows_vista_or_newer()) {
        DROPDESCRIPTION dd_prev;
        memset(&dd_prev, 0, sizeof(dd_prev));

        bool dd_prev_valid
            = (SUCCEEDED(GetDataObjectDataSimple(pdtobj, get_clipboard_format_drop_description(), dd_prev)));

        pfc::stringcvt::string_os_from_utf8 wmsg(msg);
        pfc::stringcvt::string_os_from_utf8 winsert(insert);

        // Only set the drop description if it has actually changed (otherwise things get a bit crazy near the edge of
        // the screen).
        if (!dd_prev_valid || dd_prev.type != dit || wcscmp(dd_prev.szInsert, winsert)
            || wcscmp(dd_prev.szMessage, wmsg)) {
            DROPDESCRIPTION dd;
            dd.type = dit;
            wcsncpy_s(dd.szMessage, wmsg.get_ptr(), _TRUNCATE);
            wcsncpy_s(dd.szInsert, winsert.get_ptr(), _TRUNCATE);
            return set_blob(pdtobj, get_clipboard_format_drop_description(), &dd, sizeof(dd));
        }
        return S_OK;
    }
    return E_NOTIMPL;
}

HRESULT set_using_default_drag_image(IDataObject* pdtobj, BOOL value)
{
    return set_blob(pdtobj, UsingDefaultDragImageFormat(), &value, sizeof(value));
}

HRESULT set_is_showing_text(IDataObject* pdtobj, BOOL value)
{
    return set_blob(pdtobj, IsShowingTextFormat(), &value, sizeof(value));
}

HRESULT set_disable_drag_text(IDataObject* pdtobj, BOOL value)
{
    return set_blob(pdtobj, DisableDragTextFormat(), &value, sizeof(value));
}

HRESULT set_is_computing_image(IDataObject* pdtobj, BOOL value)
{
    return set_blob(pdtobj, IsComputingImageFormat(), &value, sizeof(value));
}

HRESULT SetPreferredDropEffect(IDataObject* pdtobj, DWORD effect)
{
    return set_blob(pdtobj, PreferredDropEffectFormat(), &effect, sizeof(effect));
}

HRESULT do_drag_drop(HWND wnd, WPARAM initialKeyState, IDataObject* pDataObject, DWORD dwEffect, DWORD preferredEffect,
    DWORD* pdwEffect, SHDRAGIMAGE* lpsdi)
{
    if (preferredEffect)
        SetPreferredDropEffect(pDataObject, preferredEffect);

    const wil::com_ptr<IDropSource> pDropSource
        = new IDropSourceGeneric(wnd, pDataObject, LODWORD(initialKeyState), true, lpsdi);
    return SHDoDragDrop(wnd, pDataObject, pDropSource.get(), dwEffect, pdwEffect);
}

HRESULT STDMETHODCALLTYPE IDropSourceGeneric::QueryInterface(REFIID iid, void** ppvObject) noexcept
{
    if (ppvObject == nullptr)
        return E_INVALIDARG;

    *ppvObject = nullptr;

    if (iid == IID_IUnknown) {
        AddRef();
        *ppvObject = (IUnknown*)this;
        return S_OK;
    }

    if (iid == IID_IDropSource) {
        AddRef();
        *ppvObject = (IDropSource*)this;
        return S_OK;
    }
    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE IDropSourceGeneric::AddRef() noexcept
{
    return InterlockedIncrement(&m_refcount);
}
ULONG STDMETHODCALLTYPE IDropSourceGeneric::Release() noexcept
{
    LONG rv = InterlockedDecrement(&m_refcount);
    if (!rv) {
        delete this;
    }
    return rv;
}

HRESULT STDMETHODCALLTYPE IDropSourceGeneric::QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) noexcept
{
    if (fEscapePressed || ((m_initial_key_state & MK_LBUTTON) && (grfKeyState & MK_RBUTTON))) {
        return DRAGDROP_S_CANCEL;
    }

    if (((m_initial_key_state & MK_LBUTTON) && !(grfKeyState & MK_LBUTTON))
        || ((m_initial_key_state & MK_RBUTTON) && !(grfKeyState & MK_RBUTTON))) {
        return DRAGDROP_S_DROP;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE IDropSourceGeneric::GiveFeedback(DWORD dwEffect) noexcept
{
    HWND wnd_drag = nullptr;
    BOOL isShowingLayered = FALSE;
    if (IsThemeActive())
        GetIsShowingLayered(m_data_object.get(), isShowingLayered);

    if (SUCCEEDED(GetDragWindow(m_data_object.get(), wnd_drag)) && wnd_drag)
        PostMessage(wnd_drag, DDWM_UPDATEWINDOW, NULL, NULL);

    if (isShowingLayered) {
        if (!m_prev_is_showing_layered) {
            auto cursor = LoadCursor(nullptr, IDC_ARROW);
            SetCursor(cursor);
        }
        if (wnd_drag) {
            WPARAM wp = 1;
            if (dwEffect & DROPEFFECT_COPY)
                wp = 3;
            else if (dwEffect & DROPEFFECT_MOVE)
                wp = 2;
            else if (dwEffect & DROPEFFECT_LINK)
                wp = 4;

            PostMessage(wnd_drag, WM_USER + 2, wp, NULL);
        }
    }

    m_prev_is_showing_layered = isShowingLayered != 0;

    return isShowingLayered ? S_OK : DRAGDROP_S_USEDEFAULTCURSORS;
}

IDropSourceGeneric::IDropSourceGeneric(
    HWND wnd, IDataObject* pDataObj, DWORD initial_key_state, bool b_allowdropdescriptiontext, SHDRAGIMAGE* lpsdi)
    : m_refcount(0)
    , m_initial_key_state(initial_key_state)
    , m_prev_is_showing_layered(false)
    , m_data_object(pDataObj)
{
    HRESULT hr;

    if (b_allowdropdescriptiontext) {
        m_drag_source_helper = wil::CoCreateInstanceNoThrow<IDragSourceHelper>(CLSID_DragDropHelper);
        if (m_drag_source_helper) {
            const auto pDragSourceHelper2 = m_drag_source_helper.try_query<IDragSourceHelper2>();
            if (pDragSourceHelper2) {
                hr = pDragSourceHelper2->SetFlags(DSH_ALLOWDROPDESCRIPTIONTEXT);
            }
            if (lpsdi) {
                hr = m_drag_source_helper->InitializeFromBitmap(lpsdi, pDataObj);
                if (FAILED(hr) && lpsdi->hbmpDragImage) {
                    DeleteObject(lpsdi->hbmpDragImage);
                    lpsdi->hbmpDragImage = nullptr;
                }
            } else
                hr = m_drag_source_helper->InitializeFromWindow(wnd, nullptr, pDataObj);
            if (IsThemeActive() && IsAppThemed()) {
                set_using_default_drag_image(pDataObj);
            }
        }
    }
}
} // namespace uih::ole
