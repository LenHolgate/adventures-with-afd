#pragma once
///////////////////////////////////////////////////////////////////////////////
// File: afd.h
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

#include "../third_party/wepoll_magic.h"

#include <winternl.h>

#include <vector>

#pragma comment(lib, "ntdll.lib")

struct AfDWithIOCP
{
   AfDWithIOCP()
      : afd(nullptr),
        iocp(nullptr)
   {
   }

   AfDWithIOCP(
      const HANDLE afd,
      const HANDLE iocp)
      : afd(afd),
        iocp(iocp)
   {
   }

   AfDWithIOCP(const AfDWithIOCP &) = delete;
   AfDWithIOCP(AfDWithIOCP &&) = delete;

   AfDWithIOCP& operator=(const AfDWithIOCP &) = delete;
   AfDWithIOCP& operator=(AfDWithIOCP &&) = delete;

   ~AfDWithIOCP()
   {
      CloseHandle(iocp);

      CloseHandle(afd);
   }

   HANDLE afd;
   HANDLE iocp;
};

inline AfDWithIOCP CreateAfdAndIOCP(
   const LPCWSTR deviceName,
   const UCHAR flags = FILE_SKIP_SET_EVENT_ON_HANDLE)
{
   const auto deviceNameLengthInBytes = static_cast<USHORT>(wcslen(deviceName) * sizeof(wchar_t));

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

   const NTSTATUS status = NtCreateFile(
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

   const HANDLE hIOCP = CreateIOCP();

   // Associate the AFD handle with the IOCP...

   if (nullptr == CreateIoCompletionPort(hAFD, hIOCP, 0, 0))
   {
      ErrorExit("CreateIoCompletionPort");
   }

   if (!SetFileCompletionNotificationModes(hAFD, flags))
   {
      ErrorExit("SetFileCompletionNotificationModes");
   }

   return AfDWithIOCP{ hAFD, hIOCP};
}

inline AfDWithIOCP CreateAfdAndIOCP()
{
   static auto deviceName = L"\\Device\\Afd\\explore";   // Arbitrary name in the Afd namespace

   return CreateAfdAndIOCP(deviceName);
}

static constexpr ULONG AllEventsExceptSend =
   AFD_POLL_RECEIVE |                  // readable
   AFD_POLL_RECEIVE_EXPEDITED |        // out of band
   AFD_POLL_DISCONNECT |               // client close
   AFD_POLL_ABORT |                    // closed
   AFD_POLL_LOCAL_CLOSE |              // we have closed
   AFD_POLL_ACCEPT |                   // connection accepted on listening
   AFD_POLL_CONNECT_FAIL;              // outbound connection failed

static constexpr ULONG AllEvents =
   AllEventsExceptSend |               // All of the rest
   AFD_POLL_SEND;                      // writable


#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(address, type, field) ((type *)( \
                                                  (char *)(address) - \
                                                  (ULONG_PTR)(&((type *)nullptr)->field)))
#endif

struct PollData
{
   explicit PollData(
      const SOCKET s)
      :  s(s),
         pollInfo{},
         statusBlock{}
   {
   }

   PollData(const PollData &) = delete;
   PollData(PollData &&) = delete;

   PollData& operator=(const PollData& other) = delete;
   PollData& operator=(PollData &&) = delete;

   ~PollData()
   {
      closesocket(s);
   }

   SOCKET s;
   AFD_POLL_INFO pollInfo;
   IO_STATUS_BLOCK statusBlock;
};

inline bool SetupPollForSocketEvents(
   const HANDLE hAfD,
   void *pInput,
   const ULONG inputSize,
   IO_STATUS_BLOCK &statusBlock,
   SOCKET s,
   void *pOutput,
   const ULONG outputSize,
   void *pContext,
   const ULONG events)
{
   if (inputSize < sizeof AFD_POLL_INFO)
   {
      ErrorExit("SetupPollForSocketEvents - input too small");
   }

   memset(pInput, 0, inputSize);

   // This is information about what we are interested in for the supplied socket.
   // We're polling for one socket, we are interested in the specified events
   // The other stuff is copied from wepoll - needs more investigation

   AFD_POLL_INFO &pollInfoIn = *reinterpret_cast<AFD_POLL_INFO *>(pInput);

   pollInfoIn.Exclusive = FALSE;
   pollInfoIn.NumberOfHandles = 1;
   pollInfoIn.Timeout.QuadPart = INT64_MAX;
   pollInfoIn.Handles[0].Handle = reinterpret_cast<HANDLE>(GetBaseSocket(s));
   pollInfoIn.Handles[0].Status = 0;
   pollInfoIn.Handles[0].Events = events;

   memset(pOutput, 0, outputSize);

   statusBlock = IO_STATUS_BLOCK{};

   // kick off the poll

   const NTSTATUS status = NtDeviceIoControlFile(
      hAfD,
      nullptr,
      nullptr,
      pContext,
      &statusBlock,
      IOCTL_AFD_POLL,
      pInput,
      inputSize,
      pOutput,
      outputSize);

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

inline bool SetupPollForSocketEventsX(
   const HANDLE hAfD,
   void *pInput,
   const ULONG inputSize,
   IO_STATUS_BLOCK &statusBlock,
   void *pOutput,
   const ULONG outputSize,
   void *pContext)
{
   if (inputSize < sizeof outputSize)
   {
      ErrorExit("SetupPollForSocketEvents - input too small");
   }

   // kick off the poll

   const NTSTATUS status = NtDeviceIoControlFile(
      hAfD,
      nullptr,
      nullptr,
      pContext,
      &statusBlock,
      IOCTL_AFD_POLL,
      pInput,
      inputSize,
      pOutput,
      outputSize);

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

inline bool SetupPollForSocketEvents(
   const HANDLE hAfD,
   IO_STATUS_BLOCK &statusBlock,
   SOCKET s,
   void *pOutput,
   const ULONG outputSize,
   void *pContext,
   const ULONG events)
{
   AFD_POLL_INFO pollInfoIn {};

   return SetupPollForSocketEvents(
      hAfD,
      &pollInfoIn,
      sizeof(pollInfoIn),
      statusBlock,
      s,
      pOutput,
      outputSize,
      pContext,
      events);
}

inline bool SetupPollForSocketEvents(
   const HANDLE hAfD,
   IO_STATUS_BLOCK &statusBlock,
   SOCKET s,
   AFD_POLL_INFO &pollInfo,
   void *pContext,
   const ULONG events)
{
   return SetupPollForSocketEvents(
      hAfD,
      statusBlock,
      s,
      &pollInfo,
      sizeof(pollInfo),
      pContext,
      events);
}


inline bool SetupPollForSocketEvents(
   const HANDLE hAfD,
   PollData &data,
   const ULONG events)
{
   return SetupPollForSocketEvents(hAfD, data.statusBlock, data.s, data.pollInfo, &data, events);
}

inline PollData *PollForSocketEvents(
   const HANDLE hAfD,
   PollData &data,
   const ULONG events)
{
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
      if (data.statusBlock.Status == 0)
      {
         return &data;
      }

      status = data.statusBlock.Status;
   }

   if (status != STATUS_PENDING)
   {
      SetLastError(RtlNtStatusToDosError(status));

      ErrorExit("NtDeviceIoControlFile");
   }

   return nullptr;
}

inline void CancelPoll(
   const HANDLE hAfD,
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

inline void CancelAllPolling(
   const HANDLE hAfD)
{
   CancelPoll(hAfD, nullptr);
}

inline void CancelPoll(
   const HANDLE hAfD,
   PollData &data)
{
   CancelPoll(hAfD, &data.statusBlock);
}

inline ULONG_PTR GetCompletionKey(
   const HANDLE hIOCP,
   const DWORD timeout,
   const DWORD expectedResult = ERROR_SUCCESS)
{
   DWORD numberOfBytes = 0;

   ULONG_PTR completionKey = 0;

   OVERLAPPED *pOverlapped = nullptr;

   // A completion will not happen until there is an event on the socket...
   // what about that timeout in the poll information?

   SetLastError(ERROR_SUCCESS);

   const auto result = GetQueuedCompletionStatus(hIOCP, &numberOfBytes, &completionKey, &pOverlapped, timeout);

   (void)result;

   const DWORD lastError = GetLastError();

   if (lastError != expectedResult)
   {
      ErrorExit("GetQueuedCompletionStatus");
   }

   return completionKey;
}

template <typename T>
inline T *GetCompletionKeyAs(
   const HANDLE hIOCP,
   const DWORD timeout,
   const DWORD expectedResult = ERROR_SUCCESS)
{
   return reinterpret_cast<T *>(GetCompletionKey(hIOCP, timeout, expectedResult));
}

template <typename T>
inline DWORD GetCompletionKeysAs(
   const HANDLE hIOCP,
   const DWORD timeout,
   std::vector<T *> &completionKeys)
{
   std::vector<OVERLAPPED_ENTRY> overlapped;

   overlapped.resize(completionKeys.size());

   DWORD numEntries = 0;

   if (GetQueuedCompletionStatusEx(hIOCP, &overlapped[0], sizeof(overlapped), &numEntries, timeout, FALSE))
   {
      for (DWORD i = 0; i < numEntries; ++i)
      {
         completionKeys[i] = reinterpret_cast<T *>(overlapped[i].lpCompletionKey);
      }

      completionKeys.resize(numEntries);
   }
   else
   {
      const DWORD lastError = GetLastError();

      if (lastError != WAIT_TIMEOUT)
      {
         ErrorExit("GetQueuedCompletionStatusEx");
      }
   }

   return numEntries;
}


inline OVERLAPPED *GetCompletion(
   const HANDLE hIOCP,
   const DWORD timeout,
   const DWORD expectedResult = ERROR_SUCCESS)
{
   DWORD numberOfBytes = 0;

   ULONG_PTR completionKey = 0;

   OVERLAPPED *pOverlapped = nullptr;

   // A completion will not happen until there is an event on the socket...
   // what about that timeout in the poll information?

   SetLastError(ERROR_SUCCESS);

   const auto result = GetQueuedCompletionStatus(hIOCP, &numberOfBytes, &completionKey, &pOverlapped, timeout);

   (void)result;

   const DWORD lastError = GetLastError();

   if (lastError != expectedResult)
   {
      ErrorExit("GetQueuedCompletionStatus");
   }

   return pOverlapped;
}

template <typename T>
inline T *GetCompletionAs(
   const HANDLE hIOCP,
   const DWORD timeout,
   const DWORD expectedResult = ERROR_SUCCESS)
{
   return reinterpret_cast<T *>(GetCompletion(hIOCP, timeout, expectedResult));
}

///////////////////////////////////////////////////////////////////////////////
// End of file: afd.h
///////////////////////////////////////////////////////////////////////////////
