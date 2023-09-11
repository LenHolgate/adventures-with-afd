#pragma once
///////////////////////////////////////////////////////////////////////////////
// File: udp_socket.h
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

inline SOCKET CreateUDPSocket()
{
   const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

   if (s == INVALID_SOCKET)
   {
      ErrorExit("socket");
   }

   return s;
}

inline SOCKET CreateNonBlockingUDPSocket()
{
   const SOCKET s = CreateUDPSocket();

   return SetSocketNonBlocking(s);
}

inline void SendTo(
   SOCKET s,
   const USHORT port,
   const std::string_view &message,
   const int flags = 0)
{
   sockaddr_in addr{};

   addr.sin_family = AF_INET;
   addr.sin_port = htons(port);
   addr.sin_addr.s_addr = inet_addr("127.0.0.1");

   const int length = static_cast<int>(message.length());

   const int ret = sendto(s, message.data(), length, flags, reinterpret_cast<const sockaddr *>(&addr), sizeof addr);

   if (ret == SOCKET_ERROR)
   {
      ErrorExit("sendto");
   }

   if (ret != length)
   {
      ErrorExit("sendto - expected to sent " + std::to_string(length) + " but sent " + std::to_string(ret));
   }
}

///////////////////////////////////////////////////////////////////////////////
// End of file: socket.h
///////////////////////////////////////////////////////////////////////////////
