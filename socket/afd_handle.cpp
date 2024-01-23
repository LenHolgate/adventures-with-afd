///////////////////////////////////////////////////////////////////////////////
// File: afd_handle.cpp
///////////////////////////////////////////////////////////////////////////////
//
// The code in this file is released under the The MIT License (MIT)
//
// Copyright (c) 2024 Len Holgate.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
///////////////////////////////////////////////////////////////////////////////

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>

#include "afd_handle.h"
#include "afd_system.h"

afd_handle::afd_handle(
   afd_system &afd,
   const DWORD slot)
   :  afd(afd),
      slot(slot)
{
}

void afd_handle::associate_socket(
   SOCKET s,
   afd_events &events) const
{
   afd.associate_socket(slot, s, events);
}

void afd_handle::disassociate_socket() const
{
   afd.disassociate_socket(slot);
}

bool afd_handle::poll(
   ULONG events) const
{
   return afd.poll(slot, events);
}

///////////////////////////////////////////////////////////////////////////////
// End of file: afd_handle.cpp
///////////////////////////////////////////////////////////////////////////////
