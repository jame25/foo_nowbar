/**
 * This code is based on:
 * - The DragImg sample in the Windows SDK for Windows Server 2008 and .NET Framework 3.5
 * - https://msdn.microsoft.com/en-gb/library/ms997502.aspx [http://archive.is/uvdvf]
 *
 * The licence terms for the relevant Windows SDK are contained in windows-sdk-6.1-licence.html.
 */

#include "stdafx.h"

/**************************************************************************
THIS CODE AND INFORMATION IS PROVIDED 'AS IS' WITHOUT WARRANTY OF
ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
PARTICULAR PURPOSE.

Copyright 1999 - 2000 Microsoft Corporation.  All Rights Reserved.
**************************************************************************/

#include "data_object.h"
#include "enum_format_etc.h"
#include <shlobj.h>

extern USHORT g_cfFileContents;
extern USHORT g_cfFileGroupDescriptor;

IUnknown* GetCanonicalIUnknown(IUnknown* punk)
{
    IUnknown* punkCanonical;
    if (punk && SUCCEEDED(punk->QueryInterface(IID_IUnknown, reinterpret_cast<LPVOID*>(&punkCanonical)))) {
        punkCanonical->Release();
    } else {
        punkCanonical = punk;
    }
    return punkCanonical;
}

CDataObject::CDataObject()
{
    m_cRefCount = 0;
}

CDataObject::~CDataObject()
{
    size_t i;
    size_t count = m_data_entries.get_count();

    for (i = 0; i < count; i++)
        ReleaseStgMedium(&m_data_entries[i].sm);
}

HRESULT STDMETHODCALLTYPE CDataObject::QueryInterface(REFIID riid, LPVOID* ppvOut) noexcept
{
    *ppvOut = nullptr;

    // IUnknown
    if (IsEqualIID(riid, IID_IUnknown)) {
        *ppvOut = this;
    }

    // IDataObject
    else if (IsEqualIID(riid, IID_IDataObject)) {
        *ppvOut = static_cast<IDataObject*>(this);
    }

    if (*ppvOut) {
        (*reinterpret_cast<LPUNKNOWN*>(ppvOut))->AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CDataObject::AddRef() noexcept
{
    return ++m_cRefCount;
}

ULONG STDMETHODCALLTYPE CDataObject::Release() noexcept
{
    if (--m_cRefCount == 0) {
        delete this;
        return 0;
    }

    return m_cRefCount;
}

HRESULT CDataObject::_GetStgMediumAddRef(size_t index, STGMEDIUM* pstgmOut)
{
    HRESULT hres = S_OK;
    STGMEDIUM* pstgmIn = &m_data_entries[index].sm;
    STGMEDIUM stgmOut = *pstgmIn;

    if (pstgmIn->pUnkForRelease == nullptr && !(pstgmIn->tymed & (TYMED_ISTREAM | TYMED_ISTORAGE)))
        stgmOut.pUnkForRelease = static_cast<IDataObject*>(this);

    if (SUCCEEDED(hres)) {
        switch (stgmOut.tymed) {
        case TYMED_ISTREAM:
            stgmOut.pstm->AddRef();
            break;
        case TYMED_ISTORAGE:
            stgmOut.pstg->AddRef();
            break;
        }
        if (stgmOut.pUnkForRelease)
            stgmOut.pUnkForRelease->AddRef();

        *pstgmOut = stgmOut;
    }
    return hres;
}

HRESULT STDMETHODCALLTYPE CDataObject::GetData(LPFORMATETC pFE, LPSTGMEDIUM pSM) noexcept
{
    if (pFE == nullptr || pSM == nullptr)
        return E_INVALIDARG;

    ZeroMemory(pSM, sizeof(STGMEDIUM));

    size_t index;
    HRESULT hr = _FindFormatEtc(pFE, index, true);
    if (SUCCEEDED(hr)) {
        hr = _GetStgMediumAddRef(index, pSM);
        // hr = CopyStgMedium(&m_data_entries[index].sm, pSM);

        // if(pSM->tymed == TYMED_HGLOBAL)
        //{
        // this tell's the caller not to free the global memory
        // QueryInterface(IID_IUnknown, (LPVOID*)&pSM->pUnkForRelease);
        // }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE CDataObject::GetDataHere(LPFORMATETC pFE, LPSTGMEDIUM pSM) noexcept
{
    return E_NOTIMPL;
}

HRESULT CDataObject::_FindFormatEtc(LPFORMATETC pFE, size_t& index, bool b_checkTymed)
{
    if (!pFE)
        return DV_E_FORMATETC;

    if (pFE->ptd != nullptr)
        return DV_E_DVTARGETDEVICE;

    index = 0;

    if (m_data_entries.bsearch_t(t_data_entry::g_compare_formatetc_value, pFE, index)) {
        if (!b_checkTymed || (m_data_entries[index].fe.tymed & pFE->tymed)) // why & ?
        {
            return S_OK;
        }
        return DV_E_TYMED;
    }

    return DV_E_FORMATETC;
}

HRESULT STDMETHODCALLTYPE CDataObject::QueryGetData(LPFORMATETC pFE) noexcept
{
    size_t index;
    return _FindFormatEtc(pFE, index, true);
}

HRESULT STDMETHODCALLTYPE CDataObject::GetCanonicalFormatEtc(LPFORMATETC pFE1, LPFORMATETC pFE2) noexcept
{
    return DATA_S_SAMEFORMATETC;
}

HRESULT STDMETHODCALLTYPE CDataObject::SetData(LPFORMATETC pFE, LPSTGMEDIUM pSM, BOOL fRelease) noexcept
{
    if (pFE->ptd != nullptr)
        return DV_E_DVTARGETDEVICE;

    size_t index = 0;
    if (FAILED(_FindFormatEtc(pFE, index, false))) {
        // index = m_data_entries.get_count();
        m_data_entries.insert_item(t_data_entry(), index);
        // console::formatter() << "Added Data: " << (unsigned)pFE->tymed;
    } else
        ReleaseStgMedium(&m_data_entries[index].sm);

    m_data_entries[index].fe = *pFE;
    memset(&m_data_entries[index].sm, 0, sizeof(STGMEDIUM));

    if (fRelease) {
        m_data_entries[index].sm = *pSM;
    } else {
        CopyStgMedium(pSM, &m_data_entries[index].sm);
    }

    /* Subtlety!  Break circular reference loop */
    if (GetCanonicalIUnknown(m_data_entries[index].sm.pUnkForRelease)
        == GetCanonicalIUnknown(static_cast<IDataObject*>(this))) {
        m_data_entries[index].sm.pUnkForRelease->Release();
        m_data_entries[index].sm.pUnkForRelease = nullptr;
    }

    // for (size_t i = 0, count = m_data_entries.get_count(); i<count; i++)
    //     console::formatter() << i << ": " << (int)m_data_entries[i].fe.cfFormat << " " <<
    //     (int)m_data_entries[i].fe.dwAspect << " " << (int)m_data_entries[i].fe.lindex;
    // m_data_entries.sort_t(t_data_entry::g_compare_formatetc);

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDataObject::EnumFormatEtc(DWORD dwDir, LPENUMFORMATETC* ppEnum) noexcept
{
    // console::formatter() << "EnumFormatEtc";

    *ppEnum = nullptr;

    switch (dwDir) {
    case DATADIR_GET: {
        size_t count = m_data_entries.get_count();
        pfc::array_staticsize_t<FORMATETC> data(count);
        for (size_t i = 0; i < count; i++)
            data[i] = m_data_entries[i].fe;
        // Note: SHCreateStdEnumFmtEtc is deprecated, but still contained in current
        // versions of Windows
        return SHCreateStdEnumFmtEtc(gsl::narrow<UINT>(count), data.get_ptr(), ppEnum);
        //*ppEnum = new CEnumFormatEtc(data.get_ptr(), count);

        if (*ppEnum)
            return S_OK;

        break;
    }

    default:
        return OLE_S_USEREG;
    }

    return E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE CDataObject::DAdvise(
    LPFORMATETC pFE, DWORD advf, LPADVISESINK pAdvSink, LPDWORD pdwConnection) noexcept
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDataObject::DUnadvise(DWORD dwConnection) noexcept
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CDataObject::EnumDAdvise(LPENUMSTATDATA* ppenumAdvise) noexcept
{
    return OLE_E_ADVISENOTSUPPORTED;
}
