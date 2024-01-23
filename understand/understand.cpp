
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

#include "shared/afd.h"
#include "shared/tcp_socket.h"
#include "third_party/GoogleTest/gtest.h"

int main(int argc, char **argv) {
   testing::InitGoogleTest(&argc, argv);

  InitialiseWinsock();

  return RUN_ALL_TESTS();
}

TEST(AFDExplore, TestConnectFail)
{
   static auto deviceName = L"\\Device\\Afd\\explore";   // Arbitrary name in the Afd namespace

   const auto handles = CreateAfdAndIOCP(deviceName);

   // As we'll see below, the 'PollData' (socket, status block and outbound poll info) need to stay
   // valid until the event completes...
   // This is per connection data and per operation data but we only ever have one operation
   // per connection...

   PollData data(CreateNonBlockingTCPSocket());

   // It's unlikely to complete straight away here as we haven't done anything with
   // the socket, but I expect that once the socket is connected we could get immediate
   // completions and we could, possibly, set 'FILE_SKIP_COMPLETION_PORT_ON_SUCCESS` for the
   // AFD association...

   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   // poll is pending

   ConnectNonBlocking(data.s, NonListeningPort);

   // connect is pending, it will eventually time out...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, INFINITE);

   // the completion returns the PollData which needs to remain valid for the period of the poll

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_CONNECT_FAIL, pData->pollInfo.Handles[0].Events);
}

class AFDUnderstand : public testing::Test
{
   public :

      AFDUnderstand(const AFDUnderstand &) = delete;
      AFDUnderstand(AFDUnderstand &&) = delete;

      AFDUnderstand& operator=(const AFDUnderstand &) = delete;
      AFDUnderstand& operator=(AFDUnderstand &&) = delete;

   protected :

      AFDUnderstand()
         :  handles(CreateAfdAndIOCP()),
            data(CreateNonBlockingTCPSocket())
      {
      }

      ~AFDUnderstand() override = default;

      AfDWithIOCP handles;

      PollData data;
};

TEST_F(AFDUnderstand, TestConnectCancel)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   ConnectNonBlocking(data.s, NonListeningPort);

   CancelPoll(handles.afd, data);

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, INFINITE, ERROR_OPERATION_ABORTED);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(0, pData->pollInfo.Handles[0].Events);
}

TEST_F(AFDUnderstand, TestConnectCancelAll)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   ConnectNonBlocking(data.s, NonListeningPort);

   CancelAllPolling(handles.afd);

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, INFINITE, ERROR_OPERATION_ABORTED);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(0, pData->pollInfo.Handles[0].Events);
}

TEST_F(AFDUnderstand, TestConnect)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   Close(s);

   // disconnected...

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_DISCONNECT, pData->pollInfo.Handles[0].Events);

   ReadClientClose(data.s);
}

TEST_F(AFDUnderstand, TestConnectAndRemoteShutdownSend)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   if (SOCKET_ERROR == shutdown(s, SD_SEND))
   {
      ErrorExit("shutdown");
   }

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_DISCONNECT, pData->pollInfo.Handles[0].Events);

   ReadClientClose(data.s);

   Close(s);

   // disconnected...

   // level triggered; continues to return disconnected...

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_DISCONNECT, pData->pollInfo.Handles[0].Events);

   ReadClientClose(data.s);      // continues to return 0 
}

TEST_F(AFDUnderstand, TestConnectAndRemoteShutdownRecv)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   if (SOCKET_ERROR == shutdown(s, SD_RECEIVE))
   {
      ErrorExit("shutdown");
   }

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Close(s);

   // disconnected...

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_DISCONNECT, pData->pollInfo.Handles[0].Events);

   ReadClientClose(data.s);
}

TEST_F(AFDUnderstand, TestConnectAndRemoteRST)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   if (SOCKET_ERROR == shutdown(s, SD_RECEIVE))
   {
      ErrorExit("shutdown");
   }

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Abort(s);

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_ABORT, pData->pollInfo.Handles[0].Events);

   ReadFails(data.s, WSAECONNRESET);
}

TEST_F(AFDUnderstand, TestConnectAndRemoteSend)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   const std::string testData("test");

   Write(s, testData);

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_RECEIVE, pData->pollInfo.Handles[0].Events);

   EXPECT_EQ(testData.length(), ReadAndDiscardAllAvailable(data.s));

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndRemoteSendOOB)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   const std::string testData("1");    // only one byte of OOB for TCP on Windows
                                       // https://serverframework.com/asynchronousevents/2011/10/out-of-band-data-and-overlapped-io.html

   Write(s, testData, MSG_OOB);

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_RECEIVE_EXPEDITED, pData->pollInfo.Handles[0].Events);

   // No normal data...

   EXPECT_EQ(0, ReadAndDiscardAllAvailable(data.s));

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_RECEIVE_EXPEDITED, pData->pollInfo.Handles[0].Events);

   EXPECT_EQ(testData.length(), ReadAndDiscardAllAvailable(data.s, MSG_OOB));

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndRemoteSendOOBAndNormalData)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   const std::string testData("1");    // only one byte of OOB for TCP on Windows
                                       // https://serverframework.com/asynchronousevents/2011/10/out-of-band-data-and-overlapped-io.html

   Write(s, testData, MSG_OOB);

   // and 1 byte of normal data...

   Write(s, testData);

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_RECEIVE | AFD_POLL_RECEIVE_EXPEDITED, pData->pollInfo.Handles[0].Events);

   // normal data...

   EXPECT_EQ(testData.length(), ReadAndDiscardAllAvailable(data.s));

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_RECEIVE_EXPEDITED, pData->pollInfo.Handles[0].Events);

   EXPECT_EQ(testData.length(), ReadAndDiscardAllAvailable(data.s, MSG_OOB));

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndLocalSend)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   constexpr int recvBufferSize = 10;

   const auto listeningSocket = CreateListeningSocketWithRecvBufferSpecified(recvBufferSize);

   SetSendBuffer(data.s, 10);

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   const std::string testData("This message will be sent until it can't be sent");

   const size_t length = testData.length();

   size_t totalSent = 0;

   bool done = false;

   do
   {
      const size_t sent = WriteUntilError(data.s, testData, WSAEWOULDBLOCK);

      totalSent += sent;

      if (sent != length)
      {
         // We have had a WSAEWOULDBLOCK result.
         // No events available as AFD_POLL_SEND was the only event set and we have filled the
         // send and recv buffer and TCP flow control has prevented us sending any more...

         ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

         done = true;
      }
      else
      {
         // write was OK, sent the full amount, this check is probably excessive for production code
         // but interesting to see here in this test...

         done = true;
         // We MAY be able to still send without getting a WSAEWOULDBLOCK result..

         if (SetupPollForSocketEvents(handles.afd, data, AllEvents))
         {
            pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

            ASSERT_EQ(pData, &data);

            if (AFD_POLL_SEND == pData->pollInfo.Handles[0].Events)
            {
               done = false;
            }
         }

      }
   }
   while (!done);

   EXPECT_EQ(totalSent, ReadAndDiscardAllAvailable(s));

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndLocalSend2)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   constexpr int recvBufferSize = 10;

   const auto listeningSocket = CreateListeningSocketWithRecvBufferSpecified(recvBufferSize);

   SetSendBuffer(data.s, 10);

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   const std::string testData("This message will be sent until it can't be sent");

   const size_t length = testData.length();

   size_t totalSent = 0;

   bool done = false;

   do
   {
      const size_t sent = WriteUntilError(data.s, testData, WSAEWOULDBLOCK);

      totalSent += sent;

      if (sent != length)
      {
         // We have had a WSAEWOULDBLOCK result.

         done = true;
      }
   }
   while (!done);

   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   EXPECT_EQ(totalSent, ReadAndDiscardAllAvailable(s));

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndLocalClose)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   SetSendBuffer(data.s, 10);

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEventsExceptSend));

   Close(data.s);

   pData = GetCompletionAs<PollData>(handles.iocp, REASONABLE_TIME);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_LOCAL_CLOSE, pData->pollInfo.Handles[0].Events);

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndLocalShutdownSend)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   SetSendBuffer(data.s, 10);

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEventsExceptSend));

   if (SOCKET_ERROR == shutdown(data.s, SD_SEND))
   {
      ErrorExit("shutdown");
   }

   // no notifications for local operations

   ASSERT_EQ(nullptr, GetCompletionAs<PollData>(handles.iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT));

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndLocalShutdownRecv)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   SetSendBuffer(data.s, 10);

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEventsExceptSend));

   if (SOCKET_ERROR == shutdown(data.s, SD_RECEIVE))
   {
      ErrorExit("shutdown");
   }

   // No notification from shutting down recv side...

   ASSERT_EQ(nullptr, GetCompletionAs<PollData>(handles.iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT));

   Close(s);
}

TEST_F(AFDUnderstand, TestAccept)
{
   // In this test we associate the listening socket with the afd handle and poll
   // it for accepts...

   const auto listeningSocket = CreateListeningSocket();

   PollData listeningData(listeningSocket.s);

   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, listeningData, AllEvents));

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &listeningData);

   EXPECT_EQ(AFD_POLL_ACCEPT, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   // poll the listening socket again...
   // we would normally expect to poll the newly accepted socket as well...

   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, listeningData, AllEvents));

   Close(s);

   // disconnected...

   // No notification from closing accepted socket

   ASSERT_EQ(nullptr, GetCompletionAs<PollData>(handles.iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT));
}

TEST_F(AFDUnderstand, TestPollIsLevelTriggered)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // poll again for this socket - no changes, socket stays writable, polling is level triggered...

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // poll again for this socket - no changes, socket stays writable, polling is level triggered...

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);
}

TEST_F(AFDUnderstand, TestPollCompletionReportsStateAtTimeOfPoll)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // This next poll will return the socket state as of NOW when we query the result
   // from the IOCP after the state change...

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   // accepted...

   Close(s);

   // disconnected...

   pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // This next poll will report the disconnect...

   ASSERT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_DISCONNECT, pData->pollInfo.Handles[0].Events);

   ReadClientClose(data.s);
}

TEST_F(AFDUnderstand, TestSkipCompletionPortOnSuccess)
{
   if (!SetFileCompletionNotificationModes(handles.afd, FILE_SKIP_SET_EVENT_ON_HANDLE | FILE_SKIP_COMPLETION_PORT_ON_SUCCESS))
   {
      ErrorExit("SetFileCompletionNotificationModes");
   }

   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // poll again for this socket - no changes, socket stays writable, polling is level triggered...
   // FILE_SKIP_COMPLETION_PORT_ON_SUCCESS means we get the poll information back immediately and
   // nothing is queued to the IOCP

   pData = PollForSocketEvents(handles.afd, data, AllEvents);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   ASSERT_EQ(nullptr, GetCompletionAs<PollData>(handles.iocp, 0, WAIT_TIMEOUT));
}

TEST_F(AFDUnderstand, TestPollOnceGivesOneCompletion)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   ConnectNonBlocking(data.s, NonListeningPort);

   CancelPoll(handles.afd, data);

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, SHORT_TIME_NON_ZERO, ERROR_OPERATION_ABORTED);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(0, pData->pollInfo.Handles[0].Events);

   ASSERT_EQ(nullptr, GetCompletionAs<PollData>(handles.iocp, 0, WAIT_TIMEOUT));

   ASSERT_EQ(nullptr, GetCompletionAs<PollData>(handles.iocp, 0, WAIT_TIMEOUT));
}

TEST_F(AFDUnderstand, TestPollTwiceSameDataGivesTwoCompletions)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   ConnectNonBlocking(data.s, NonListeningPort);

   CancelPoll(handles.afd, data);

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, SHORT_TIME_NON_ZERO, ERROR_OPERATION_ABORTED);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(0, pData->pollInfo.Handles[0].Events);

   pData = GetCompletionAs<PollData>(handles.iocp, SHORT_TIME_NON_ZERO, ERROR_OPERATION_ABORTED);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(0, pData->pollInfo.Handles[0].Events);

   ASSERT_EQ(nullptr, GetCompletionAs<PollData>(handles.iocp, 0, WAIT_TIMEOUT));
}

TEST_F(AFDUnderstand, TestPollTwiceSameDataDifferentEvents)
{
   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AFD_POLL_SEND));

   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AFD_POLL_ABORT));

   const auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // second poll is for different events...

   ASSERT_EQ(nullptr, GetCompletionAs<PollData>(handles.iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT));

   const SOCKET s = listeningSocket.Accept();

   Abort(s);

   // this second poll never completes, looks like the polling is set up per socket
   // rather than per call to poll? Either way this is all way 'off-piste' and we'd
   // never want or need to do this...

   ASSERT_EQ(nullptr, GetCompletionAs<PollData>(handles.iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT));
}

TEST_F(AFDUnderstand, TestPollForEventsWithDifferentSizedOutputStructureToInputStructureInputSmallerThanOutput)
{
   // pollInfoOut is large enough for 10 handle entries in the array...

   BYTE pollInfoOut[sizeof AFD_POLL_INFO + (9 * sizeof AFD_POLL_HANDLE_INFO)];

   ASSERT_EQ(false, SetupPollForSocketEvents(handles.afd, data.statusBlock, data.s, &pollInfoOut, sizeof(pollInfoOut), &data, AllEvents));

   ConnectNonBlocking(data.s, NonListeningPort);

   CancelPoll(handles.afd, data);

   PollData *pData = GetCompletionAs<PollData>(handles.iocp, SHORT_TIME_NON_ZERO, ERROR_OPERATION_ABORTED);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(0, pData->pollInfo.Handles[0].Events);

   ASSERT_EQ(nullptr, GetCompletionAs<PollData>(handles.iocp, 0, WAIT_TIMEOUT));

   ASSERT_EQ(nullptr, GetCompletionAs<PollData>(handles.iocp, 0, WAIT_TIMEOUT));
}

TEST_F(AFDUnderstand, TestPollForEventsWithDifferentSizedOutputStructureToInputStructureOutputSmallerThanInput)
{
   // pollInfoIn is large enough for 10 handle entries in the array...

   BYTE pollInfoIn[sizeof AFD_POLL_INFO + (9 * sizeof AFD_POLL_HANDLE_INFO)];

   AFD_POLL_INFO pollInfoOut {};

   EXPECT_THROW(SetupPollForSocketEvents(handles.afd, &pollInfoIn, sizeof(pollInfoIn), data.statusBlock, data.s, &pollInfoOut, sizeof(pollInfoOut), &data, AllEvents), std::exception);
}

TEST(AFDMultipleAFD, TestDuplicateName)
{
   // It seems you can open the same device name multiple times and get different handles
   // back ...
   // Using Process Explorer the two \device\Afd handles appear to be unnamed and
   // at different addresses...

   static auto deviceName = L"\\Device\\Afd\\explore";   // Arbitrary name in the Afd namespace

   const auto handles1 = CreateAfdAndIOCP(deviceName);

   const auto handles2 = CreateAfdAndIOCP(deviceName);

   EXPECT_NE(handles1.afd, handles2.afd);
}

TEST(AFDMultipleAFD, TestDuplicateNameAssociateSocket)
{
   // It seems you can open the same device name multiple times and get different handles
   // back ...

   static auto deviceName = L"\\Device\\Afd\\explore";   // Arbitrary name in the Afd namespace

   const auto handles1 = CreateAfdAndIOCP(deviceName);

   const auto handles2 = CreateAfdAndIOCP(deviceName);

   PollData data(CreateNonBlockingTCPSocket());

   // Associate with 1st afd handle...

   ASSERT_EQ(false, SetupPollForSocketEvents(handles1.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles1.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Nothing on the other IOCP...
   ASSERT_EQ(nullptr, GetCompletionAs<PollData>(handles2.iocp, 0, WAIT_TIMEOUT));

   // poll again for this socket - no changes, socket stays writable, polling is level triggered...
   // but we use a different afd handle and IOCP, potentially moving this socket from one thread
   // to another...

   ASSERT_EQ(true, SetupPollForSocketEvents(handles2.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles2.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Nothing on the other IOCP...
   ASSERT_EQ(nullptr, GetCompletionAs<PollData>(handles1.iocp, 0, WAIT_TIMEOUT));
}

TEST(AFDMultipleAFD, TestMoveSocketBetweenAfdHandles)
{
   static auto deviceName1 = L"\\Device\\Afd\\explore1";   // Arbitrary name in the Afd namespace

   const auto handles1 = CreateAfdAndIOCP(deviceName1);

   static auto deviceName2 = L"\\Device\\Afd\\explore2";   // Arbitrary name in the Afd namespace

   const auto handles2 = CreateAfdAndIOCP(deviceName2);

   PollData data(CreateNonBlockingTCPSocket());

   // Associate with 1st afd handle...

   ASSERT_EQ(false, SetupPollForSocketEvents(handles1.afd, data, AllEvents));

   const auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletionAs<PollData>(handles1.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // poll again for this socket - no changes, socket stays writable, polling is level triggered...
   // but we use a different afd handle and IOCP, potentially moving this socket from one thread
   // to another...

   ASSERT_EQ(true, SetupPollForSocketEvents(handles2.afd, data, AllEvents));

   pData = GetCompletionAs<PollData>(handles2.iocp, 0);

   ASSERT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);
}

///////////////////////////////////////////////////////////////////////////////
// End of file: understand.cpp
///////////////////////////////////////////////////////////////////////////////
