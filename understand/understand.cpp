
///////////////////////////////////////////////////////////////////////////////
// File: understand.cpp
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

#include "shared/shared.h"
#include "third_party/wepoll_magic.h"
#include "third_party/GoogleTest/gtest.h"

#include <SDKDDKVer.h>

#include <winternl.h>

#include <iostream>

#pragma comment(lib, "ntdll.lib")

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  InitialiseWinsock();

  return RUN_ALL_TESTS();
}

// connect to valid server and read
// connect to valid server and it client closes
// connect to valid server and write
// connect to valid server and it closes
// connect to valid server and it aborts
// move our socket between afd handles on each call
// multiple sockets on one handle

struct AfDWithIOCP
{
   AfDWithIOCP()
      : afd(0),
        iocp(0)
   {
   }

   AfDWithIOCP(
      HANDLE afd,
      HANDLE iocp)
      : afd(afd),
        iocp(iocp)
   {
   }

   ~AfDWithIOCP()
   {

      CloseHandle(iocp);

      CloseHandle(afd);
   }

   HANDLE afd;
   HANDLE iocp;
};

AfDWithIOCP CreateAfdAndIOCP(
   LPCWSTR deviceName)
{
   const USHORT deviceNameLengthInBytes = static_cast<USHORT>(wcslen(deviceName) * sizeof(wchar_t));

   static const UNICODE_STRING deviceNameUString { deviceNameLengthInBytes, deviceNameLengthInBytes, const_cast<LPWSTR>(deviceName) };

   static OBJECT_ATTRIBUTES attributes = {
      sizeof(OBJECT_ATTRIBUTES),
      nullptr,
      const_cast<UNICODE_STRING *>(&deviceNameUString),
      0,
      nullptr,
      nullptr
   };

   HANDLE hAFD;

   IO_STATUS_BLOCK createStatusBlock {};  // so we can reason about the lifetime of the status block
                                          // used for polling we use a different one here...

   // A note from the wepoll source ... 
   // By opening \Device\Afd without specifying any extended attributes, we'll
   // get a handle that lets us talk to the AFD driver, but that doesn't have an
   // associated endpoint (so it's not a socket).

   // See https://notgull.github.io/device-afd/ for more useful information about \Device\Afd and why
   // it's useful

   NTSTATUS status = NtCreateFile(
      &hAFD,
      SYNCHRONIZE,
      &attributes,
      &createStatusBlock,
      nullptr,
      0,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      FILE_OPEN,
      0,
      nullptr,
      0);

   if (status != 0)
   {
      SetLastError(RtlNtStatusToDosError(status));

      ErrorExit("NtCreateFile");
   }

   // Create an IOCP for notifications...

   HANDLE hIOCP = CreateIOCP();

   // Associate the AFD handle with the IOCP...

   if (nullptr == CreateIoCompletionPort(hAFD, hIOCP, 0, 0))
   {
      ErrorExit("CreateIoCompletionPort");
   }

   if (!SetFileCompletionNotificationModes(hAFD, FILE_SKIP_SET_EVENT_ON_HANDLE))
   {
      ErrorExit("SetFileCompletionNotificationModes");
   }

   return AfDWithIOCP{ hAFD, hIOCP};
}

AfDWithIOCP CreateAfdAndIOCP()
{
   static LPCWSTR deviceName = L"\\Device\\Afd\\explore";   // Arbitrary name in the Afd namespace

   return CreateAfdAndIOCP(deviceName);
}

SOCKET CreateTCPSocket()
{
   SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

   if (s == INVALID_SOCKET)
   {
      ErrorExit("socket");
   }

   return s;
}

SOCKET CreateNonBlockingTCPSocket()
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

static constexpr ULONG AllEvents =
   AFD_POLL_RECEIVE |                  // readable
   AFD_POLL_RECEIVE_EXPEDITED |        // out of band
   AFD_POLL_SEND |                     // writable
   AFD_POLL_DISCONNECT |               // client close
   AFD_POLL_ABORT |                    // closed
   AFD_POLL_LOCAL_CLOSE |              // ?
   AFD_POLL_ACCEPT |                   // connection accepted on listening
   AFD_POLL_CONNECT_FAIL;              // outbound connection failed


#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(address, type, field) ((type *)( \
                                                  (char *)(address) - \
                                                  (ULONG_PTR)(&((type *)nullptr)->field)))
#endif

struct PollData
{
   PollData(
      SOCKET s)
      :  s(s),
         pollInfo{},
         statusBlock{}
   {
   }

   ~PollData()
   {
      closesocket(s);
   }

   SOCKET s;
   AFD_POLL_INFO pollInfo;
   IO_STATUS_BLOCK statusBlock;

};

bool SetupPollForSocketEvents(
   HANDLE hAfD,
   PollData &data,
   const ULONG events)
{
   // This is information about what we are interested in for the supplied socket.
   // We're polling for one socket, we are interested in the specified events
   // The other stuff is copied from wepoll - needs more investigation

   AFD_POLL_INFO pollInfoIn {};

   pollInfoIn.Exclusive = FALSE;
   pollInfoIn.NumberOfHandles = 1;
   pollInfoIn.Timeout.QuadPart = INT64_MAX;
   pollInfoIn.Handles[0].Handle = reinterpret_cast<HANDLE>(GetBaseSocket(data.s));
   pollInfoIn.Handles[0].Status = 0;
   pollInfoIn.Handles[0].Events = events;

   data.pollInfo = AFD_POLL_INFO{};

   data.statusBlock = IO_STATUS_BLOCK{};

   // kick off the poll

   NTSTATUS status = NtDeviceIoControlFile(
      hAfD,
      nullptr,
      nullptr,
      &data,
      &data.statusBlock,
      IOCTL_AFD_POLL,
      &pollInfoIn,
      sizeof (AFD_POLL_INFO),
      &data.pollInfo,
      sizeof(AFD_POLL_INFO));

   if (status == 0)
   {
      return true;
   }

   if (status != STATUS_PENDING)
   {
      SetLastError(RtlNtStatusToDosError(status));

      ErrorExit("NtDeviceIoControlFile");
   }

   return false;
}

void CancelPoll(
   HANDLE hAfD,
   IO_STATUS_BLOCK *pStatusBlock)
{
   if (!CancelIoEx(hAfD, reinterpret_cast<LPOVERLAPPED>(pStatusBlock)))
   {
      const DWORD lastError = GetLastError();

      if (lastError != ERROR_OPERATION_ABORTED)
      {
         ErrorExit("CancelIoEx");
      }
   }
}

void CancelAllPolling(
   HANDLE hAfD)
{
   CancelPoll(hAfD, nullptr);
}

void CancelPoll(
   HANDLE hAfD,
   PollData &data)
{
   CancelPoll(hAfD, &data.statusBlock);
}

PollData *GetCompletion(
   HANDLE hIOCP,
   const DWORD timeout,
   const DWORD expectedResult = ERROR_SUCCESS)
{
   DWORD numberOfBytes = 0;

   ULONG_PTR completionKey = 0;

   OVERLAPPED *pOverlapped = nullptr;

   // A completion will not happen until there is an event on the socket...
   // what about that timeout in the poll information?

   SetLastError(ERROR_SUCCESS);

   const auto result = ::GetQueuedCompletionStatus(hIOCP, &numberOfBytes, &completionKey, &pOverlapped, timeout);

   const DWORD lastError = GetLastError();

   if (lastError != expectedResult)
   {
      ErrorExit("GetQueuedCompletionStatus");
   }

   return reinterpret_cast<PollData *>(pOverlapped);
}

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

constexpr USHORT InvalidPort = 1;

TEST(AFDFirstTest, TestConnectFail)
{
   static LPCWSTR deviceName = L"\\Device\\Afd\\explore";   // Arbitrary name in the Afd namespace

   auto handles = CreateAfdAndIOCP(deviceName);

   // As we'll see below, the 'PollData' (socket, status block and outbound poll info) need to stay
   // valid until the event completes...
   // This is per connection data and per operation data but we only ever have one operation
   // per connection...

   PollData data(CreateNonBlockingTCPSocket());

   // It's unlikely to complete straight away here as we haven't done anything with
   // the socket, but I expect that once the socket is connected we could get immediate
   // completions and we could, possibly, set 'FILE_SKIP_COMPLETION_PORT_ON_SUCCESS` for the
   // AFD association...

   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   // poll is pending

   ConnectNonBlocking(data.s, InvalidPort);

   // connect is pending, it will eventually time out...

   PollData *pData = GetCompletion(handles.iocp, INFINITE);

   // the completion returns the PollData which needs to remain valid for the period of the poll

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_CONNECT_FAIL, pData->pollInfo.Handles[0].Events);
}

class AFDTests : public testing::Test
{
   protected :

      AFDTests()
         :  handles(CreateAfdAndIOCP()),
            data(CreateNonBlockingTCPSocket())
      {
      }

      ~AFDTests()
      {
      }

      AfDWithIOCP handles;

      PollData data;
};

TEST_F(AFDTests, TestConnectCancel)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   ConnectNonBlocking(data.s, InvalidPort);

   CancelPoll(handles.afd, data);

   PollData *pData = GetCompletion(handles.iocp, INFINITE, ERROR_OPERATION_ABORTED);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(0, pData->pollInfo.Handles[0].Events);
}

TEST_F(AFDTests, TestConnectCancelAll)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   ConnectNonBlocking(data.s, InvalidPort);

   CancelAllPolling(handles.afd);

   PollData *pData = GetCompletion(handles.iocp, INFINITE, ERROR_OPERATION_ABORTED);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(0, pData->pollInfo.Handles[0].Events);
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

ListeningSocket CreateListeningSocket()
{
   bool done = false;

   SOCKET s = CreateTCPSocket();

   USHORT port = 5050;

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

TEST_F(AFDTests, TestConnect)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // poll again for this socket - no changes, socket stays wriable, polling is level triggered...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   sockaddr_in addr {};

   int addressLength = sizeof(addr);

   SOCKET s = accept(listeningSocket.s, reinterpret_cast<sockaddr *>(&addr), &addressLength);

   if (s == INVALID_SOCKET)
   {
      ErrorExit("accept");
   }

   // accepted...

   closesocket(s);

   // disconnected...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_DISCONNECT, pData->pollInfo.Handles[0].Events);
}

TEST_F(AFDTests, TestPollIsLevelTriggered)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // poll again for this socket - no changes, socket stays wriable, polling is level triggered...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // poll again for this socket - no changes, socket stays wriable, polling is level triggered...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);
}

TEST_F(AFDTests, TestPollCompletionReportsStateAtTimeOfPoll)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   sockaddr_in addr {};

   int addressLength = sizeof(addr);

   SOCKET s = accept(listeningSocket.s, reinterpret_cast<sockaddr *>(&addr), &addressLength);

   if (s == INVALID_SOCKET)
   {
      ErrorExit("accept");
   }

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // This next poll will return the socket state as of NOW when we query the result
   // from the IOCP after the state change...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   // accepted...

   closesocket(s);

   // disconnected...

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // This next poll will report the disconnect...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_DISCONNECT, pData->pollInfo.Handles[0].Events);
}

///////////////////////////////////////////////////////////////////////////////
// End of file: understand.cpp
///////////////////////////////////////////////////////////////////////////////
