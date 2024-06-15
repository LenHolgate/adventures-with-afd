#pragma once
///////////////////////////////////////////////////////////////////////////////
// File: tcp_listening_socket.h
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

#include "afd_events.h"

class tcp_socket;
class tcp_socket_callbacks;
class tcp_listening_socket;

class tcp_listening_socket_callbacks
{
   public :

      virtual void on_incoming_connections(
         tcp_listening_socket &s) = 0;

      virtual void on_connection_reset(
         tcp_listening_socket &s) = 0;

      virtual void on_disconnected(
         tcp_listening_socket &s) = 0;

   protected :

      virtual ~tcp_listening_socket_callbacks() = default;
};

class tcp_listening_socket : private afd_events
{
   public:

      tcp_listening_socket(
         HANDLE iocp,
         tcp_listening_socket_callbacks &callbacks);

      tcp_listening_socket(
         HANDLE iocp,
         const sockaddr &address,
         int address_length,
         tcp_listening_socket_callbacks &callbacks);

      ~tcp_listening_socket() override;

      void bind(
         const sockaddr &address,
         int address_length);

      void listen(
         int backlog);

      tcp_socket *accept(
         sockaddr &address,
         int &address_length,
         tcp_socket_callbacks &callbacks);

      void close();

   private :

      bool poll(
         ULONG events);

      void handle_events() override;

      ULONG handle_events(
         ULONG eventsToHandle,
         NTSTATUS status);

      HANDLE iocp;

      SOCKET s;

      SOCKET baseSocket;

      AFD_POLL_INFO pollInfoIn;
      AFD_POLL_INFO pollInfoOut;
      IO_STATUS_BLOCK statusBlock;

      ULONG events;

      tcp_listening_socket_callbacks &callbacks;

      enum class state
      {
         created,
         bound,
         listening,
         disconnected
      };

      state connection_state;
};

///////////////////////////////////////////////////////////////////////////////
// End of file: tcp_listening_socket.h
///////////////////////////////////////////////////////////////////////////////
