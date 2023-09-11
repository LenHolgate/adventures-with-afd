#pragma once
///////////////////////////////////////////////////////////////////////////////
// File: tcp_socket.h
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

#include "socket.h"

inline SOCKET CreateTCPSocket()
{
   const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

   if (s == INVALID_SOCKET)
   {
      ErrorExit("socket");
   }

   return s;
}

inline SOCKET CreateNonBlockingTCPSocket()
{
   const SOCKET s = CreateTCPSocket();

   return SetSocketNonBlocking(s);
}

struct ListeningSocket
{
   ListeningSocket(
      const SOCKET s,
      const USHORT port)
      :  s(s),
         port(port)
   {
   }

   ListeningSocket(const ListeningSocket &) = delete;
   ListeningSocket(ListeningSocket &&) = delete;

   ListeningSocket& operator=(const ListeningSocket &) = delete;
   ListeningSocket& operator=(ListeningSocket &&) = delete;

   [[nodiscard]] SOCKET Accept() const
   {
      sockaddr_in addr {};

      int addressLength = sizeof addr;

      const SOCKET accepted = accept(s, reinterpret_cast<sockaddr *>(&addr), &addressLength);

      if (accepted == INVALID_SOCKET)
      {
         ErrorExit("accept");
      }

      // Set it as non-blocking

      unsigned long one = 1;

      if (0 != ioctlsocket(accepted, FIONBIO, &one))
      {
         ErrorExit("ioctlsocket");
      }

      return accepted;
   }

   ~ListeningSocket()
   {
      closesocket(s);
   }

   SOCKET s;

   USHORT port;
};

inline ListeningSocket CreateListeningSocket(
   sockaddr_in &addr,
   const int recvBufferSize = -1,
   const USHORT basePort = 5050)
{
   bool done = false;

   const SOCKET s = CreateTCPSocket();

   const USHORT port = Bind(s,  addr, recvBufferSize,basePort);

   if (SOCKET_ERROR == listen(s, 10))
   {
      ErrorExit("listen");
   }

   return ListeningSocket(s, port);
}

inline ListeningSocket CreateListeningSocketWithRecvBufferSpecified(
   const int recvBufferSize,
   const USHORT basePort = 5050)
{
   sockaddr_in addr {};

   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

   return CreateListeningSocket(addr, recvBufferSize, basePort);
}

inline ListeningSocket CreateListeningSocket(
   const USHORT basePort = 5050)
{
   sockaddr_in addr {};

   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

   return CreateListeningSocket(addr, -1, basePort);
}

inline ListeningSocket CreateListeningSocket(
   const std::string_view &address,
   const int recvBufferSize = -1,
   const USHORT basePort = 5050)
{
   sockaddr_in addr {};

   if (SOCKET_ERROR == inet_pton(AF_INET, address.data(), &addr.sin_addr))
   {
      ErrorExit("inet_pton");
   }

   addr.sin_family = AF_INET;
   addr.sin_port = htons(basePort);

   return CreateListeningSocket(addr, recvBufferSize, basePort);
}

///////////////////////////////////////////////////////////////////////////////
// End of file: tcp_socket.h
///////////////////////////////////////////////////////////////////////////////
