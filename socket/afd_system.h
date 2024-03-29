#pragma once
///////////////////////////////////////////////////////////////////////////////
// File: afd_system.h
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

#include "../shared/afd.h"

class afd_system_events
{
   public :

      virtual void handle_events() = 0;

   protected :

      virtual ~afd_system_events() = default;
};

class afd_system
{
   public :

      virtual void associate_socket(
         ULONG slot,
         SOCKET s,
         afd_events &events) = 0;

      virtual void disassociate_socket(
         ULONG slot) = 0;

      virtual bool poll(
         ULONG slot,
         ULONG events) = 0;

   protected :

      virtual ~afd_system() = default;
};

///////////////////////////////////////////////////////////////////////////////
// End of file: afd_system.h
///////////////////////////////////////////////////////////////////////////////
