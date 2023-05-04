
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
#include "shared/socket.h"
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

TEST(AFDExplore, TestConnectFail)
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

   ConnectNonBlocking(data.s, NonListeningPort);

   // connect is pending, it will eventually time out...

   PollData *pData = GetCompletion(handles.iocp, INFINITE);

   // the completion returns the PollData which needs to remain valid for the period of the poll

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_CONNECT_FAIL, pData->pollInfo.Handles[0].Events);
}

class AFDUnderstand : public testing::Test
{
   protected :

      AFDUnderstand()
         :  handles(CreateAfdAndIOCP()),
            data(CreateNonBlockingTCPSocket())
      {
      }

      ~AFDUnderstand()
      {
      }

      AfDWithIOCP handles;

      PollData data;
};

TEST_F(AFDUnderstand, TestConnectCancel)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   ConnectNonBlocking(data.s, NonListeningPort);

   CancelPoll(handles.afd, data);

   PollData *pData = GetCompletion(handles.iocp, INFINITE, ERROR_OPERATION_ABORTED);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(0, pData->pollInfo.Handles[0].Events);
}

TEST_F(AFDUnderstand, TestConnectCancelAll)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   ConnectNonBlocking(data.s, NonListeningPort);

   CancelAllPolling(handles.afd);

   PollData *pData = GetCompletion(handles.iocp, INFINITE, ERROR_OPERATION_ABORTED);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(0, pData->pollInfo.Handles[0].Events);
}

TEST_F(AFDUnderstand, TestConnect)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   // accepted...

   Close(s);

   // disconnected...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_DISCONNECT, pData->pollInfo.Handles[0].Events);

   ReadClientClose(data.s);
}

TEST_F(AFDUnderstand, TestConnectAndRemoteShutdownSend)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   // accepted...

   if (SOCKET_ERROR == shutdown(s, SD_SEND))
   {
      ErrorExit("shutdown");
   }

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_DISCONNECT, pData->pollInfo.Handles[0].Events);

   ReadClientClose(data.s);

   Close(s);

   // disconnected...

   // level triggered; continues to return disconnected...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_DISCONNECT, pData->pollInfo.Handles[0].Events);

   ReadClientClose(data.s);      // contiues to return 0 
}

TEST_F(AFDUnderstand, TestConnectAndRemoteShutdownRecv)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   // accepted...

   if (SOCKET_ERROR == shutdown(s, SD_RECEIVE))
   {
      ErrorExit("shutdown");
   }

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Close(s);

   // disconnected...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_DISCONNECT, pData->pollInfo.Handles[0].Events);

   ReadClientClose(data.s);
}

TEST_F(AFDUnderstand, TestConnectAndRemoteRST)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   // accepted...

   if (SOCKET_ERROR == shutdown(s, SD_RECEIVE))
   {
      ErrorExit("shutdown");
   }

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Abort(s);

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_ABORT, pData->pollInfo.Handles[0].Events);

   ReadFails(data.s, WSAECONNRESET);
}

TEST_F(AFDUnderstand, TestConnectAndRemoteSend)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   // accepted...

   const std::string testData("test");

   Write(s, testData);

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_RECEIVE, pData->pollInfo.Handles[0].Events);

   EXPECT_EQ(testData.length(), ReadAndDiscardAllAvailable(data.s));

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndRemoteSendOOB)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   // accepted...

   const std::string testData("1");    // only one byte of OOB for TCP on Windows
                                       // https://serverframework.com/asynchronousevents/2011/10/out-of-band-data-and-overlapped-io.html

   Write(s, testData, MSG_OOB);

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_RECEIVE_EXPEDITED, pData->pollInfo.Handles[0].Events);

   // No normal data...

   EXPECT_EQ(0, ReadAndDiscardAllAvailable(data.s));

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_RECEIVE_EXPEDITED, pData->pollInfo.Handles[0].Events);

   EXPECT_EQ(testData.length(), ReadAndDiscardAllAvailable(data.s, MSG_OOB));

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndRemoteSendOOBAndNormalData)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   // accepted...

   const std::string testData("1");    // only one byte of OOB for TCP on Windows
                                       // https://serverframework.com/asynchronousevents/2011/10/out-of-band-data-and-overlapped-io.html

   Write(s, testData, MSG_OOB);

   // and 1 byte of normal data...

   Write(s, testData);

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_RECEIVE | AFD_POLL_RECEIVE_EXPEDITED, pData->pollInfo.Handles[0].Events);

   // normal data...

   EXPECT_EQ(testData.length(), ReadAndDiscardAllAvailable(data.s));

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_RECEIVE_EXPEDITED, pData->pollInfo.Handles[0].Events);

   EXPECT_EQ(testData.length(), ReadAndDiscardAllAvailable(data.s, MSG_OOB));

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndLocalSend)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   constexpr int recvBufferSize = 10;

   auto listeningSocket = CreateListeningSocketWithRecvBufferSpecified(recvBufferSize);

   SetSendBuffer(data.s, 10);

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   // accepted...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   const std::string testData("This message will be sent until it can't be sent");

   const size_t length = testData.length();

   size_t totalSent = 0;

   bool done = false;

   do
   {
      const int sent = WriteUntilError(data.s, testData, WSAEWOULDBLOCK);

      totalSent += sent;

      if (sent != length)
      {
         // We have had a WSAEWOULDBLOCK result.
         // No events available as AFD_POLL_SEND was the only event set and we have filled the
         // send and recv buffer and TCP flow control has prevented us sending any more...

         EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

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
            pData = GetCompletion(handles.iocp, REASONABLE_TIME);

            EXPECT_EQ(pData, &data);

            if (AFD_POLL_SEND == pData->pollInfo.Handles[0].Events)
            {
               done = false;
            }
         }

      }
   }
   while (!done);

   EXPECT_EQ(totalSent, ReadAndDiscardAllAvailable(s));

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndLocalSend2)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   constexpr int recvBufferSize = 10;

   auto listeningSocket = CreateListeningSocketWithRecvBufferSpecified(recvBufferSize);

   SetSendBuffer(data.s, 10);

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   // accepted...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   const std::string testData("This message will be sent until it can't be sent");

   const size_t length = testData.length();

   size_t totalSent = 0;

   bool done = false;

   do
   {
      const int sent = WriteUntilError(data.s, testData, WSAEWOULDBLOCK);

      totalSent += sent;

      if (sent != length)
      {
         // We have had a WSAEWOULDBLOCK result.

         done = true;
      }
   }
   while (!done);

   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   EXPECT_EQ(totalSent, ReadAndDiscardAllAvailable(s));

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndLocalClose)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   constexpr int recvBufferSize = 10;

   auto listeningSocket = CreateListeningSocket();

   SetSendBuffer(data.s, 10);

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   // accepted...

   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEventsExceptSend));

   Close(data.s);

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_LOCAL_CLOSE, pData->pollInfo.Handles[0].Events);

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndLocalShutdownSend)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   constexpr int recvBufferSize = 10;

   auto listeningSocket = CreateListeningSocket();

   SetSendBuffer(data.s, 10);

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   // accepted...

   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEventsExceptSend));

   if (SOCKET_ERROR == shutdown(s, SD_SEND))
   {
      ErrorExit("shutdown");
   }

   pData = GetCompletion(handles.iocp, REASONABLE_TIME);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_DISCONNECT, pData->pollInfo.Handles[0].Events);

   Close(s);
}

TEST_F(AFDUnderstand, TestConnectAndLocalShutdownRecv)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   constexpr int recvBufferSize = 10;

   auto listeningSocket = CreateListeningSocket();

   SetSendBuffer(data.s, 10);

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   // accepted...

   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEventsExceptSend));

   if (SOCKET_ERROR == shutdown(s, SD_RECEIVE))
   {
      ErrorExit("shutdown");
   }

   // No notification from shutting down recv side...

   EXPECT_EQ(nullptr, GetCompletion(handles.iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT));

   Close(s);
}

TEST_F(AFDUnderstand, TestAccept)
{
   // In this test we associate the listening socket with the afd handle and poll
   // it for accepts...

   auto listeningSocket = CreateListeningSocket();

   PollData listeningData(listeningSocket.s);

   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, listeningData, AllEvents));

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &listeningData);

   EXPECT_EQ(AFD_POLL_ACCEPT, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   // accepted...

   // poll the listening socket again...
   // we would normally expect to poll the newly accepted socket as well...

   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, listeningData, AllEvents));

   Close(s);

   // disconnected...

   // No notification from closing accepted socket

   EXPECT_EQ(nullptr, GetCompletion(handles.iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT));
}

TEST_F(AFDUnderstand, TestPollIsLevelTriggered)
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

TEST_F(AFDUnderstand, TestPollCompletionReportsStateAtTimeOfPoll)
{
   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Note that at present the remote end hasn't accepted

   SOCKET s = listeningSocket.Accept();

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // This next poll will return the socket state as of NOW when we query the result
   // from the IOCP after the state change...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   // accepted...

   Close(s);

   // disconnected...

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // This next poll will report the disconnect...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND | AFD_POLL_DISCONNECT, pData->pollInfo.Handles[0].Events);

   ReadClientClose(data.s);
}

TEST_F(AFDUnderstand, TestSkipCompletionPortOnSuccess)
{
   if (!SetFileCompletionNotificationModes(handles.afd, FILE_SKIP_SET_EVENT_ON_HANDLE | FILE_SKIP_COMPLETION_PORT_ON_SUCCESS))
   {
      ErrorExit("SetFileCompletionNotificationModes");
   }

   EXPECT_EQ(false, SetupPollForSocketEvents(handles.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // poll again for this socket - no changes, socket stays wriable, polling is level triggered...
   // FILE_SKIP_COMPLETION_PORT_ON_SUCCESS means we get the poll information back immediately and
   // nothing is queued to the IOCP

   pData = PollForSocketEvents(handles.afd, data, AllEvents);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   EXPECT_EQ(nullptr, GetCompletion(handles.iocp, 0, WAIT_TIMEOUT));
}

TEST(AFDMultipleAFD, TestDuplicateName)
{
   // It seems you can open the same device name multiple times and get different handles
   // back ...
   // Using Process Explorer the two \device\Afd handles appear to be unnamed and
   // at different addresses...

   static LPCWSTR deviceName = L"\\Device\\Afd\\explore";   // Arbitrary name in the Afd namespace

   auto handles1 = CreateAfdAndIOCP(deviceName);

   auto handles2 = CreateAfdAndIOCP(deviceName);
}

TEST(AFDMultipleAFD, TestDuplicateNameAssociateSocket)
{
   // It seems you can open the same device name multiple times and get different handles
   // back ...

   static LPCWSTR deviceName = L"\\Device\\Afd\\explore";   // Arbitrary name in the Afd namespace

   auto handles1 = CreateAfdAndIOCP(deviceName);

   auto handles2 = CreateAfdAndIOCP(deviceName);

   PollData data(CreateNonBlockingTCPSocket());

   // Associate with 1st afd handle...

   EXPECT_EQ(false, SetupPollForSocketEvents(handles1.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles1.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Nothing on the other IOCP...
   EXPECT_EQ(nullptr, GetCompletion(handles2.iocp, 0, WAIT_TIMEOUT));

   // poll again for this socket - no changes, socket stays wriable, polling is level triggered...
   // but we use a different afd handle and IOCP, potentially moving this socket from one thread
   // to another...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles2.afd, data, AllEvents));

   pData = GetCompletion(handles2.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // Nothing on the other IOCP...
   EXPECT_EQ(nullptr, GetCompletion(handles1.iocp, 0, WAIT_TIMEOUT));
}

TEST(AFDMultipleAFD, TestMoveSocketBetweenAfdHandles)
{
   static LPCWSTR deviceName1 = L"\\Device\\Afd\\explore1";   // Arbitrary name in the Afd namespace

   auto handles1 = CreateAfdAndIOCP(deviceName1);

   static LPCWSTR deviceName2 = L"\\Device\\Afd\\explore2";   // Arbitrary name in the Afd namespace

   auto handles2 = CreateAfdAndIOCP(deviceName2);

   PollData data(CreateNonBlockingTCPSocket());

   // Associate with 1st afd handle...

   EXPECT_EQ(false, SetupPollForSocketEvents(handles1.afd, data, AllEvents));

   auto listeningSocket = CreateListeningSocket();

   ConnectNonBlocking(data.s, listeningSocket.port);

   // connect will complete immediately and report the socket as writable...

   PollData *pData = GetCompletion(handles1.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);

   // poll again for this socket - no changes, socket stays wriable, polling is level triggered...
   // but we use a different afd handle and IOCP, potentially moving this socket from one thread
   // to another...

   EXPECT_EQ(true, SetupPollForSocketEvents(handles2.afd, data, AllEvents));

   pData = GetCompletion(handles2.iocp, 0);

   EXPECT_EQ(pData, &data);

   EXPECT_EQ(AFD_POLL_SEND, pData->pollInfo.Handles[0].Events);
}

///////////////////////////////////////////////////////////////////////////////
// End of file: understand.cpp
///////////////////////////////////////////////////////////////////////////////
