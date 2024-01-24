///////////////////////////////////////////////////////////////////////////////
// File: echo_server.cpp
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

#include "tcp_socket.h"
#include "multi_connection_afd_system.h"
#include "afd_handle.h"

#include "tcp_listening_socket.h"

class echo_server : private tcp_listening_socket_callbacks
{
   public :

      echo_server(
         afd_handle afd)
         : s(afd, *this),
           is_done(false)
      {
      }

      ~echo_server() override
      {
         try
         {
            s.close();
         }
         catch (...)
         {

         }
      }

      void listen(
         const sockaddr &address,
         const int address_length,
         const int backlog)
      {
         s.bind(address, address_length);

         s.listen(backlog);
      }

      bool done() const
      {
         return is_done;
      }

   private :

      void on_incoming_connections(
         tcp_listening_socket &s) override
      {
         std::cout << "on_incoming_connections" << std::endl;

         bool accepting = true;

         while (accepting)
         {
            sockaddr_in client_address{};

            int client_address_length = sizeof client_address;

            SOCKET client_socket = s.accept(reinterpret_cast<sockaddr &>(client_address), client_address_length);

            accepting = (client_socket != INVALID_SOCKET);

            if (accepting)
            {
               std::cout << "new connection accepted" << std::endl;

               static const char *pMessage = "TODO\r\n";

               ::send(client_socket, pMessage, 6, 0);

               // this is where we need to support multiple sockets in the afd system...

               ::shutdown(client_socket, SD_SEND);
               ::closesocket(client_socket);
            }
         }
      }

      void on_connection_reset(
         tcp_listening_socket &s) override
      {
         std::cout << "on_connection_reset" << std::endl;

         s.close();

         is_done = true;
      }

      void on_disconnected(
         tcp_listening_socket &s) override
      {
         std::cout << "on_disconnected" << std::endl;

         (void)s;

         is_done = true;
      }

      tcp_listening_socket s;

      bool is_done;

      BYTE recv_buffer[100];
};

int main(int argc, char **argv)
{
   InitialiseWinsock();

   try
   {
      const auto handles = CreateAfdAndIOCP();

      multi_connection_afd_system afd(handles.afd);

      afd_handle handle(afd, 0);

      echo_server server(handle);

      sockaddr_in address{};

      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      address.sin_port = htons(5050);

      const int backlog = 10;

      server.listen(reinterpret_cast<const sockaddr &>(address), sizeof address, backlog);

      while (!server.done())
      {
         // process events

         auto *pAfd = GetCompletionAs<afd_system_events>(handles.iocp, INFINITE);

         if (pAfd)
         {
            pAfd->handle_events();
         }
         else
         {
            throw std::exception("failed to process events");
         }
      }
   }
   catch (std::exception &e)
   {
      std::cout << "exception: " << e.what() << std::endl;
   }

   std::cout << "all done" << std::endl;

   return 0;
}

///////////////////////////////////////////////////////////////////////////////
// End of file: echo_server.cpp
///////////////////////////////////////////////////////////////////////////////
