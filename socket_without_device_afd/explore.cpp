///////////////////////////////////////////////////////////////////////////////
// File: explore.cpp
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

#include "shared/afd.h"

#include "third_party/GoogleTest/gtest.h"
#include "third_party/GoogleTest/gmock.h"

#include <SDKDDKVer.h>

#include <winternl.h>

#pragma comment(lib, "ntdll.lib")

static SOCKET CreateNonBlockingSocket(
   HANDLE iocp)
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

   if (nullptr == CreateIoCompletionPort(reinterpret_cast<HANDLE>(s), iocp, 0, 0))
   {
      ErrorExit("CreateIoCompletionPort");
   }

   if (!SetFileCompletionNotificationModes(reinterpret_cast<HANDLE>(s), FILE_SKIP_SET_EVENT_ON_HANDLE))
   {
      ErrorExit("SetFileCompletionNotificationModes");
   }

   return s;
}

typedef NTSTATUS(NTAPI *NtCancelIoFileEx_t)(HANDLE           FileHandle,
                                            PIO_STATUS_BLOCK IoRequestToCancel,
                                            PIO_STATUS_BLOCK IoStatusBlock);
TEST(AFDExplore, TestSinglePoll)
{
   HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  
   NtCancelIoFileEx_t NtCancelIoFileEx = (NtCancelIoFileEx_t)(void *)GetProcAddress(ntdll, "NtCancelIoFileEx");

   const auto iocp = CreateIOCP();

   SOCKET s = CreateNonBlockingSocket(iocp);

   constexpr ULONG events = 
      AFD_POLL_RECEIVE |                  // readable
      AFD_POLL_RECEIVE_EXPEDITED |        // out of band
      AFD_POLL_SEND |                     // writable
      AFD_POLL_DISCONNECT |               // client close
      AFD_POLL_ABORT |                    // closed
      AFD_POLL_LOCAL_CLOSE |              // ?
      AFD_POLL_ACCEPT |                   // connection accepted on listening
      AFD_POLL_CONNECT_FAIL;              // outbound connection failed

   AFD_POLL_INFO pollInfoIn {};

   pollInfoIn.Exclusive = FALSE;
   pollInfoIn.NumberOfHandles = 1;
   pollInfoIn.Timeout.QuadPart = INT64_MAX;
   pollInfoIn.Handles[0].Handle = reinterpret_cast<HANDLE>(GetBaseSocket(s));
   pollInfoIn.Handles[0].Status = 0;
   pollInfoIn.Handles[0].Events = events;

   AFD_POLL_INFO pollInfoOut {};

   IO_STATUS_BLOCK pollStatusBlock1 {};

   IO_STATUS_BLOCK pollStatusBlock2 {};

   // kick off the poll

   NTSTATUS status = NtDeviceIoControlFile(
      reinterpret_cast<HANDLE>(s),
      nullptr,
      nullptr,
      &pollStatusBlock1,                        // the 'overlapped' returned from the IOCP
      &pollStatusBlock2,                        // that status block used for cancelling and for where the status is returned
      IOCTL_AFD_POLL,
      &pollInfoIn,
      sizeof (pollInfoIn),
      &pollInfoOut,
      sizeof(pollInfoOut));

   EXPECT_EQ(status, STATUS_PENDING);

   {
      DWORD numberOfBytes = 0;

      ULONG_PTR completionKey = 0;

      OVERLAPPED *pOverlapped = 0;

      const BOOL result = ::GetQueuedCompletionStatus(iocp, &numberOfBytes, &completionKey, &pOverlapped, 100);

      EXPECT_EQ(result, FALSE);

      EXPECT_EQ(GetLastError(), WAIT_TIMEOUT);
      EXPECT_EQ(numberOfBytes, 0);
      EXPECT_EQ(completionKey, 0);
      EXPECT_EQ(pOverlapped, nullptr);

      SetLastError(ERROR_SUCCESS);
   }

   IO_STATUS_BLOCK pollStatusBlock3 {};
   IO_STATUS_BLOCK pollStatusBlock4 {};

   const BOOL result = NtCancelIoFileEx(
      reinterpret_cast<HANDLE>(s),
      &pollStatusBlock2,
      &pollStatusBlock3);

   EXPECT_EQ(result, 0);

   {
      DWORD numberOfBytes = 0;

      ULONG_PTR completionKey = 0;

      OVERLAPPED *pOverlapped = 0;

      const BOOL result = ::GetQueuedCompletionStatus(iocp, &numberOfBytes, &completionKey, &pOverlapped, 100);

      EXPECT_EQ(result, FALSE);

      EXPECT_EQ(GetLastError(), ERROR_OPERATION_ABORTED);
      EXPECT_EQ(numberOfBytes, 0x10);
      EXPECT_EQ(completionKey, 0);
      EXPECT_EQ(reinterpret_cast<const IO_STATUS_BLOCK *>(pOverlapped), &pollStatusBlock1);

      #define STATUS_CANCELLED               ((DWORD   )0xC0000120L)

      EXPECT_EQ(pollStatusBlock2.Status, STATUS_CANCELLED);
      EXPECT_EQ(pollStatusBlock2.Information, 0x10);
   }

   ::closesocket(s);
}

TEST(AFDExplore, TestMultiplPollsExclusiveFalseSameControlBlock)
{
   HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  
   NtCancelIoFileEx_t NtCancelIoFileEx = (NtCancelIoFileEx_t)(void *)GetProcAddress(ntdll, "NtCancelIoFileEx");

   const auto iocp = CreateIOCP();

   SOCKET s = CreateNonBlockingSocket(iocp);

   constexpr ULONG events = 
      AFD_POLL_RECEIVE |                  // readable
      AFD_POLL_RECEIVE_EXPEDITED |        // out of band
      AFD_POLL_SEND |                     // writable
      AFD_POLL_DISCONNECT |               // client close
      AFD_POLL_ABORT |                    // closed
      AFD_POLL_LOCAL_CLOSE |              // ?
      AFD_POLL_ACCEPT |                   // connection accepted on listening
      AFD_POLL_CONNECT_FAIL;              // outbound connection failed

   AFD_POLL_INFO pollInfoIn {};

   pollInfoIn.Exclusive = FALSE;
   pollInfoIn.NumberOfHandles = 1;
   pollInfoIn.Timeout.QuadPart = INT64_MAX;
   pollInfoIn.Handles[0].Handle = reinterpret_cast<HANDLE>(GetBaseSocket(s));
   pollInfoIn.Handles[0].Status = 0;
   pollInfoIn.Handles[0].Events = events;

   AFD_POLL_INFO pollInfoOut {};

   IO_STATUS_BLOCK pollStatusBlock1 {};

   IO_STATUS_BLOCK pollStatusBlock2 {};

   // kick off the poll

   NTSTATUS status = NtDeviceIoControlFile(
      reinterpret_cast<HANDLE>(s),
      nullptr,
      nullptr,
      &pollStatusBlock1,                        // the 'overlapped' returned from the IOCP
      &pollStatusBlock2,                        // that status block used for cancelling and for where the status is returned
      IOCTL_AFD_POLL,
      &pollInfoIn,
      sizeof (pollInfoIn),
      &pollInfoOut,
      sizeof(pollInfoOut));

   EXPECT_EQ(status, STATUS_PENDING);

   {
      DWORD numberOfBytes = 0;

      ULONG_PTR completionKey = 0;

      OVERLAPPED *pOverlapped = 0;

      const BOOL result = ::GetQueuedCompletionStatus(iocp, &numberOfBytes, &completionKey, &pOverlapped, 100);

      EXPECT_EQ(result, FALSE);

      EXPECT_EQ(GetLastError(), WAIT_TIMEOUT);
      EXPECT_EQ(numberOfBytes, 0);
      EXPECT_EQ(completionKey, 0);
      EXPECT_EQ(pOverlapped, nullptr);

      SetLastError(ERROR_SUCCESS);
   }

   // kick off a second poll with the same control blocks...

   status = NtDeviceIoControlFile(
      reinterpret_cast<HANDLE>(s),
      nullptr,
      nullptr,
      &pollStatusBlock1,                        // the 'overlapped' returned from the IOCP
      &pollStatusBlock2,                        // that status block used for cancelling and for where the status is returned
      IOCTL_AFD_POLL,
      &pollInfoIn,
      sizeof (pollInfoIn),
      &pollInfoOut,
      sizeof(pollInfoOut));

   EXPECT_EQ(status, STATUS_PENDING);

   {
      DWORD numberOfBytes = 0;

      ULONG_PTR completionKey = 0;

      OVERLAPPED *pOverlapped = 0;

      const BOOL result = ::GetQueuedCompletionStatus(iocp, &numberOfBytes, &completionKey, &pOverlapped, 100);

      EXPECT_EQ(result, FALSE);

      EXPECT_EQ(GetLastError(), WAIT_TIMEOUT);
      EXPECT_EQ(numberOfBytes, 0);
      EXPECT_EQ(completionKey, 0);
      EXPECT_EQ(pOverlapped, nullptr);

      SetLastError(ERROR_SUCCESS);
   }

   IO_STATUS_BLOCK pollStatusBlock3 {};
   IO_STATUS_BLOCK pollStatusBlock4 {};

   const BOOL result = NtCancelIoFileEx(
      reinterpret_cast<HANDLE>(s),
      &pollStatusBlock2,
      &pollStatusBlock3);

   EXPECT_EQ(result, 0);

   {
      DWORD numberOfBytes = 0;

      ULONG_PTR completionKey = 0;

      OVERLAPPED *pOverlapped = 0;

      const BOOL result = ::GetQueuedCompletionStatus(iocp, &numberOfBytes, &completionKey, &pOverlapped, 100);

      EXPECT_EQ(result, FALSE);

      EXPECT_EQ(GetLastError(), ERROR_OPERATION_ABORTED);
      EXPECT_EQ(numberOfBytes, 0x10);
      EXPECT_EQ(completionKey, 0);
      EXPECT_EQ(reinterpret_cast<const IO_STATUS_BLOCK *>(pOverlapped), &pollStatusBlock1);

      EXPECT_EQ(pollStatusBlock2.Status, STATUS_CANCELLED);
      EXPECT_EQ(pollStatusBlock2.Information, 0x10);

      SetLastError(ERROR_SUCCESS);
   }

   {
      DWORD numberOfBytes = 0;

      ULONG_PTR completionKey = 0;

      OVERLAPPED *pOverlapped = 0;

      const BOOL result = ::GetQueuedCompletionStatus(iocp, &numberOfBytes, &completionKey, &pOverlapped, 100);

      EXPECT_EQ(result, FALSE);

      EXPECT_EQ(GetLastError(), ERROR_OPERATION_ABORTED);
      EXPECT_EQ(numberOfBytes, 0x10);
      EXPECT_EQ(completionKey, 0);
      EXPECT_EQ(reinterpret_cast<const IO_STATUS_BLOCK *>(pOverlapped), &pollStatusBlock1);

      EXPECT_EQ(pollStatusBlock2.Status, STATUS_CANCELLED);
      EXPECT_EQ(pollStatusBlock2.Information, 0x10);
   }

   ::closesocket(s);
}

TEST(AFDExplore, TestMultiplPollsExclusiveTrueSameControlBlock)
{
   HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  
   NtCancelIoFileEx_t NtCancelIoFileEx = (NtCancelIoFileEx_t)(void *)GetProcAddress(ntdll, "NtCancelIoFileEx");

   const auto iocp = CreateIOCP();

   SOCKET s = CreateNonBlockingSocket(iocp);

   constexpr ULONG events = 
      AFD_POLL_RECEIVE |                  // readable
      AFD_POLL_RECEIVE_EXPEDITED |        // out of band
      AFD_POLL_SEND |                     // writable
      AFD_POLL_DISCONNECT |               // client close
      AFD_POLL_ABORT |                    // closed
      AFD_POLL_LOCAL_CLOSE |              // ?
      AFD_POLL_ACCEPT |                   // connection accepted on listening
      AFD_POLL_CONNECT_FAIL;              // outbound connection failed

   AFD_POLL_INFO pollInfoIn {};

   pollInfoIn.Exclusive = TRUE;
   pollInfoIn.NumberOfHandles = 1;
   pollInfoIn.Timeout.QuadPart = INT64_MAX;
   pollInfoIn.Handles[0].Handle = reinterpret_cast<HANDLE>(GetBaseSocket(s));
   pollInfoIn.Handles[0].Status = 0;
   pollInfoIn.Handles[0].Events = events;

   AFD_POLL_INFO pollInfoOut {};

   IO_STATUS_BLOCK pollStatusBlock1 {};

   IO_STATUS_BLOCK pollStatusBlock2 {};

   // kick off the poll

   NTSTATUS status = NtDeviceIoControlFile(
      reinterpret_cast<HANDLE>(s),
      nullptr,
      nullptr,
      &pollStatusBlock1,                        // the 'overlapped' returned from the IOCP
      &pollStatusBlock2,                        // that status block used for cancelling and for where the status is returned
      IOCTL_AFD_POLL,
      &pollInfoIn,
      sizeof (pollInfoIn),
      &pollInfoOut,
      sizeof(pollInfoOut));

   EXPECT_EQ(status, STATUS_PENDING);

   {
      DWORD numberOfBytes = 0;

      ULONG_PTR completionKey = 0;

      OVERLAPPED *pOverlapped = 0;

      const BOOL result = ::GetQueuedCompletionStatus(iocp, &numberOfBytes, &completionKey, &pOverlapped, 100);

      EXPECT_EQ(result, FALSE);

      EXPECT_EQ(GetLastError(), WAIT_TIMEOUT);
      EXPECT_EQ(numberOfBytes, 0);
      EXPECT_EQ(completionKey, 0);
      EXPECT_EQ(pOverlapped, nullptr);

      SetLastError(ERROR_SUCCESS);
   }

   // kick off a second poll with the same control blocks...

   status = NtDeviceIoControlFile(
      reinterpret_cast<HANDLE>(s),
      nullptr,
      nullptr,
      &pollStatusBlock1,                        // the 'overlapped' returned from the IOCP
      &pollStatusBlock2,                        // that status block used for cancelling and for where the status is returned
      IOCTL_AFD_POLL,
      &pollInfoIn,
      sizeof (pollInfoIn),
      &pollInfoOut,
      sizeof(pollInfoOut));

   EXPECT_EQ(status, STATUS_PENDING);

   {
      DWORD numberOfBytes = 0;

      ULONG_PTR completionKey = 0;

      OVERLAPPED *pOverlapped = 0;

      const BOOL result = ::GetQueuedCompletionStatus(iocp, &numberOfBytes, &completionKey, &pOverlapped, 100);

      EXPECT_EQ(result, TRUE);

      EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
      EXPECT_EQ(numberOfBytes, 0x10);
      EXPECT_EQ(completionKey, 0);
      EXPECT_EQ(reinterpret_cast<const IO_STATUS_BLOCK *>(pOverlapped), &pollStatusBlock1);

      EXPECT_EQ(pollStatusBlock2.Status, 0);
      EXPECT_EQ(pollStatusBlock2.Information, 0x10);
   }

   IO_STATUS_BLOCK pollStatusBlock3 {};
   IO_STATUS_BLOCK pollStatusBlock4 {};

   const BOOL result = NtCancelIoFileEx(
      reinterpret_cast<HANDLE>(s),
      &pollStatusBlock2,
      &pollStatusBlock3);

   EXPECT_EQ(result, 0);

   {
      DWORD numberOfBytes = 0;

      ULONG_PTR completionKey = 0;

      OVERLAPPED *pOverlapped = 0;

      const BOOL result = ::GetQueuedCompletionStatus(iocp, &numberOfBytes, &completionKey, &pOverlapped, 100);

      EXPECT_EQ(result, FALSE);

      EXPECT_EQ(GetLastError(), ERROR_OPERATION_ABORTED);
      EXPECT_EQ(numberOfBytes, 0x10);
      EXPECT_EQ(completionKey, 0);
      EXPECT_EQ(reinterpret_cast<const IO_STATUS_BLOCK *>(pOverlapped), &pollStatusBlock1);

      EXPECT_EQ(pollStatusBlock2.Status, STATUS_CANCELLED);
      EXPECT_EQ(pollStatusBlock2.Information, 0x10);

      SetLastError(ERROR_SUCCESS);
   }

   {
      DWORD numberOfBytes = 0;

      ULONG_PTR completionKey = 0;

      OVERLAPPED *pOverlapped = 0;

      const BOOL result = ::GetQueuedCompletionStatus(iocp, &numberOfBytes, &completionKey, &pOverlapped, 100);

      EXPECT_EQ(result, FALSE);

      EXPECT_EQ(GetLastError(), WAIT_TIMEOUT);
      EXPECT_EQ(numberOfBytes, 0);
      EXPECT_EQ(completionKey, 0);
      EXPECT_EQ(reinterpret_cast<const IO_STATUS_BLOCK *>(pOverlapped), nullptr);
   }

   ::closesocket(s);
}

///////////////////////////////////////////////////////////////////////////////
// End of file: explore.cpp
///////////////////////////////////////////////////////////////////////////////
