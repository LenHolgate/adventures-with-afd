///////////////////////////////////////////////////////////////////////////////
// File: echo_client.cpp
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
#include "single_connection_afd_system.h"
#include "afd_handle.h"

class echo_client : private tcp_socket_callbacks
{
   public :

      echo_client(
         afd_handle afd,
         int number_of_messages)
         : s(afd, *this),
           is_done(false),
           bytes_read(0),
           number_of_messages(number_of_messages),
           number_of_messages_sent(0)
      {
         for (auto i = 0; i < sizeof send_buffer; ++i)
         {
            send_buffer[i] = static_cast<BYTE>(i);
         }

         memset(recv_buffer, 0, sizeof recv_buffer);
      }

      ~echo_client() override
      {
         try
         {
            if (!is_done)
            {
               s.close();
            }
         }
         catch (...)
         {

         }
      }

      void connect(
         const sockaddr &address,
         const int address_length)
      {
         s.connect(address, address_length);
      }

      bool done() const
      {
         return is_done;
      }

   private :

      void write_data(
         tcp_socket &s)
      {
         if (number_of_messages_sent < number_of_messages)
         {
            if (sizeof send_buffer != s.write(send_buffer, sizeof send_buffer))
            {
               // todo, handle partial sends
               throw std::exception("failed to send all data");
            }

            ++number_of_messages_sent;

            read_data(s);
         }
         else
         {
            s.close();

            is_done = true;
         }
      }

      void read_data(
         tcp_socket &s)
      {
         int bytes_read_this_time = 0;

         do
         {
            int bytes_needed = sizeof recv_buffer - bytes_read;

            bytes_read_this_time = s.read(&recv_buffer[bytes_read], bytes_needed);

            bytes_read += bytes_read_this_time;
         }
         while (bytes_read_this_time && bytes_read < sizeof recv_buffer);

         if (bytes_read == sizeof recv_buffer)
         {
            // validate

            if (0 != memcmp(send_buffer, recv_buffer, bytes_read))
            {
               throw std::exception("validation failed");
            }

            bytes_read = 0;
            memset(recv_buffer, 0, sizeof recv_buffer);

            write_data(s);
         }
      }

      void on_connected(
         tcp_socket &s) override
      {
         write_data(s);
      }

      void on_connection_failed(
         tcp_socket &s,
         DWORD error) override
      {
         (void)s;
         (void)error;

         is_done = true;

         throw std::exception("connection failed");
      }

      void on_readable(
         tcp_socket &s) override
      {
         read_data(s);
      }


      void on_readable_oob(
         tcp_socket &s) override
      {
         (void)s;

         throw std::exception("unexpected out-of-band data available");
      }

      void on_writable(
         tcp_socket &s) override
      {
         (void)s;

         throw std::exception("unexpected writable...");
      }

      void on_client_close(
         tcp_socket &s) override
      {
         s.shutdown(tcp_socket::shutdown_how::both);

         is_done = true;
      }

      void on_connection_reset(
         tcp_socket &s) override
      {
         s.close();

         is_done = true;
      }

      void on_disconnected(
         tcp_socket &s) override
      {
         (void)s;

         is_done = true;
      }

      tcp_socket s;

      bool is_done;

      BYTE send_buffer[100];

      BYTE recv_buffer[sizeof send_buffer];

      int bytes_read;

      const int number_of_messages;

      int number_of_messages_sent;
};

int main(int argc, char **argv)
{
   InitialiseWinsock();

   try
   {
      const auto handles = CreateAfdAndIOCP();

      single_connection_afd_system afd(handles.afd);

      afd_handle handle(afd, 0);

      const int number_of_messages = 10;

      echo_client client(handle, number_of_messages);

      sockaddr_in address{};

      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      address.sin_port = htons(5050);

      client.connect(reinterpret_cast<const sockaddr &>(address), sizeof address);

      while (!client.done())
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
// End of file: echo_client.cpp
///////////////////////////////////////////////////////////////////////////////
