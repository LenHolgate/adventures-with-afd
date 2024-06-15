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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, INFINITE);

   EXPECT_CALL(callbacks, on_connection_failed(::testing::_, ::testing::_)).Times(1);

   pSocket->handle_events();
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   static const BYTE data[] = { 1, 2, 3, 4 };

   socket.write(data, sizeof data);
}

TEST(AFDSocket, TestConnectAndRecv)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   // Note that at present the remote end hasn't accepted

   const SOCKET s = listeningSocket.Accept();

   // accepted...

   const std::string testData("test");

   Write(s, testData);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_readable(::testing::_)).Times(1);

   pSocket->handle_events();

   available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, testData.length());

   EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

   available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);
}

TEST(AFDSocket, TestConnectAndLocalCloseWithNoPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   EXPECT_CALL(callbacks, on_disconnected(::testing::_)).Times(1);

   socket.close();

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndLocalCloseWithPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   socket.close();

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_disconnected(::testing::_)).Times(1);

   pSocket->handle_events();
}

TEST(AFDSocket, TestConnectAndLocalShutdownSendNoPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   socket.shutdown(tcp_socket::shutdown_how::send);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndLocalShutdownSendWithPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   socket.shutdown(tcp_socket::shutdown_how::send);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndLocalShutdownRecvNoPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   socket.shutdown(tcp_socket::shutdown_how::receive);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndLocalShutdownRecvWithPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   socket.shutdown(tcp_socket::shutdown_how::receive);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndLocalShutdownBothNoPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   socket.shutdown(tcp_socket::shutdown_how::both);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndLocalShutdownBothWithPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   socket.shutdown(tcp_socket::shutdown_how::both);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRemoteCloseNoPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   Close(s);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRemoteCloseWithPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   Close(s);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_client_close(::testing::_)).Times(1);

   pSocket->handle_events();
}

TEST(AFDSocket, TestConnectAndRemoteCloseWithNoPollPendingDetectsOnNextRead)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   Close(s);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_client_close(::testing::_)).Times(1);

   pSocket->handle_events();
}

TEST(AFDSocket, TestConnectAndRemoteCloseWithNoPollPendingDoesNotDetectOnNextWrite)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   Close(s);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);

   static const BYTE data[] = { 1, 2, 3, 4 };

   const int sent = socket.write(data, sizeof data);

   EXPECT_EQ(sent, sizeof data);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRemoteResetNoPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   Abort(s);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRemoteResetWithPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   Abort(s);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connection_reset(::testing::_)).Times(1);

   pSocket->handle_events();
}

TEST(AFDSocket, TestConnectAndRemoteResetWithNoPollPendingDetectsOnNextRead)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   Abort(s);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connection_reset(::testing::_)).Times(1);

   pSocket->handle_events();
}

TEST(AFDSocket, TestConnectAndRemoteResetWithNoPollPendingDetectsOnNextWrite)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   Abort(s);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);

   static const BYTE data[] = { 1, 2, 3, 4 };

   const int sent = socket.write(data, sizeof data);

   EXPECT_EQ(sent, 0);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_writable(::testing::_)).Times(1);
   EXPECT_CALL(callbacks, on_connection_reset(::testing::_)).Times(1);

   pSocket->handle_events();
}

TEST(AFDSocket, TestConnectAndRemoteShutdownSendNoPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   shutdown(s, SD_SEND);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRemoteShutdownSendWithPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   shutdown(s, SD_SEND);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_client_close(::testing::_)).Times(1);

   pSocket->handle_events();
}

TEST(AFDSocket, TestConnectAndRemoteShutdownSendWithNoPollPendingDetectsOnNextRead)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   shutdown(s, SD_SEND);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   //EXPECT_CALL(callbacks, on_client_close(::testing::_)).Times(1);

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_client_close(::testing::_)).Times(1);

   pSocket->handle_events();
}

TEST(AFDSocket, TestConnectAndRemoteShutdownSendWithNoPollPendingDoesNotDetectOnNextWrite)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   shutdown(s, SD_SEND);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);

   static const BYTE data[] = { 1, 2, 3, 4 };

   const int sent = socket.write(data, sizeof data);

   EXPECT_EQ(sent, sizeof data);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRemoteShutdownRecvNoPollPending)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   shutdown(s, SD_RECEIVE);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRemoteShutdownRecvWithPollPendingDetectsNothing)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s = listeningSocket.Accept();

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   shutdown(s, SD_RECEIVE);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO, WAIT_TIMEOUT);

   EXPECT_EQ(pSocket, nullptr);
}

TEST(AFDSocket, TestConnectAndRecvMultiplSockets)
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   //EXPECT_EQ(pSocket, &socket1);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   BYTE buffer[100];

   int buffer_length = sizeof buffer;

   int available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   // Note that at present the remote end hasn't accepted

   const SOCKET s1 = listeningSocket.Accept();

   // accepted...

   socket2.connect(reinterpret_cast<const sockaddr &>(address), sizeof(address));

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   //EXPECT_EQ(pSocket, &socket2);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

   const SOCKET s2 = listeningSocket.Accept();

   const std::string testData("test");

   Write(s1, testData);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_readable(::testing::_)).Times(1);

   pSocket->handle_events();

   available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, testData.length());

   EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

   available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   available = socket2.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   Write(s2, testData);

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_readable(::testing::_)).Times(1);

   pSocket->handle_events();

   available = socket2.read(buffer, buffer_length);

   EXPECT_EQ(available, testData.length());

   EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

   available = socket2.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);


   available = socket1.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);
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

   auto *pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &connected_socket);

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   pSocket->handle_events();

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

   EXPECT_CALL(callbacks, on_connected(::testing::_)).Times(1);

   accepted_socket.accepted();

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_EQ(pSocket, &accepted_socket);

   EXPECT_CALL(callbacks, on_writable(::testing::_)).Times(1);

   pSocket->handle_events();

   const std::string testData("test");

   accepted_socket.write(reinterpret_cast<const BYTE *>(testData.c_str()), static_cast<int>(testData.length()));

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_readable(::testing::_)).Times(1);

   pSocket->handle_events();

   available = connected_socket.read(buffer, buffer_length);

   EXPECT_EQ(available, testData.length());

   EXPECT_EQ(0, memcmp(testData.c_str(), buffer, available));

   available = connected_socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   available = accepted_socket.read(buffer, buffer_length);

   EXPECT_EQ(available, 0);

   connected_socket.write(reinterpret_cast<const BYTE *>(testData.c_str()), static_cast<int>(testData.length()));

   pSocket = GetCompletionAs<afd_events>(iocp, SHORT_TIME_NON_ZERO);

   EXPECT_CALL(callbacks, on_readable(::testing::_)).Times(1);

   pSocket->handle_events();

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
