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

#include "tcp_listening_socket.h"
#include "tcp_socket.h"
#include "afd_system.h"

#pragma comment(lib, "ntdll.lib")

int main(int argc, char **argv) {
   testing::InitGoogleTest(&argc, argv);

  InitialiseWinsock();

  return RUN_ALL_TESTS();
}

class mock_tcp_listening_socket_callbacks : public tcp_listening_socket_callbacks
{
   public :

   MOCK_METHOD(void, on_incoming_connection, (tcp_listening_socket &), (override));
};

TEST(AFDListeningSocket, TestConstruct)
{
   const auto handles = CreateAfdAndIOCP();

   afd_system afd(handles.afd);

   afd_handle handle(afd, 0);

   mock_tcp_listening_socket_callbacks callbacks;

   tcp_listening_socket socket(handle, callbacks);
}

TEST(AFDListeningSocket, TestConstructWithAddress)
{
   const auto port = GetAvailablePort();

   const auto handles = CreateAfdAndIOCP();

   afd_system afd(handles.afd);

   afd_handle handle(afd, 0);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(port);

   mock_tcp_listening_socket_callbacks callbacks;

   tcp_listening_socket socket(handle, reinterpret_cast<const sockaddr &>(address), sizeof(address), callbacks);
}

TEST(AFDListeningSocket, TestConstructWithInvalidAddress)
{
   const auto handles = CreateAfdAndIOCP();

   afd_system afd(handles.afd);

   afd_handle handle(afd, 0);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_NONE);
   address.sin_port = htons(0);

   mock_tcp_listening_socket_callbacks callbacks;

   EXPECT_THROW(tcp_listening_socket socket(handle, reinterpret_cast<const sockaddr &>(address), sizeof(address), callbacks), std::exception);
}

TEST(AFDListeningSocket, TestConstructWithAddressInUse)
{
   const auto portInUse = CreateListeningSocket();

   const auto handles = CreateAfdAndIOCP();

   afd_system afd(handles.afd);

   afd_handle handle(afd, 0);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(portInUse.port);

   mock_tcp_listening_socket_callbacks callbacks;

   EXPECT_THROW(tcp_listening_socket socket(handle, reinterpret_cast<const sockaddr &>(address), sizeof(address), callbacks), std::exception);
}


TEST(AFDListeningSocket, TestBind)
{
   const auto handles = CreateAfdAndIOCP();

   afd_system afd(handles.afd);

   afd_handle handle(afd, 0);

   mock_tcp_listening_socket_callbacks callbacks;

   tcp_listening_socket socket(handle, callbacks);

   const auto port = GetAvailablePort();

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(port);

   socket.bind(reinterpret_cast<const sockaddr &>(address), sizeof(address));
}

TEST(AFDListeningSocket, TestBindWithInvalidAddress)
{
   const auto handles = CreateAfdAndIOCP();

   afd_system afd(handles.afd);

   afd_handle handle(afd, 0);

   mock_tcp_listening_socket_callbacks callbacks;

   tcp_listening_socket socket(handle, callbacks);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_NONE);
   address.sin_port = htons(0);

   EXPECT_THROW(socket.bind(reinterpret_cast<const sockaddr &>(address), sizeof(address)), std::exception);
}

TEST(AFDListeningSocket, TestBindWithAddressInUse)
{
   const auto handles = CreateAfdAndIOCP();

   afd_system afd(handles.afd);

   afd_handle handle(afd, 0);

   mock_tcp_listening_socket_callbacks callbacks;

   tcp_listening_socket socket(handle, callbacks);

   const auto portInUse = CreateListeningSocket();

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(portInUse.port);

   EXPECT_THROW(socket.bind(reinterpret_cast<const sockaddr &>(address), sizeof(address)), std::exception);
}

TEST(AFDListeningSocket, TestListen)
{
   const auto port = GetAvailablePort();

   const auto handles = CreateAfdAndIOCP();

   afd_system afd(handles.afd);

   afd_handle handle(afd, 0);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(port);

   mock_tcp_listening_socket_callbacks callbacks;

   tcp_listening_socket socket(handle, reinterpret_cast<const sockaddr &>(address), sizeof(address), callbacks);

   socket.listen(10);
}

TEST(AFDListeningSocket, TestListenBeforeBind)
{
   const auto handles = CreateAfdAndIOCP();

   afd_system afd(handles.afd);

   afd_handle handle(afd, 0);

   mock_tcp_listening_socket_callbacks callbacks;

   tcp_listening_socket socket(handle, callbacks);

   EXPECT_THROW(socket.listen(10), std::exception);
}

TEST(AFDListeningSocket, TestAccept)
{
   const auto port = GetAvailablePort();

   const auto handles = CreateAfdAndIOCP();

   afd_system afd(handles.afd);

   afd_handle handle(afd, 0);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(port);

   mock_tcp_listening_socket_callbacks callbacks;

   tcp_listening_socket socket(handle, reinterpret_cast<const sockaddr &>(address), sizeof(address), callbacks);

   socket.listen(10);

   auto s = CreateTCPSocket();

   ::connect(s, &reinterpret_cast<const sockaddr &>(address), sizeof(address));

   EXPECT_CALL(callbacks, on_incoming_connection(::testing::_)).Times(1);

   auto *pAfd = GetCompletionAs<afd_system>(handles.iocp, SHORT_TIME_NON_ZERO);

   pAfd->handle_events();

   ::closesocket(s);
}

TEST(AFDListeningSocket, TestClose)
{
   const auto port = GetAvailablePort();

   const auto handles = CreateAfdAndIOCP();

   afd_system afd(handles.afd);

   afd_handle handle(afd, 0);

   sockaddr_in address {};

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons(port);

   mock_tcp_listening_socket_callbacks callbacks;

   tcp_listening_socket socket(handle, reinterpret_cast<const sockaddr &>(address), sizeof(address), callbacks);

   socket.listen(10);

   socket.close();
}

///////////////////////////////////////////////////////////////////////////////
// End of file: test.cpp
///////////////////////////////////////////////////////////////////////////////
