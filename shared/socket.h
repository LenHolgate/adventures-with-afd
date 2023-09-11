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

#include <string>
#include <ws2tcpip.h>

inline SOCKET SetSocketNonBlocking(
   SOCKET s)
{
   // Set it as non-blocking

   unsigned long one = 1;

   if (0 != ioctlsocket(s, FIONBIO, &one))
   {
      ErrorExit("ioctlsocket");
   }

   return s;
}

constexpr USHORT NonListeningPort = 1;

inline void SetRecvBuffer(
   const SOCKET s,
   const int size)
{
   if (SOCKET_ERROR == setsockopt(s, SOL_SOCKET,  SO_RCVBUF, reinterpret_cast<const char *>(&size), sizeof(int)))
   {
      ErrorExit("setsockopt - SO_RCVBUF");
   }

   int getValue = 0;

   int valueSize = sizeof getValue;

   if (SOCKET_ERROR != getsockopt(s, SOL_SOCKET,  SO_RCVBUF, reinterpret_cast<char *>(&getValue), &valueSize))
   {
      if (valueSize == sizeof(int))
      {
         if (getValue != size)
         {
            ErrorExit("getsockopt - SO_RCVBUF - failed to set size");
         }
      }
      else
      {
         ErrorExit("getsockopt - SO_RCVBUF - result is not sizeof(int)");
      }
   }
   else
   {
      ErrorExit("getsockopt - SO_RCVBUF");
   }
}

inline void SetSendBuffer(
   const SOCKET s,
   const int size)
{
   if (SOCKET_ERROR == setsockopt(s, SOL_SOCKET,  SO_SNDBUF, reinterpret_cast<const char *>(&size), sizeof(int)))
   {
      ErrorExit("setsockopt - SO_SNDBUF");
   }

   int getValue = 0;

   int valueSize = sizeof getValue;

   if (SOCKET_ERROR != getsockopt(s, SOL_SOCKET,  SO_SNDBUF, reinterpret_cast<char *>(&getValue), &valueSize))
   {
      if (valueSize == sizeof(int))
      {
         if (getValue != size)
         {
            ErrorExit("getsockopt - SO_SNDBUF - failed to set size");
         }
      }
      else
      {
         ErrorExit("getsockopt - SO_SNDBUF - result is not sizeof(int)");
      }
   }
   else
   {
      ErrorExit("getsockopt - SO_SNDBUF");
   }
}

inline void ConnectNonBlocking(
   const SOCKET s,
   const sockaddr_in &addr)
{
   const int result = connect(s, reinterpret_cast<const sockaddr *>(&addr), sizeof addr);

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

inline void ConnectNonBlocking(
   const SOCKET s,
   const ULONG address,
   const USHORT remotePort)
{
   sockaddr_in addr {};

   /* Attempt to connect to an address that we won't be able to connect to. */
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(address);
   addr.sin_port = htons(remotePort);

   ConnectNonBlocking(s, addr);
}

inline void ConnectNonBlocking(
   const SOCKET s,
   const USHORT remotePort)
{
   ConnectNonBlocking(s, INADDR_LOOPBACK, remotePort);
}

inline void ConnectNonBlocking(
   const SOCKET s,
   const std::string_view &address,
   const USHORT remotePort)
{
   sockaddr_in addr {};

   if (SOCKET_ERROR == inet_pton(AF_INET, address.data(), &addr.sin_addr))
   {
      ErrorExit("inet_pton");
   }

   addr.sin_family = AF_INET;
   addr.sin_port = htons(remotePort);

   ConnectNonBlocking(s, addr);
}

inline USHORT Bind(
   SOCKET s,
   sockaddr_in &addr,
   const int recvBufferSize = -1,
   const USHORT basePort = 5050)
{
   if (recvBufferSize != -1)
   {
      SetRecvBuffer(s, recvBufferSize);
   }

   USHORT port = basePort;

   bool done = false;

   do
   {
      addr.sin_port = htons(port);

      if (0 == bind(s, reinterpret_cast<const sockaddr *>(&addr), sizeof addr))
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

   return port;
}

inline USHORT Bind(
   SOCKET s,
   const int recvBufferSize = -1,
   const USHORT basePort = 5050)
{
   sockaddr_in addr{};

   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = INADDR_ANY;

   return Bind(s, addr, recvBufferSize, basePort);
}

inline void ReadClientClose(
   const SOCKET s)
{
   constexpr int bufferLength = 1;

   char buffer[bufferLength]{};

   const int bytes = recv(s, buffer, bufferLength, 0);

   if (bytes == SOCKET_ERROR)
   {
      ErrorExit("recv");
   }

   if (bytes != 0)
   {
      ErrorExit("recv - expected 0 got " + std::to_string(bytes));
   }
}

inline void ReadFails(
   const SOCKET s,
   const DWORD expectedError)
{
   constexpr int bufferLength = 10;

   char buffer[bufferLength]{};

   const int bytes = recv(s, buffer, bufferLength, 0);

   if (bytes == SOCKET_ERROR)
   {
      const DWORD lastError = GetLastError();

      if (lastError != expectedError)
      {
         ErrorExit("recv");
      }
   }
   else
   {
      ErrorExit("recv - expected error got " + std::to_string(bytes) + " bytes");
   }
}

inline void Write(
   const SOCKET s,
   const std::string_view &message,
   const int flags = 0)
{
   const int length = static_cast<int>(message.length());

   const int ret = send(s, message.data(), length, flags);

   if (ret == SOCKET_ERROR)
   {
      ErrorExit("send");
   }

   if (ret != length)
   {
      ErrorExit("send - expected to sent " + std::to_string(length) + " but sent " + std::to_string(ret));
   }
}

inline int WriteUntilError(
   const SOCKET s,
   const std::string_view &message,
   const DWORD expectedError)
{
   const int length = static_cast<int>(message.length());

   const int ret = send(s, message.data(), length, 0);

   if (ret == SOCKET_ERROR)
   {
      const DWORD lastError = GetLastError();

      if (lastError != expectedError)
      {
         ErrorExit("send");
      }

      return 0;
   }

   if (ret != length)
   {
      const DWORD lastError = GetLastError();

      if (lastError != expectedError)
      {
         ErrorExit("ReadAndDiscardAllAvailable - recv");
      }
   }

   return ret;
}

inline size_t ReadAndDiscardAllAvailable(
   const SOCKET s,
   const int flags = 0)
{
   constexpr int bufferLength = 1024;

   char buffer[bufferLength]{};

   size_t totalBytes = 0;

   bool done = false;

   do
   {
      const int bytes = recv(s, buffer, bufferLength, flags);

      if (bytes == SOCKET_ERROR)
      {
         const DWORD lastError = GetLastError();

         if (lastError != WSAEWOULDBLOCK)
         {
            ErrorExit("ReadAndDiscardAllAvailable - recv");
         }

         done = true;
      }
      else
      {
         totalBytes += bytes;

         done = (bytes == 0);
      }
   }
   while (!done);

   return totalBytes;
}

inline void Abort(
   const SOCKET s)
{
   LINGER lingerStruct;

   lingerStruct.l_onoff = 1;
   lingerStruct.l_linger = 0;

   if (SOCKET_ERROR == setsockopt(
      s,
      SOL_SOCKET,
      SO_LINGER,
      reinterpret_cast<char *>(&lingerStruct),
      sizeof lingerStruct))
   {
      ErrorExit("Abort - setsockopt");
   }

   if (SOCKET_ERROR == closesocket(s))
   {
      ErrorExit("Abort - closesocket");
   }
}

inline void Close(
   const SOCKET s)
{
   if (SOCKET_ERROR == closesocket(s))
   {
      ErrorExit("closesocket");
   }
}

///////////////////////////////////////////////////////////////////////////////
// End of file: socket.h
///////////////////////////////////////////////////////////////////////////////
