#pragma once
///////////////////////////////////////////////////////////////////////////////
// File: shared.h
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

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>

#include <iostream>
#include <string_view>

#pragma comment(lib, "ws2_32.lib")

using std::string;

constexpr DWORD SHORT_TIME_NON_ZERO = 100;
constexpr DWORD REASONABLE_TIME = 10000;

inline string GetLastErrorMessage(
   const DWORD last_error,
   const bool stripTrailingLineFeed = true)
{
   constexpr int bufferSize = 511;

   CHAR errmsg[bufferSize + 1];

   if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      last_error,
      0,
      errmsg,
      511,
      nullptr))
   {
      // if we fail, call ourself to find out why and return that error

      const DWORD thisError = GetLastError();

      if (thisError != last_error)
      {
         return GetLastErrorMessage(thisError, stripTrailingLineFeed);
      }

      // But don't get into an infinite loop...

      return "Failed to obtain error string";
   }

   if (stripTrailingLineFeed)
   {
      const size_t length = strlen(errmsg);

      if (errmsg[length-1] == '\n')
      {
         errmsg[length-1] = 0;

         if (errmsg[length-2] == '\r')
         {
            errmsg[length-2] = 0;
         }
      }
   }

   return errmsg;
}

[[noreturn]] inline void ErrorExit(
   const std::string_view &message,
   const DWORD lastError)
{
   std:: cout << "Error: " << message << " failed: " << lastError << " - " << GetLastErrorMessage(lastError) << std::endl;

   // FAIL() returns from here and we can't do that as we rely on ErrorExit to actually exit...
   //exit(1);     // exit aborts all tests
   throw std::exception("test failed");
}

[[noreturn]] inline void ErrorExit(
   const std::string_view &message)
{
   const DWORD lastError = GetLastError();

   ErrorExit(message, lastError);
}

inline void InitialiseWinsock()
{
   WSADATA data;

   static constexpr WORD wVersionRequested = 0x202;

   if (0 != WSAStartup(wVersionRequested, &data))
   {
      ErrorExit("WSAStartup");
   }
}

inline HANDLE CreateIOCP()
{
   const HANDLE hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

   if (nullptr == hIOCP)
   {
      ErrorExit("CreateIoCompletionPort");
   }

   return hIOCP;
}

///////////////////////////////////////////////////////////////////////////////
// End of file: shared.h
///////////////////////////////////////////////////////////////////////////////
