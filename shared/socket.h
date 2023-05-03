#pragma once
///////////////////////////////////////////////////////////////////////////////
// File: socket.h
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

#include "shared.h"

inline SOCKET CreateTCPSocket()
{
   SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

   if (s == INVALID_SOCKET)
   {
      ErrorExit("socket");
   }

   return s;
}

inline SOCKET CreateNonBlockingTCPSocket()
{
   SOCKET s = CreateTCPSocket();

   // Set it as non-blocking

   unsigned long one = 1;

   if (0 != ioctlsocket(s, (long) FIONBIO, &one))
   {
      ErrorExit("ioctlsocket");
   }
   return s;
}

constexpr USHORT NonListeningPort = 1;

void ConnectNonBlocking(
   SOCKET s,
   const USHORT remotePort)
{
   sockaddr_in addr {};

   /* Attempt to connect to an address that we won't be able to connect to. */
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   addr.sin_port = htons(remotePort);

   const int result = connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof addr);

   if (result == SOCKET_ERROR)
   {
      const DWORD lastError = WSAGetLastError();

      if (lastError == WSAEWOULDBLOCK)
      {
         return;
      }
   }
   ErrorExit("connect");
}

struct ListeningSocket
{
   ListeningSocket(
      SOCKET s,
      USHORT port)
      :  s(s),
         port(port)
   {
   }

   ~ListeningSocket()
   {
      closesocket(s);
   }

   SOCKET s;

   USHORT port;
};

ListeningSocket CreateListeningSocket(
   const USHORT basePort = 5050)
{
   bool done = false;

   SOCKET s = CreateTCPSocket();

   USHORT port = basePort;

   sockaddr_in addr {};

   /* Attempt to connect to an address that we won't be able to connect to. */
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

   do
   {
      addr.sin_port = htons(port);

      if (0 == bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof addr))
      {
         done = true;
      }
      else
      {
         const DWORD lastError = GetLastError();

         if (lastError == WSAEADDRINUSE)
         {
            ++port;
         }
         else
         {
            ErrorExit("bind");
         }
      }
   }
   while (!done);

   if (SOCKET_ERROR == listen(s, 10))
   {
      ErrorExit("listen");
   }

   return ListeningSocket(s, port);
}

///////////////////////////////////////////////////////////////////////////////
// End of file: socket.h
///////////////////////////////////////////////////////////////////////////////
