///////////////////////////////////////////////////////////////////////////////
// File: afd_system.cpp
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

#include "afd_handle.h"
#include "afd_system.h"
#include "afd_events.h"

#include "../shared/afd.h"

static ULONG validate_slots(
   const ULONG slots)
{
   if (slots < 1)
   {
      throw std::exception("slots must be at least 1");
   }

   return slots;
}

afd_system::afd_system(
   HANDLE hAfd,
   const int num_slots)
   : hAfd(hAfd),
     num_slots(validate_slots(num_slots)),
     poll_info_size(sizeof AFD_POLL_INFO + ((num_slots - 1) * sizeof AFD_POLL_HANDLE_INFO)),
     pPollInfoIn(reinterpret_cast<AFD_POLL_INFO *>(new BYTE[poll_info_size])),
     pPollInfoOut(reinterpret_cast<AFD_POLL_INFO *>(new BYTE[poll_info_size])),
     ppEvents(new afd_events*[num_slots])
{
   memset(pPollInfoIn, 0, poll_info_size);
   memset(pPollInfoOut, 0, poll_info_size);
   memset(ppEvents, 0, sizeof(afd_events*) * num_slots);

   pPollInfoIn->Exclusive = FALSE;
   pPollInfoIn->NumberOfHandles = 1;                 // based on max slots used
   pPollInfoIn->Timeout.QuadPart = INT64_MAX;
}

void afd_system::associate_socket(
   ULONG slot,
   SOCKET s,
   afd_events &events)
{
   if (slot > num_slots)
   {
      throw std::exception("invalid slot");
   }

   // active sockets++
   // can it be non-contiguous?
   // does it benefit from being contiguous?

   pPollInfoIn->Handles[slot].Handle = reinterpret_cast<HANDLE>(GetBaseSocket(s));
   // also store events in an corresponding array...
   // we use events to callback to the socket

   ppEvents[slot] = &events;

   pPollInfoIn->NumberOfHandles = slot + 1;
}

void afd_system::disassociate_socket(
   ULONG slot)
{
   if (slot > num_slots)
   {
      throw std::exception("invalid slot");
   }

   //active sockets--;

   pPollInfoIn->Handles[slot].Handle = 0;

   ppEvents[slot] = nullptr;
}

bool afd_system::poll(
   ULONG slot,
   ULONG events)
{
   if (slot > num_slots)
   {
      throw std::exception("invalid slot");
   }

   // lock...
   // index into pollIn, set events...

   pPollInfoIn->Handles[slot].Status = 0;
   pPollInfoIn->Handles[slot].Events = events;

   // whenever we poll build a pollInfoIn structure that only contains our active handles (ones with non-zero event)
   // need to be able to map from handle to this structure when the poll return occurs...

   // can use one structure...
   // can dynamically size it depending on how many sockets we have?

   // zero poll out

   memset(pPollInfoOut, 0, poll_info_size);

   memset(&statusBlock, 0, sizeof statusBlock);

   return SetupPollForSocketEventsX(
      hAfd,
      pPollInfoIn,
      poll_info_size,
      statusBlock,
      pPollInfoOut,
      poll_info_size,
      this);
}

void afd_system::handle_events()
{
   // lock
   // iterate the active handles
   // process events...

   for (ULONG i = 0; i < pPollInfoOut->NumberOfHandles; ++i)
   {
      if (pPollInfoOut->Handles[i].Status || pPollInfoOut->Handles[i].Events)
      {
         // need to map pPollInfoOut->Handles[i].Handle to our index

         // index and i will be different

         const ULONG index = i;

         if (ppEvents[index])
         {
            pPollInfoIn->Handles[index].Events = ppEvents[index]->handle_events(pPollInfoOut->Handles[i].Events, RtlNtStatusToDosError(pPollInfoOut->Handles[i].Status));
         }
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
// End of file: afd_system.cpp
///////////////////////////////////////////////////////////////////////////////
