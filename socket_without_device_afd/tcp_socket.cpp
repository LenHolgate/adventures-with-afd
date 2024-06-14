///////////////////////////////////////////////////////////////////////////////
// File: tcp_socket.cpp
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

#include "../third_party/wepoll_magic.h"

#include <winternl.h>

#include "tcp_socket.h"

#include "shared/afd.h"

#include <exception>

static SOCKET CreateNonBlockingSocket()
{
   SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

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

tcp_socket::tcp_socket(
   HANDLE iocp,
   tcp_socket_callbacks &callbacks)
   :  s(CreateNonBlockingSocket()),
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

tcp_socket::~tcp_socket()
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

void tcp_socket::connect(
   const sockaddr &address,
   const int address_length)
{
   if (connection_state != state::created)
   {
      throw std::exception("already connected");
   }

   const int result = ::connect(s, &address, address_length);

   if (result == SOCKET_ERROR)
   {
      const DWORD lastError = WSAGetLastError();

      if (lastError != WSAEWOULDBLOCK)
      {
         throw std::exception("failed to connect");
      }
   }

   connection_state = state::pending_connect;

   events = AFD_POLL_SEND |                     // writable which also means "connected"
            AFD_POLL_DISCONNECT |               // client close
            AFD_POLL_ABORT |                    // closed
            AFD_POLL_LOCAL_CLOSE |              // we have closed
            AFD_POLL_CONNECT_FAIL;              // outbound connection failed

   poll(events);
}

bool tcp_socket::poll(
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

int tcp_socket::write(
   const BYTE *pData,
   const int data_length)
{
   if (connection_state != state::connected)
   {
      throw std::exception("not connected");
   }

   // write to socket, if we can't write all of it, queue it in our send buffer and poll for writability
   // on a writable event we can write from our send buffer and repeat until it's empty
   // if we write and our send buffer is not empty we MUST only add to our send buffer
   // if our send buffer is full we MUST fail the write and let the caller deal with it

   // this adds an additional layer of buffering for negligable advantage as we will always need
   // to be able to alert the caller that we're writable again because we can always fill our
   // write buffer

   int bytes = ::send(s, reinterpret_cast<const char *>(pData), data_length, 0);

   if (bytes == SOCKET_ERROR)
   {
      const DWORD lastError = WSAGetLastError();

      if (lastError == WSAECONNRESET ||
          lastError == WSAECONNABORTED ||
          lastError == WSAENETRESET)
      {
         //handle_events(AFD_POLL_ABORT, 0);
      }
      else if (lastError != WSAEWOULDBLOCK)
      {
         throw std::exception("failed to write");
      }

      bytes = 0;
   }

   if (bytes != data_length)
   {
      events |= (AFD_POLL_SEND |
                 AFD_POLL_DISCONNECT |               // client close
                 AFD_POLL_ABORT |                    // closed
                 AFD_POLL_LOCAL_CLOSE);              // we have closed

      poll(events);
   }

   return bytes;
}

int tcp_socket::read(
   BYTE *pBuffer,
   int buffer_length)
{
   if (connection_state != state::connected)
   {
      throw std::exception("not connected");
   }

   // try and read data into the buffer supplied
   // if we read anything then return the amount
   // if we read zero we have client closed
   // if we block, poll for readability and return 0

   // this breaks everything anybody expects about a socket read call returning 0 on client close...

   int bytes = recv(s, reinterpret_cast<char *>(pBuffer), buffer_length, 0);

   if (bytes == 0)
   {
      //handle_events(AFD_POLL_DISCONNECT, 0);
   }

   if (bytes == SOCKET_ERROR)
   {
      const DWORD lastError = WSAGetLastError();

      if (lastError == WSAECONNRESET ||
          lastError == WSAECONNABORTED ||
          lastError == WSAENETRESET)
      {
         //handle_events(AFD_POLL_ABORT, 0);
      }
      else if (lastError != WSAEWOULDBLOCK)
      {
         throw std::exception("failed to read");
      }

      bytes = 0;
   }

   if (bytes == 0)
   {
      events |= (AFD_POLL_RECEIVE |
                 AFD_POLL_DISCONNECT |               // client close
                 AFD_POLL_ABORT |                    // closed
                 AFD_POLL_LOCAL_CLOSE);              // we have closed

      poll(events);
   }

   return bytes;
}

void tcp_socket::close()
{
   // two options here, one is to always have a poll pending for close/reset events
   // the second is to only report those if we have a read or write poll pending
   // only polling when we need to is likely more efficient but makes the reporting
   // of closure dependent on reading/writing - much as with normal sockets, this
   // means that for long lived connections with little activity we would fail to
   // see closure unless we have a read pending, but, chances are we would have a
   // read pending...
   // for now, allow polling only for read/write and deal with closure callbacks
   // here if we don't have a poll pending...

   // this may complicate matters as we now have callbacks occurring from calls to
   // the socket api on the same thread, which we don't get from the polled
   // situation

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

void tcp_socket::shutdown(
   const shutdown_how how)
{
   if (connection_state != state::connected)
   {
      throw std::exception("not connected");
   }

   // there are no callbacks for local operations, we assume the caller
   // can track the fact that we've shutdown if it's interesting in remembering
   // this detail...

   if (SOCKET_ERROR == ::shutdown(s, static_cast<int>(how)))
   {
      throw std::exception("failed to shutdown");
   }
}

void tcp_socket::handle_events()
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

ULONG tcp_socket::handle_events(
   const ULONG eventsToHandle,
   const NTSTATUS status)
{
   // need to know what state we're in as we would do one thing for connect and other things when
   // connected?

   events = 0;

   if (connection_state == state::pending_connect)
   {
      if (AFD_POLL_CONNECT_FAIL & eventsToHandle)
      {
         connection_state = state::disconnected;

         callbacks.on_connection_failed(*this, status);
      }
      else if (AFD_POLL_SEND & eventsToHandle)
      {
         connection_state = state::connected;

         callbacks.on_connected(*this);
      }
   }
   else if (AFD_POLL_SEND & eventsToHandle)
   {
      callbacks.on_writable(*this);
   }

   if (AFD_POLL_RECEIVE & eventsToHandle)
   {
      callbacks.on_readable(*this);
   }

   if (AFD_POLL_RECEIVE_EXPEDITED & eventsToHandle)
   {
      callbacks.on_readable_oob(*this);
   }

   if (AFD_POLL_ABORT & eventsToHandle)
   {
      connection_state = state::disconnected;

      callbacks.on_connection_reset(*this);
   }

   if (AFD_POLL_DISCONNECT & eventsToHandle)
   {
      connection_state = state::disconnected;

      callbacks.on_client_close(*this);
   }

   if (AFD_POLL_LOCAL_CLOSE & eventsToHandle)
   {
      connection_state = state::disconnected;

      callbacks.on_disconnected(*this);
   }

   return events;
}

///////////////////////////////////////////////////////////////////////////////
// End of file: tcp_socket.cpp
///////////////////////////////////////////////////////////////////////////////
