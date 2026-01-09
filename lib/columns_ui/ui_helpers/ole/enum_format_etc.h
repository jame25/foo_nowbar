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

#pragma once

#include <windows.h>
#include <ole2.h>

class CEnumFormatEtc : public IEnumFORMATETC {
public:
    CEnumFormatEtc(LPFORMATETC pFE, int numberItems);
    ~CEnumFormatEtc();

    // BOOL FInit(HWND);

    // IUnknown members
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID*) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;

    // IEnumFORMATETC members
    HRESULT STDMETHODCALLTYPE Next(ULONG, LPFORMATETC, ULONG*) noexcept override;
    HRESULT STDMETHODCALLTYPE Skip(ULONG) noexcept override;
    HRESULT STDMETHODCALLTYPE Reset() noexcept override;
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC**) noexcept override;

private:
    DWORD m_cRefCount;
    LPFORMATETC m_pStrFE;
    ULONG m_iCur;
    ULONG m_cItems;
};
