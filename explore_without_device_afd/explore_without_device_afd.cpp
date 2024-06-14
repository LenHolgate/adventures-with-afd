
///////////////////////////////////////////////////////////////////////////////
// File: explore_without_device_afd.cpp
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

#include "../shared/shared.h"
#include "../third_party/wepoll_magic.h"

#include <SDKDDKVer.h>

#include <winternl.h>

#include <iostream>

#pragma comment(lib, "ntdll.lib")

using std::cout;
using std::endl;

int main()
{
   InitialiseWinsock();

   // Create an IOCP for notifications...

   HANDLE hIOCP = CreateIOCP();

   // Create a stream socket

   SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);

   if (s == INVALID_SOCKET)
   {
      ErrorExit("socket");
   }

   // Set it as non-blocking

   unsigned long one = 1;

   if (0 != ioctlsocket(s, (long) FIONBIO, &one))
   {
      ErrorExit("ioctlsocket");
   }


   // Associate the socket with the IOCP...

   if (nullptr == CreateIoCompletionPort(reinterpret_cast<HANDLE>(s), hIOCP, 0, 0))
   {
      ErrorExit("CreateIoCompletionPort");
   }

   if (!SetFileCompletionNotificationModes(reinterpret_cast<HANDLE>(s), FILE_SKIP_SET_EVENT_ON_HANDLE))
   {
      ErrorExit("SetFileCompletionNotificationModes");
   }

   // These are the events that wepoll suggests that AFD exposes...

   constexpr ULONG events = 
      AFD_POLL_RECEIVE |                  // readable
      AFD_POLL_RECEIVE_EXPEDITED |        // out of band
      AFD_POLL_SEND |                     // writable
      AFD_POLL_DISCONNECT |               // client close
      AFD_POLL_ABORT |                    // closed
      AFD_POLL_LOCAL_CLOSE |              // ?
      AFD_POLL_ACCEPT |                   // connection accepted on listening
      AFD_POLL_CONNECT_FAIL;              // outbound connection failed


   // This is information about what we are interested in for the supplied socket.
   // We're polling for one socket, we are interested in the specified events
   // The other stuff is copied from wepoll - needs more investigation

   AFD_POLL_INFO pollInfoIn {};

   pollInfoIn.Exclusive = FALSE;
   pollInfoIn.NumberOfHandles = 1;
   pollInfoIn.Timeout.QuadPart = INT64_MAX;
   pollInfoIn.Handles[0].Handle = reinterpret_cast<HANDLE>(GetBaseSocket(s));
   pollInfoIn.Handles[0].Status = 0;
   pollInfoIn.Handles[0].Events = events;

   // To make it clear that the inbound and outbound poll structures can be different
   // we use a different one...

   // As we'll see below, the status block and the outbound poll info need to stay
   // valid until the event completes...

   AFD_POLL_INFO pollInfoOut {};

   IO_STATUS_BLOCK pollStatusBlock {};

   // kick off the poll

   NTSTATUS status = NtDeviceIoControlFile(
      reinterpret_cast<HANDLE>(s),
      nullptr,
      nullptr,
      &pollStatusBlock,
      &pollStatusBlock,
      IOCTL_AFD_POLL,
      &pollInfoIn,
      sizeof (pollInfoIn),
      &pollInfoOut,
      sizeof(pollInfoOut));

   if (status == 0)
   {
      // It's unlikely to complete straight away here as we haven't done anything with
      // the socket, but I expect that once the socket is connected we could get immediate
      // completions and we could, possibly, set 'FILE_SKIP_COMPLETION_PORT_ON_SUCCESS` for the
      // AFD association...

      cout << "success" << endl;
   }
   else if (status == STATUS_PENDING)
   {
      cout << "pending" << endl;

      DWORD numberOfBytes = 0;

      ULONG_PTR completionKey = 0;

      OVERLAPPED *pOverlapped = 0;

      // A completion will not happen until there is an event on the socket...
      // what about that timeout in the poll information?

      if (::GetQueuedCompletionStatus(hIOCP, &numberOfBytes, &completionKey, &pOverlapped, 1000))
      {
         ErrorExit("GetQueuedCompletionStatus - Unexpected!");
      }

      sockaddr_in addr {};

      /* Attempt to connect to an address that we won't be able to connect to. */
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      addr.sin_port = htons(1);

      int result = connect(s, (struct sockaddr*) &addr, sizeof addr);

      if (result == SOCKET_ERROR)
      {
         const DWORD lastError = WSAGetLastError();

         if (lastError == WSAEWOULDBLOCK)
         {
            cout << "connect would block" << endl;

            if (!::GetQueuedCompletionStatus(hIOCP, &numberOfBytes, &completionKey, &pOverlapped, INFINITE))
            {
               ErrorExit("GetQueuedCompletionStatus");
            }

            cout << "got completion" << endl;

            IO_STATUS_BLOCK *pStatus = reinterpret_cast<IO_STATUS_BLOCK *>(pOverlapped);

            if (pStatus == &pollStatusBlock)
            {
               cout << "status block as expected" << endl;

               // pStatus is the status block that was submitted when we polled, therefore it identifies our socket
               // we could use the 'extended overlapped' trick to obtain the containing object and map from this
               // back to a 'per connection' structure - note that this status block MUST live for as long as the operation
               // is outstanding...
            }

            // In addition pollInfoOut now contains details of the poll result, this object must also live for the
            // duration of the poll operation.

            cout << "poll event = " << pollInfoOut.Handles[0].Events << endl;
         }
         else
         {
            ErrorExit("connect");
         }
      }
      else
      {
         ErrorExit("connect");
      }

   }
   else
   {
      SetLastError(RtlNtStatusToDosError(status));

      ErrorExit("NtDeviceIoControlFile");
   }

   closesocket(s);

   CloseHandle(hIOCP);

   cout << "all done" << endl;
}

///////////////////////////////////////////////////////////////////////////////
// End of file: explore_without_device_afd.cpp
///////////////////////////////////////////////////////////////////////////////
