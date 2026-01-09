#include "stdafx.h"

/**
 * This code is based on:
 * - The DragImg sample in the Windows SDK for Windows Server 2008 and .NET Framework 3.5
 * - https://msdn.microsoft.com/en-gb/library/ms997502.aspx [http://archive.is/uvdvf]
 *
 * The licence terms for the relevant Windows SDK are contained in windows-sdk-6.1-licence.html.
 */

/**************************************************************************
   THIS CODE AND INFORMATION IS PROVIDED 'AS IS' WITHOUT WARRANTY OF
   ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
   THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
   PARTICULAR PURPOSE.

   Copyright 1999 - 2000 Microsoft Corporation.  All Rights Reserved.
**************************************************************************/

#include <windows.h>
#include <shlobj.h>
#include "enum_format_etc.h"

CEnumFormatEtc::CEnumFormatEtc(LPFORMATETC pFE, int nItems)
{
    m_cRefCount = 1;

    m_iCur = 0;

    m_pStrFE = new FORMATETC[nItems];

    if (m_pStrFE) {
        CopyMemory((LPBYTE)m_pStrFE, (LPBYTE)pFE, sizeof(FORMATETC) * nItems);
        m_cItems = nItems;
    } else {
        m_cItems = 0;
    }
}

CEnumFormatEtc::~CEnumFormatEtc()
{
    delete[] m_pStrFE;
}

HRESULT STDMETHODCALLTYPE CEnumFormatEtc::QueryInterface(REFIID riid, LPVOID* ppvOut) noexcept
{
    *ppvOut = nullptr;

    // IUnknown
    if (IsEqualIID(riid, IID_IUnknown)) {
        *ppvOut = this;
    }

    // IEnumFORMATETC
    else if (IsEqualIID(riid, IID_IEnumFORMATETC)) {
        *ppvOut = (IEnumFORMATETC*)this;
    }

    if (*ppvOut) {
        (*(LPUNKNOWN*)ppvOut)->AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CEnumFormatEtc::AddRef() noexcept
{
    return ++m_cRefCount;
}

ULONG STDMETHODCALLTYPE CEnumFormatEtc::Release() noexcept
{
    if (--m_cRefCount == 0) {
        delete this;
        return 0;
    }

    return m_cRefCount;
}

HRESULT STDMETHODCALLTYPE CEnumFormatEtc::Next(ULONG celt, LPFORMATETC pFE, ULONG* puFetched) noexcept
{
    ULONG cReturn = 0L;

    if (celt <= 0 || pFE == nullptr || m_iCur >= m_cItems)
        return S_FALSE;

    if (puFetched == nullptr && celt != 1)
        return S_FALSE;

    if (puFetched != nullptr)
        *puFetched = 0;

    while (m_iCur < m_cItems && celt > 0) {
        *pFE++ = m_pStrFE[m_iCur++];
        cReturn++;
        celt--;
    }

    if (nullptr != puFetched)
        *puFetched = (cReturn - celt);

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEnumFormatEtc::Skip(ULONG celt) noexcept
{
    if ((m_iCur + celt) >= m_cItems)
        return S_FALSE;

    m_iCur += celt;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEnumFormatEtc::Reset() noexcept
{
    m_iCur = 0;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEnumFormatEtc::Clone(IEnumFORMATETC** ppCloneEnumFormatEtc) noexcept
{
    CEnumFormatEtc* newEnum;

    if (nullptr == ppCloneEnumFormatEtc)
        return S_FALSE;

    newEnum = new CEnumFormatEtc(m_pStrFE, m_cItems);

    if (newEnum == nullptr)
        return E_OUTOFMEMORY;

    newEnum->AddRef();
    newEnum->m_iCur = m_iCur;

    *ppCloneEnumFormatEtc = newEnum;

    return S_OK;
}
