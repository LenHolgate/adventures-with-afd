
///////////////////////////////////////////////////////////////////////////////
// File: understand-udp.cpp
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

#include "shared/afd.h"
#include "shared/udp_socket.h"
#include "third_party/GoogleTest/gtest.h"

#include <SDKDDKVer.h>

#include <winternl.h>

#include "shared/udp_socket.h"

#pragma comment(lib, "ntdll.lib")


class AFDUnderstandUDP : public testing::Test
{
   public :

      AFDUnderstandUDP(const AFDUnderstandUDP &) = delete;
      AFDUnderstandUDP(AFDUnderstandUDP &&) = delete;

      AFDUnderstandUDP& operator=(const AFDUnderstandUDP &) = delete;
      AFDUnderstandUDP& operator=(AFDUnderstandUDP &&) = delete;

   protected :

      AFDUnderstandUDP()
         :  handles(CreateAfdAndIOCP()),
            data(CreateNonBlockingUDPSocket())
      {
      }

      ~AFDUnderstandUDP() override = default;

      AfDWithIOCP handles;

      PollData data;
};

TEST_F(AFDUnderstandUDP, TestCreate)
{
   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);
}

TEST_F(AFDUnderstandUDP, TestBind)
{
   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   const USHORT port = Bind(data.s);

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);
}

TEST_F(AFDUnderstandUDP, TestRecv)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEventsExceptSend));

   const USHORT port = Bind(data.s);

   SOCKET sendSocket = CreateUDPSocket();

   const std::string testData("test");

   SendTo(sendSocket, port, testData);

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_RECEIVE, pData->pollInfo.Handles[0].Events);
}

///////////////////////////////////////////////////////////////////////////////
// End of file: understand-udp.cpp
///////////////////////////////////////////////////////////////////////////////
