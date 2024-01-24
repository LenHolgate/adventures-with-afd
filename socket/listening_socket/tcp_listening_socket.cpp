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

#include "shared/afd.h"

#include <exception>

tcp_listening_socket::tcp_listening_socket(
   afd_handle afd,
   tcp_listening_socket_callbacks &callbacks)
   :  afd(afd),
      s(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)),
      events(0),
      callbacks(callbacks),
      connection_state(state::created)
{
   if (s == INVALID_SOCKET)
   {
      throw std::exception("failed to create socket");
   }

   unsigned long one = 1;

   if (0 != ioctlsocket(s, FIONBIO, &one))
   {
      throw std::exception("ioctlsocket - failed to set socket not-blocking");
   }

   afd.associate_socket(s, *this);
}

tcp_listening_socket::tcp_listening_socket(
   afd_handle afd,
   const sockaddr &address,
   int address_length,
   tcp_listening_socket_callbacks &callbacks)
   : tcp_listening_socket(afd, callbacks)
{
   bind(address, address_length);
}

tcp_listening_socket::~tcp_listening_socket()
{
   afd.disassociate_socket();

   if (s != INVALID_SOCKET)
   {
      ::closesocket(s);

      s = INVALID_SOCKET;
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

void tcp_listening_socket::listen(
   const int backlog)
{
   if (SOCKET_ERROR == ::listen(s, backlog))
   {
      throw std::exception("failed to listen");
   }

   connection_state = state::listening;

   events = AllEvents;
   
   afd.poll(events);
}

SOCKET tcp_listening_socket::accept(
   sockaddr &address,
   int &address_length)
{
   const SOCKET accepted = ::accept(s, &address, &address_length);

   if (accepted == INVALID_SOCKET)
   {
      throw std::exception("failed to accept");
   }

   return accepted;
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

ULONG tcp_listening_socket::handle_events(
   const ULONG eventsToHandle,
   const NTSTATUS status)
{
   // need to know what state we're in as we would do one thing for connect and other things when
   // connected?

   events = 0;

   (void)eventsToHandle;
   (void)status;

   if (connection_state == state::listening)
   {
      if (AFD_POLL_ACCEPT & eventsToHandle)
      {
         callbacks.on_incoming_connection(*this);
      }
   }
   //else if (AFD_POLL_SEND & eventsToHandle)
   //{
   //   callbacks.on_writable(*this);
   //}

   //if (AFD_POLL_RECEIVE & eventsToHandle)
   //{
   //   callbacks.on_readable(*this);
   //}

   //if (AFD_POLL_RECEIVE_EXPEDITED & eventsToHandle)
   //{
   //   callbacks.on_readable_oob(*this);
   //}

   //if (AFD_POLL_ABORT & eventsToHandle)
   //{
   //   connection_state = state::disconnected;

   //   callbacks.on_connection_reset(*this);
   //}

   //if (AFD_POLL_DISCONNECT & eventsToHandle)
   //{
   //   connection_state = state::disconnected;

   //   callbacks.on_client_close(*this);
   //}

   //if (AFD_POLL_LOCAL_CLOSE & eventsToHandle)
   //{
   //   connection_state = state::disconnected;

   //   callbacks.on_disconnected(*this);
   //}

   return events;
}

///////////////////////////////////////////////////////////////////////////////
// End of file: tcp_listening_socket.cpp
///////////////////////////////////////////////////////////////////////////////
