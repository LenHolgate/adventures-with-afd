///////////////////////////////////////////////////////////////////////////////
// File: test.cpp
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
#include "shared/tcp_socket.h"

#include "third_party/GoogleTest/gtest.h"
#include "third_party/GoogleTest/gmock.h"

#include <SDKDDKVer.h>

#include <winternl.h>

#include "tcp_socket.h"

#pragma comment(lib, "ntdll.lib")

int main(int argc, char **argv) {
   testing::InitGoogleTest(&argc, argv);

  InitialiseWinsock();

  return RUN_ALL_TESTS();
}

class mock_tcp_socket_callbacks : public tcp_socket_callbacks
{
   public :

   MOCK_METHOD(void, on_connected, (tcp_socket &), (override));
   MOCK_METHOD(void, on_connection_failed, (tcp_socket &, DWORD), (override));
   MOCK_METHOD(void, on_readable, (tcp_socket &), (override));
   MOCK_METHOD(void, on_readable_oob, (tcp_socket &), (override));
   MOCK_METHOD(void, on_writable, (tcp_socket &), (override));
   MOCK_METHOD(void, on_client_close, (tcp_socket &), (override));
   MOCK_METHOD(void, on_connection_reset, (tcp_socket &), (override));
   MOCK_METHOD(void, on_disconnected, (tcp_socket &), (override));
   MOCK_METHOD(void, on_connection_complete, (), (override));
};

class mock_tcp_socket_callbacks_ex : public mock_tcp_socket_callbacks
{
   public:

      using On_readable_callback = std::function<void(
         tcp_socket &s)>;

      mock_tcp_socket_callbacks_ex(
         const On_readable_callback &on_readable_callback)
         : on_readable_callback(on_readable_callback)
      {

      }

      void on_readable(
         tcp_socket &s) override
      {
         on_readable_callback(s);

         mock_tcp_socket_callbacks::on_readable(s);
      }

      const On_readable_callback on_readable_callback;
};

TEST(AFDSocket, TestConstruct)
{
   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket(iocp, callbacks);
}

TEST(AFDSocket, TestConnectFail)
{
   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket(iocp, callbacks);

   sockaddr_in address {};

   /* Attempt to connect to an address that we won't be able to connect to. */
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(1);

   socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, INFINITE);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connection_failed(::testing::_, ::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);
}

TEST(AFDSocket, TestConnect)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket(iocp, callbacks);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);
}

TEST(AFDSocket, TestConnectAndSend)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket(iocp, callbacks);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   static const BYTE data[] = { 1, 2, 3, 4 };

   socket.write(data, sizeof data);
}

TEST(AFDSocket, TestConnectAndRecvReadInOnReadable)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   const std::string testData("test");

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   // In this test we issue the reads inside the on_readable callback. This
   // results in the read that returns 0 automatically adjusting the poll
   // to add readable back into the events we're interested in

   mock_tcp_socket_callbacks_ex callbacks([&](tcp_socket &s){
      DWORD available = s.read(buffer, buffer_length);

      EXPECT_EQ(available, testData.length());

      EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

      available = s.read(buffer, buffer_length);

      EXPECT_EQ(available, 0);
      });

   tcp_socket socket(iocp, callbacks);

   sockaddr_in address{};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   EXPECT_NE(s, INVALID_SOCKET);

   // accepted...

   for (auto i = 0; i < 5; ++i)
   {
      Write(s, testData);

      pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

      EXPECT_EQ(pSocket, &socket);

      EXPECT_CALL(callbacks, on_readable(::testing::_)).Times(1);

      EXPECT_EQ(pSocket->handle_events(), true);

      // read is dealt with in on_readable and the poll is reset to include
      // the readable event...
   }
}
TEST(AFDSocket, TestConnectAndRecv)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket(iocp, callbacks);

   sockaddr_in address{};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   EXPECT_NE(s, INVALID_SOCKET);

   // accepted...

   const std::string testData("test");

   for (auto i = 0; i < 5; ++i)
   {
      Write(s, testData);

      pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

      EXPECT_EQ(pSocket, &socket);

      EXPECT_CALL(callbacks, on_readable(::testing::_)).Times(1);

      if (!pSocket->handle_events())
      {
         pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

         EXPECT_EQ(pSocket, &socket);

         EXPECT_EQ(pSocket->handle_events(), true);
      }

      available = socket.read(buffer, buffer_length);

      EXPECT_EQ(available, testData.length());

      EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

      available = socket.read(buffer, buffer_length);

      EXPECT_EQ(available, 0);
   }
}

TEST(AFDSocket, TestConnectAndLocalClose)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket(iocp, callbacks);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   socket.close();

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_disconnected(::testing::_)).Times(1);
   EXPECT_CALL(callbacks, on_connection_complete()).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);
}

TEST(AFDSocket, TestConnectAndLocalShutdownSend)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket(iocp, callbacks);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   socket.shutdown(tcp_socket::shutdown_how::send);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndLocalShutdownRecv)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket(iocp, callbacks);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   const SOCKET s = listeningSocket.Accept();

   EXPECT_NE(s, INVALID_SOCKET);

   socket.shutdown(tcp_socket::shutdown_how::receive);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndLocalShutdownBoth)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket(iocp, callbacks);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   const SOCKET s = listeningSocket.Accept();

   EXPECT_NE(s, INVALID_SOCKET);

   socket.shutdown(tcp_socket::shutdown_how::both);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRemoteClose)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket(iocp, callbacks);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   const SOCKET s = listeningSocket.Accept();

   EXPECT_NE(s, INVALID_SOCKET);

   Close(s);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_client_close(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRemoteReset)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket(iocp, callbacks);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   const SOCKET s = listeningSocket.Accept();

   EXPECT_NE(s, INVALID_SOCKET);

   EXPECT_CALL(callbacks, on_connection_reset(::testing::_)).Times(1);

   Abort(s);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_EQ(pSocket->handle_events(), true);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRemoteShutdownSend)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket(iocp, callbacks);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   const SOCKET s = listeningSocket.Accept();

   EXPECT_NE(s, INVALID_SOCKET);

   shutdown(s, SD_SEND);

   EXPECT_CALL(callbacks, on_client_close(::testing::_)).Times(1);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_EQ(pSocket->handle_events(), true);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRemoteShutdownRecv)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket(iocp, callbacks);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   const SOCKET s = listeningSocket.Accept();

   EXPECT_NE(s, INVALID_SOCKET);

   shutdown(s, SD_RECEIVE);

   // we only spot the fact that the peer is not longer able to read if we try
   // and write to it

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);

   static const BYTE data[] = { 1, 2, 3, 4 };

   socket.write(data, sizeof data);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket);

   EXPECT_CALL(callbacks, on_connection_reset(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRecvMultipleSockets)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket socket1(iocp, callbacks);

   tcp_socket socket2(iocp, callbacks);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket1.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket1);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   // Note that at present the remote end hasn't accepted

   const SOCKET s1 = listeningSocket.Accept();

   EXPECT_NE(s1, INVALID_SOCKET);

   // accepted...

   socket2.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket2);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   const SOCKET s2 = listeningSocket.Accept();

   EXPECT_NE(s2, INVALID_SOCKET);

   const std::string testData("test");

   Write(s1, testData);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket1);

   EXPECT_CALL(callbacks, on_readable(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, testData.length());

   EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

   available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket1);

   EXPECT_EQ(pSocket->handle_events(), false);

   available = socket2.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   Write(s2, testData);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket2);

   EXPECT_CALL(callbacks, on_readable(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   available = socket2.read(buffer, buffer_length);

   EXPECT_EQ(available, testData.length());

   EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

   available = socket2.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &socket2);

   EXPECT_EQ(pSocket->handle_events(), false);

   available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRecvMultipleSocketsGetQueuedCompletionStatusExReadInOnReadable)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   const std::string testData("test");

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   // In this test we issue the reads inside the on_readable callback. This
   // results in the read that returns 0 automatically adjusting the poll
   // to add readable back into the events we're interested in

   mock_tcp_socket_callbacks_ex callbacks1([&](tcp_socket &s){
      DWORD available = s.read(buffer, buffer_length);

      EXPECT_EQ(available, testData.length());

      EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

      available = s.read(buffer, buffer_length);

      EXPECT_EQ(available, 0);
      });

   mock_tcp_socket_callbacks_ex callbacks2([&](tcp_socket &s){
      DWORD available = s.read(buffer, buffer_length);

      EXPECT_EQ(available, testData.length());

      EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

      available = s.read(buffer, buffer_length);

      EXPECT_EQ(available, 0);
      });

   tcp_socket socket1(iocp, callbacks1);

   tcp_socket socket2(iocp, callbacks2);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket1.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   socket2.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   std::vector<afd_events *> sockets;

   sockets.resize(3);

   DWORD numEvents = GetCompletionKeysAs(iocp, SHORT_TIME_NON_ZERO, sockets);

   EXPECT_EQ(numEvents, 2);
   EXPECT_EQ(sockets.size(), 2);
   EXPECT_EQ(sockets[0], &socket1);
   EXPECT_EQ(sockets[1], &socket2);

   EXPECT_CALL(callbacks1, on_connected(::testing::_)).Times(1);
   EXPECT_CALL(callbacks2, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(sockets[0]->handle_events(), true);
   EXPECT_EQ(sockets[1]->handle_events(), true);

   int available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   available = socket2.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   // Accept the connections on our listening socket

   const SOCKET s1 = listeningSocket.Accept();

   EXPECT_NE(s1, INVALID_SOCKET);

   const SOCKET s2 = listeningSocket.Accept();

   EXPECT_NE(s2, INVALID_SOCKET);

   // accepted...

   sockets.resize(3);

   numEvents = GetCompletionKeysAs(iocp, SHORT_TIME_NON_ZERO, sockets);

   EXPECT_EQ(numEvents, 0);
   EXPECT_EQ(sockets.size(), 0);

   Write(s1, testData);

   sockets.resize(3);

   numEvents = GetCompletionKeysAs(iocp, SHORT_TIME_NON_ZERO, sockets);

   EXPECT_EQ(numEvents, 1);
   EXPECT_EQ(sockets.size(), 1);
   EXPECT_EQ(sockets[0], &socket1);

   EXPECT_CALL(callbacks1, on_readable(::testing::_)).Times(1);

   EXPECT_EQ(sockets[0]->handle_events(), true);

   Write(s2, testData);

   sockets.resize(3);

   numEvents = GetCompletionKeysAs(iocp, SHORT_TIME_NON_ZERO, sockets);

   EXPECT_EQ(numEvents, 1);
   EXPECT_EQ(sockets.size(), 1);
   EXPECT_EQ(sockets[0], &socket2);

   EXPECT_CALL(callbacks2, on_readable(::testing::_)).Times(1);

   EXPECT_EQ(sockets[0]->handle_events(), true);

   Write(s1, testData);
   Write(s2, testData);

   sockets.resize(3);

   numEvents = GetCompletionKeysAs(iocp, SHORT_TIME_NON_ZERO, sockets);

   EXPECT_EQ(numEvents, 2);
   EXPECT_EQ(sockets.size(), 2);
   EXPECT_EQ(sockets[0], &socket1);
   EXPECT_EQ(sockets[1], &socket2);

   EXPECT_CALL(callbacks1, on_readable(::testing::_)).Times(1);
   EXPECT_CALL(callbacks2, on_readable(::testing::_)).Times(1);

   EXPECT_EQ(sockets[0]->handle_events(), true);
   EXPECT_EQ(sockets[1]->handle_events(), true);
}
TEST(AFDSocket, TestConnectAndRecvMultipleSocketsGetQueuedCompletionStatusEx)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks1;

   tcp_socket socket1(iocp, callbacks1);

   mock_tcp_socket_callbacks callbacks2;

   tcp_socket socket2(iocp, callbacks2);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   socket1.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   socket2.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   std::vector<afd_events *> sockets;

   sockets.resize(3);

   DWORD numEvents = GetCompletionKeysAs(iocp, SHORT_TIME_NON_ZERO, sockets);

   EXPECT_EQ(numEvents, 2);
   EXPECT_EQ(sockets.size(), 2);
   EXPECT_EQ(sockets[0], &socket1);
   EXPECT_EQ(sockets[1], &socket2);

   EXPECT_CALL(callbacks1, on_connected(::testing::_)).Times(1);
   EXPECT_CALL(callbacks2, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(sockets[0]->handle_events(), true);
   EXPECT_EQ(sockets[1]->handle_events(), true);

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   available = socket2.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   // Accept the connections on our listening socket

   const SOCKET s1 = listeningSocket.Accept();

   EXPECT_NE(s1, INVALID_SOCKET);

   const SOCKET s2 = listeningSocket.Accept();

   EXPECT_NE(s2, INVALID_SOCKET);

   // accepted...

   sockets.resize(3);

   numEvents = GetCompletionKeysAs(iocp, SHORT_TIME_NON_ZERO, sockets);

   EXPECT_EQ(numEvents, 0);
   EXPECT_EQ(sockets.size(), 0);

   const std::string testData("test");

   Write(s1, testData);

   sockets.resize(3);

   numEvents = GetCompletionKeysAs(iocp, SHORT_TIME_NON_ZERO, sockets);

   EXPECT_EQ(numEvents, 1);
   EXPECT_EQ(sockets.size(), 1);
   EXPECT_EQ(sockets[0], &socket1);

   EXPECT_CALL(callbacks1, on_readable(::testing::_)).Times(1);

   EXPECT_EQ(sockets[0]->handle_events(), true);

   available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, testData.length());

   EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

   available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   sockets.resize(3);

   numEvents = GetCompletionKeysAs(iocp, SHORT_TIME_NON_ZERO, sockets);

   EXPECT_EQ(numEvents, 1);
   EXPECT_EQ(sockets.size(), 1);
   EXPECT_EQ(sockets[0], &socket1);
   EXPECT_EQ(sockets[0]->handle_events(), false);

   Write(s2, testData);

   sockets.resize(3);

   numEvents = GetCompletionKeysAs(iocp, SHORT_TIME_NON_ZERO, sockets);

   EXPECT_EQ(numEvents, 1);
   EXPECT_EQ(sockets.size(), 1);
   EXPECT_EQ(sockets[0], &socket2);

   EXPECT_CALL(callbacks2, on_readable(::testing::_)).Times(1);

   EXPECT_EQ(sockets[0]->handle_events(), true);

   available = socket2.read(buffer, buffer_length);

   EXPECT_EQ(available, testData.length());

   EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

   available = socket2.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   sockets.resize(3);

   numEvents = GetCompletionKeysAs(iocp, SHORT_TIME_NON_ZERO, sockets);

   EXPECT_EQ(numEvents, 1);
   EXPECT_EQ(sockets.size(), 1);
   EXPECT_EQ(sockets[0], &socket2);
   EXPECT_EQ(sockets[0]->handle_events(), false);

   Write(s1, testData);
   Write(s2, testData);

   sockets.resize(3);

   numEvents = GetCompletionKeysAs(iocp, SHORT_TIME_NON_ZERO, sockets);

   EXPECT_EQ(numEvents, 2);
   EXPECT_EQ(sockets.size(), 2);
   EXPECT_EQ(sockets[0], &socket1);
   EXPECT_EQ(sockets[1], &socket2);

   EXPECT_CALL(callbacks1, on_readable(::testing::_)).Times(1);
   EXPECT_CALL(callbacks2, on_readable(::testing::_)).Times(1);

   EXPECT_EQ(sockets[0]->handle_events(), true);
   EXPECT_EQ(sockets[1]->handle_events(), true);

   available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, testData.length());

   EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

   available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   available = socket2.read(buffer, buffer_length);

   EXPECT_EQ(available, testData.length());

   EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

   available = socket2.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   sockets.resize(3);

   numEvents = GetCompletionKeysAs(iocp, SHORT_TIME_NON_ZERO, sockets);

   EXPECT_EQ(numEvents, 2);
   EXPECT_EQ(sockets.size(), 2);
   EXPECT_EQ(sockets[0], &socket1);
   EXPECT_EQ(sockets[1], &socket2);
   EXPECT_EQ(sockets[0]->handle_events(), false);
   EXPECT_EQ(sockets[1]->handle_events(), false);
}

TEST(AFDSocket, TestAcceptedSocket)
{
   const auto listeningSocket = CreateListeningSocket();

   const auto iocp = CreateIOCP();

   mock_tcp_socket_callbacks callbacks;

   tcp_socket connected_socket(iocp, callbacks);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(listeningSocket.port);

   connected_socket.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   auto *pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &connected_socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = connected_socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   // Note that at present the remote end hasn't accepted

   // accepted...

   tcp_socket accepted_socket(
      iocp,
      listeningSocket.Accept(),
      callbacks);

   accepted_socket.accepted();

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &accepted_socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   const std::string testData("test");

   accepted_socket.write(reinterpret_cast<const BYTE *>(testData.c_str()), static_cast<int>(testData.length()));

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_readable(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), true);

   available = connected_socket.read(buffer, buffer_length);

   EXPECT_EQ(available, testData.length());

   EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

   available = connected_socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   available = accepted_socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   connected_socket.write(reinterpret_cast<const BYTE *>(testData.c_str()), static_cast<int>(testData.length()));

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_readable(::testing::_)).Times(1);

   EXPECT_EQ(pSocket->handle_events(), false);

   pSocket = GetCompletionKeyAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket->handle_events(), true);

   available = accepted_socket.read(buffer, buffer_length);

   EXPECT_EQ(available, testData.length());

   EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

   available = accepted_socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   available = connected_socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);
}

///////////////////////////////////////////////////////////////////////////////
// End of file: test.cpp
///////////////////////////////////////////////////////////////////////////////
