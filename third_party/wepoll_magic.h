#pragma once
///////////////////////////////////////////////////////////////////////////////
// File: wepoll_magic.h
///////////////////////////////////////////////////////////////////////////////
//
// The code in this file is released under the The MIT License (MIT)
//
// Copyright (c) 2023 Len Holgate.
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
//
// Based on code from wepoll.
//
// wepoll - epoll for Windows
// https://github.com/piscisaureus/wepoll
//
// Copyright 2012-2020, Bert Belder <bertbelder@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 


#include "../shared/shared.h"

enum
{
   IOCTL_AFD_POLL = 0x00012024
};

struct AFD_POLL_HANDLE_INFO {
  HANDLE Handle;
  ULONG Events;
  NTSTATUS Status;
};

struct AFD_POLL_INFO {
  LARGE_INTEGER Timeout;
  ULONG NumberOfHandles;
  ULONG Exclusive;
  AFD_POLL_HANDLE_INFO Handles[1];
};

#ifndef SIO_BSP_HANDLE_POLL
#define SIO_BSP_HANDLE_POLL 0x4800001D
#endif

#ifndef SIO_BASE_HANDLE
#define SIO_BASE_HANDLE 0x48000022
#endif

enum
{
   AFD_POLL_RECEIVE                    = 0x0001,
   AFD_POLL_RECEIVE_EXPEDITED          = 0x0002,
   AFD_POLL_SEND                       = 0x0004,
   AFD_POLL_DISCONNECT                 = 0x0008,
   AFD_POLL_ABORT                      = 0x0010,
   AFD_POLL_LOCAL_CLOSE                = 0x0020,
   AFD_POLL_CONNECT                    = 0x0040,
   AFD_POLL_ACCEPT                     = 0x0080,
   AFD_POLL_CONNECT_FAIL               = 0x0100,
   AFD_POLL_QOS                        = 0x0200,
   AFD_POLL_GROUP_QOS                  = 0x0400,
   AFD_POLL_ROUTING_INTERFACE_CHANGE   = 0x0800,
   AFD_POLL_ADDRESS_LIST_CHANGE        = 0x1000
};

static SOCKET GetBaseSocket(
   const SOCKET s,
   const DWORD ioctl)
{
   SOCKET baseSocket;

   DWORD bytes;

   if (WSAIoctl(
      s,
      ioctl,
      nullptr,
      0,
      &baseSocket,
      sizeof baseSocket,
      &bytes,
      nullptr,
      nullptr) != SOCKET_ERROR)
   {
      return baseSocket;
   }

   return INVALID_SOCKET;
}

static SOCKET GetBaseSocket(
   SOCKET s)
{
   for (;;)
   {
      SOCKET baseSocket = GetBaseSocket(s, SIO_BASE_HANDLE);

      if (baseSocket != INVALID_SOCKET)
      {
         return baseSocket;
      }

    /* Even though Microsoft documentation clearly states that LSPs should
     * never intercept the `SIO_BASE_HANDLE` ioctl [1], Komodia based LSPs do
     * so anyway, breaking it, with the apparent intention of preventing LSP
     * bypass [2]. Fortunately they don't handle `SIO_BSP_HANDLE_POLL`, which
     * will at least let us obtain the socket associated with the next winsock
     * protocol chain entry. If this succeeds, loop around and call
     * `SIO_BASE_HANDLE` again with the returned BSP socket, to make sure that
     * we unwrap all layers and retrieve the actual base socket.
     *  [1] https://docs.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls
     *  [2] https://www.komodia.com/newwiki/index.php?title=Komodia%27s_Redirector_bug_fixes#Version_2.2.2.6
     */

      baseSocket = GetBaseSocket(s, SIO_BSP_HANDLE_POLL);

      if (baseSocket != INVALID_SOCKET && baseSocket != s)
      {
         s = baseSocket;
      }
      else
      {
         ErrorExit("GetBaseSocket");
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
// End of file: wepoll_magic.h
///////////////////////////////////////////////////////////////////////////////
