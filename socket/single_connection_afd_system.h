#pragma once
///////////////////////////////////////////////////////////////////////////////
// File: single_connection_afd_system.h
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

#include "shared/afd.h"

#include "afd_system.h"

class afd_events;

class single_connection_afd_system : public afd_system, afd_system_events
{
   public :

      explicit single_connection_afd_system(
         HANDLE hAfd,
         int num_slots = 1);

      void associate_socket(
         ULONG slot,
         SOCKET s,
         afd_events &events) override;

      void disassociate_socket(
         ULONG slot) override;

      bool poll(
         ULONG slot,
         ULONG events) override;

      void handle_events() override;

   private :

      HANDLE hAfd;
      const ULONG num_slots;
      const ULONG poll_info_size;
      AFD_POLL_INFO *pPollInfoIn;
      AFD_POLL_INFO *pPollInfoOut;
      IO_STATUS_BLOCK statusBlock;

      afd_events **ppEvents;
};

///////////////////////////////////////////////////////////////////////////////
// End of file: single_connection_afd_system.h
///////////////////////////////////////////////////////////////////////////////
