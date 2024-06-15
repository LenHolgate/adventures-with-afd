///////////////////////////////////////////////////////////////////////////////
// File: tcp_listening_socket.cpp
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

#include "third_party/wepoll_magic.h"

#include <winternl.h>

#include "tcp_listening_socket.h"
#include "tcp_socket.h"

#include "shared/afd.h"

#include <exception>

static SOCKET CreateNonBlockingSocket()
{
   SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);

   if (s == INVALID_SOCKET)
   {
      throw std::exception("failed to create socket");
   }

   unsigned long one = 1;

   if (0 != ioctlsocket(s, FIONBIO, &one))
   {
      throw std::exception("ioctlsocket - failed to set socket not-blocking");
   }

   return s;
}


tcp_listening_socket::tcp_listening_socket(
   HANDLE iocp,
   tcp_listening_socket_callbacks &callbacks)
   :  iocp(iocp),
      s(CreateNonBlockingSocket()),
      baseSocket(GetBaseSocket(s)),
      pollInfoIn{},
      pollInfoOut{},
      statusBlock{},
      events(0),
      callbacks(callbacks),
      connection_state(state::created)
{
   // Associate the AFD handle with the IOCP...

   if (nullptr == CreateIoCompletionPort(reinterpret_cast<HANDLE>(baseSocket), iocp, 0, 0))
   {
      ErrorExit("CreateIoCompletionPort");
   }

   if (!SetFileCompletionNotificationModes(reinterpret_cast<HANDLE>(baseSocket), FILE_SKIP_SET_EVENT_ON_HANDLE))
   {
      ErrorExit("SetFileCompletionNotificationModes");
   }

   pollInfoIn.Exclusive = FALSE;
   pollInfoIn.NumberOfHandles = 1;
   pollInfoIn.Timeout.QuadPart = INT64_MAX;
   pollInfoIn.Handles[0].Handle = reinterpret_cast<HANDLE>(baseSocket);
   pollInfoIn.Handles[0].Status = 0;
   pollInfoIn.Handles[0].Events = events;
}

tcp_listening_socket::tcp_listening_socket(
   HANDLE iocp,
   const sockaddr &address,
   int address_length,
   tcp_listening_socket_callbacks &callbacks)
   : tcp_listening_socket(iocp, callbacks)
{
   bind(address, address_length);
}

tcp_listening_socket::~tcp_listening_socket()
{
   if (s != INVALID_SOCKET)
   {
      ::closesocket(s);

      s = INVALID_SOCKET;
   }

   if (baseSocket != INVALID_SOCKET)
   {
      ::closesocket(baseSocket);

      baseSocket = INVALID_SOCKET;
   }
}

void tcp_listening_socket::bind(
   const sockaddr &address,
   int address_length)
{
   if (connection_state != state::created)
   {
      throw std::exception("too late to bind");
   }

   if (0 != ::bind(s, &address, address_length))
   {
      const DWORD lastError = GetLastError();

      (void)lastError;

      throw std::exception("failed to bind");
   }

   connection_state = state::bound;
}

bool tcp_listening_socket::poll(
   const ULONG events)
{
   pollInfoIn.Handles[0].Status = 0;
   pollInfoIn.Handles[0].Events = events;

   // zero poll out

   memset(&pollInfoOut, 0, sizeof pollInfoOut);

   memset(&statusBlock, 0, sizeof statusBlock);

   return SetupPollForSocketEventsX(
      reinterpret_cast<HANDLE>(baseSocket),
      &pollInfoIn,
      sizeof pollInfoIn,
      statusBlock,
      &pollInfoOut,
      sizeof pollInfoOut,
      static_cast<afd_events *>(this));
}

void tcp_listening_socket::listen(
   const int backlog)
{
   if (SOCKET_ERROR == ::listen(s, backlog))
   {
      throw std::exception("failed to listen");
   }

   connection_state = state::listening;

   events = AllEvents;
   
   poll(events);
}

tcp_socket *tcp_listening_socket::accept(
   sockaddr &address,
   int &address_length,
   tcp_socket_callbacks &callbacks)
{
   tcp_socket *pSocket = nullptr;

   SOCKET accepted = ::accept(s, &address, &address_length);

   if (accepted != INVALID_SOCKET)
   {
      unsigned long one = 1;

      if (0 != ioctlsocket(accepted, FIONBIO, &one))
      {
         throw std::exception("ioctl failed to set non-blocking");
      }

      pSocket = new tcp_socket(iocp, accepted, callbacks);

      pSocket->accepted();
   }

   return pSocket;
}

void tcp_listening_socket::close()
{
   if (s != INVALID_SOCKET)
   {
      const bool triggerCallback = (events == 0);

      if (SOCKET_ERROR == closesocket(s))
      {
         throw std::exception("failed to close");
      }

      s = INVALID_SOCKET;

      if (triggerCallback)
      {
         handle_events(AFD_POLL_LOCAL_CLOSE, 0);
      }
   }
}

void tcp_listening_socket::handle_events()
{
   if (pollInfoOut.NumberOfHandles != 1)
   {
      throw std::exception("unexpected number of handles");
   }

   // process events...

   if (pollInfoOut.Handles[0].Status || pollInfoOut.Handles[0].Events)
   {
      pollInfoIn.Handles[0].Events = handle_events(pollInfoOut.Handles[0].Events, RtlNtStatusToDosError(pollInfoOut.Handles[0].Status));
   }
}

ULONG tcp_listening_socket::handle_events(
   const ULONG eventsToHandle,
   const NTSTATUS status)
{
   (void)status;

   if (connection_state == state::listening)
   {
      if (AFD_POLL_ACCEPT & eventsToHandle)
      {
         callbacks.on_incoming_connections(*this);
      }
   }

   if (AFD_POLL_ABORT & eventsToHandle)
   {
      connection_state = state::disconnected;

      callbacks.on_connection_reset(*this);

      events = 0;
   }

   if (AFD_POLL_LOCAL_CLOSE & eventsToHandle)
   {
      connection_state = state::disconnected;

      callbacks.on_disconnected(*this);

      events = 0;
   }

   if(events)
   {
      poll(events);
   }

   return events;
}

///////////////////////////////////////////////////////////////////////////////
// End of file: tcp_listening_socket.cpp
///////////////////////////////////////////////////////////////////////////////
