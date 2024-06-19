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

//#define DEBUG_POLLING

#ifdef DEBUG_POLLING
#define DEBUGGING(_m) _m
#else
#define DEBUGGING(_m)
#endif

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
   : tcp_socket(
      iocp,
      CreateNonBlockingSocket(),
      callbacks)
{
}

tcp_socket::tcp_socket(
   HANDLE iocp,
   SOCKET s,
   tcp_socket_callbacks &callbacks)
   :  s(s),
      baseSocket(GetBaseSocket(s)),
      pollInfoIn{},
      pollInfoOut{},
      statusBlock{},
      events(0),
      callbacks(callbacks),
      connection_state(state::created),
      handling_events(false)
{
   // Associate the AFD handle with the IOCP...

   if (nullptr == CreateIoCompletionPort(reinterpret_cast<HANDLE>(baseSocket), iocp, reinterpret_cast<ULONG_PTR>(static_cast<afd_events *>(this)), 0))
   {
      ErrorExit("CreateIoCompletionPort");
   }

   if (!SetFileCompletionNotificationModes(reinterpret_cast<HANDLE>(baseSocket), FILE_SKIP_SET_EVENT_ON_HANDLE))
   {
      ErrorExit("SetFileCompletionNotificationModes");
   }

   pollInfoIn.Exclusive = TRUE;
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

   events = AllEvents;

   poll(events);
}

void tcp_socket::accepted()
{
   if (connection_state != state::created)
   {
      throw std::exception("already accepted");
   }

   connection_state = state::pending_accept;

   events = AllEvents;

   poll(events);
}

bool tcp_socket::poll(
   const ULONG events)
{
   DEBUGGING(std::cout << this << " - poll - " << std::hex << events << std::endl);

   if (s != INVALID_SOCKET)
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
         &statusBlock);
   }

   return false;
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
         DEBUGGING(std::cout << this << " - write - connection aborted" << std::endl);
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
      if ((events & AFD_POLL_SEND) == 0)
      {
         events |= AFD_POLL_SEND;

         if (!handling_events)
         {
            poll(events);
         }
      }
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
      DEBUGGING(std::cout << this << " - read - client closed" << std::endl);
      //handle_events(AFD_POLL_DISCONNECT, 0);
   }

   if (bytes == SOCKET_ERROR)
   {
      const DWORD lastError = WSAGetLastError();

      if (lastError == WSAECONNRESET ||
          lastError == WSAECONNABORTED ||
          lastError == WSAENETRESET)
      {
         DEBUGGING(std::cout << this << " - read - connection aborted" << std::endl);

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
      if ((events & AFD_POLL_RECEIVE) == 0)
      {
         events |= AFD_POLL_RECEIVE;

         if (!handling_events)
         {
            poll(events);
         }
      }
   }

   return bytes;
}

void tcp_socket::close()
{
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
   if (s != INVALID_SOCKET)
   {
      if (connection_state != state::connected &&
          connection_state != state::client_closed)
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
}

bool tcp_socket::handle_events()
{
   bool handled = false;

   DEBUGGING(std::cout << this << " - handle_events" << std::endl);

   if (pollInfoOut.NumberOfHandles)
   {
      if (pollInfoOut.NumberOfHandles != 1)
      {
         throw std::exception("unexpected number of handles");
      }

      // process events...

      if (pollInfoOut.Handles[0].Status || pollInfoOut.Handles[0].Events)
      {
         handled = true;

         handling_events = true;

         pollInfoIn.Handles[0].Events = handle_events(pollInfoOut.Handles[0].Events, RtlNtStatusToDosError(pollInfoOut.Handles[0].Status));

         handling_events = false;

         if (s != INVALID_SOCKET)
         {
            if (pollInfoIn.Handles[0].Events)
            {
               poll(pollInfoIn.Handles[0].Events);
            }
         }
         else
         {
            callbacks.on_connection_complete();
         }
      }
   }
   else
   {
      DEBUGGING(std::cout << this << " - handle_events - no events" << std::endl);
   }

   return handled;
}

ULONG tcp_socket::handle_events(
   const ULONG eventsToHandle,
   const NTSTATUS status)
{
   // need to know what state we're in as we would do one thing for connect and other things when
   // connected?

   DEBUGGING(std::cout << this << " - handle_events: " << std::hex << eventsToHandle << std::endl);

   if (connection_state == state::pending_connect ||
       connection_state == state::pending_accept)
   {
      if (AFD_POLL_CONNECT_FAIL & eventsToHandle)
      {
         connection_state = state::disconnected;

         events &= ~AFD_POLL_CONNECT_FAIL;

         callbacks.on_connection_failed(*this, status);
      }
      else if (AFD_POLL_CONNECT & eventsToHandle)
      {
         connection_state = state::connected;

         events &= ~AFD_POLL_CONNECT;
         events &= ~AFD_POLL_SEND;

         callbacks.on_connected(*this);
      }
   }
   else if (AFD_POLL_SEND & eventsToHandle)
   {
      events &= ~AFD_POLL_SEND;

      callbacks.on_writable(*this);
   }

   if (AFD_POLL_RECEIVE & eventsToHandle)
   {
      events &= ~AFD_POLL_RECEIVE;

      callbacks.on_readable(*this);
   }

   if (AFD_POLL_RECEIVE_EXPEDITED & eventsToHandle)
   {
      events &= ~AFD_POLL_RECEIVE_EXPEDITED;

      callbacks.on_readable_oob(*this);
   }

   if (AFD_POLL_ABORT & eventsToHandle)
   {
      events &= ~AFD_POLL_ABORT;

      connection_state = state::disconnected;

      callbacks.on_connection_reset(*this);
   }

   if (AFD_POLL_DISCONNECT & eventsToHandle)
   {
      events &= ~AFD_POLL_DISCONNECT;

      DEBUGGING(std::cout << this << " - AFD_POLL_DISCONNECT" << std::endl);

      callbacks.on_client_close(*this);

      connection_state = state::client_closed;
   }

   if (AFD_POLL_LOCAL_CLOSE & eventsToHandle)
   {
      events &= ~AFD_POLL_LOCAL_CLOSE;

      connection_state = state::disconnected;

      callbacks.on_disconnected(*this);
   }

   return events;
}

///////////////////////////////////////////////////////////////////////////////
// End of file: tcp_socket.cpp
///////////////////////////////////////////////////////////////////////////////
