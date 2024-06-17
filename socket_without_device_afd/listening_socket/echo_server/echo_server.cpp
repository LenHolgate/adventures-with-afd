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

#include "tcp_listening_socket.h"

class echo_server_connection : private tcp_socket_callbacks
{
   public :

      echo_server_connection(
         HANDLE iocp,
         SOCKET accepted)
         : s(iocp, accepted, *this),
           bytes_read(0)
      {
         memset(recv_buffer, 0, sizeof recv_buffer);

         std::cout << this << " - echo_server_connection created" << std::endl;
      }

      ~echo_server_connection() override
      {
         std::cout << this << " - echo_server_connection destroyed" << std::endl;
      }

      void accepted()
      {
         s.accepted();
      }

   private :

      void write_data(
         tcp_socket &s)
      {
         std::cout << this << " - write_data: " << bytes_read << std::endl;

         if (bytes_read)
         {
            // if we have data to write...

            const auto bytes_written = s.write(recv_buffer, bytes_read);

            std::cout << this << " - write_data - written: " << bytes_written << std::endl;

            // write as much as we can...

            bytes_read -= bytes_written;

            if (bytes_read)
            {
               // if we didn't write it all...

               if (bytes_written)
               {
                  std::cout << this << " - write_data - shuffle: " << bytes_read << std::endl;

                  // remove the data we DID write

                  memmove(recv_buffer, &recv_buffer[bytes_written], bytes_read);
               }
            }

            // and see if we can read some more...

            read_data(s);
         }
      }

      void read_data(
         tcp_socket &s)
      {
         std::cout << this << " - read_data" << std::endl;

         int space_available = sizeof recv_buffer - bytes_read;

         if (space_available)
         {
            int bytes_read_this_time = 0;

            do
            {
               // while we have space, read more data

               bytes_read_this_time = s.read(&recv_buffer[bytes_read], space_available);

               bytes_read += bytes_read_this_time;

               std::cout << this << " - read_data - new data: " << bytes_read_this_time << std::endl;
               std::cout << this << " - read_data - total data: " << bytes_read << std::endl;

               space_available = sizeof recv_buffer - bytes_read;
            }
            while (space_available && bytes_read_this_time);
         }

         if (bytes_read)
         {
            // if we have data, echo it back...

            write_data(s);
         }
      }

      void on_connected(
         tcp_socket &s) override
      {
         std::cout << this << " - on_connected" << std::endl;

         read_data(s);
      }

      void on_connection_failed(
         tcp_socket &s,
         DWORD error) override
      {
         (void)s;
         (void)error;

         throw std::exception("connection failed");
      }

      void on_readable(
         tcp_socket &s) override
      {
         std::cout << this << " - on_readable" << std::endl;

         read_data(s);
      }

      void on_readable_oob(
         tcp_socket &s) override
      {
         std::cout << this << " - on_readable_oob" << std::endl;

         (void)s;

         throw std::exception("unexpected out-of-band data available");
      }

      void on_writable(
         tcp_socket &s) override
      {
         std::cout << this << " - on_writable" << std::endl;

         (void)s;
      }

      void on_client_close(
         tcp_socket &s) override
      {
         std::cout << this << " - on_client_close" << std::endl;

         if (!bytes_read)
         {
            std::cout << this << " - on_client_close - no more data" << std::endl;
            // no more data to write

            s.shutdown(tcp_socket::shutdown_how::both);

            s.close();
         }
      }

      void on_connection_reset(
         tcp_socket &s) override
      {
         std::cout << this << " - on_connection_reset" << std::endl;

         s.close();
      }

      void on_disconnected(
         tcp_socket &s) override
      {
         std::cout << this << " - on_disconnected" << std::endl;

         (void)s;
      }

      tcp_socket s;

      BYTE recv_buffer[100];

      int bytes_read;
};

class echo_server : private tcp_listening_socket_callbacks
{
   public :

      echo_server(
         HANDLE iocp)
         : s(iocp, *this),
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
         std::cout << "listening_socket - on_incoming_connections" << std::endl;

         bool accepting = true;

         while (accepting)
         {
            std::cout << "listening_socket - trying to accept" << std::endl;

            sockaddr_in client_address{};

            int client_address_length = sizeof client_address;

            auto accepted = s.accept(reinterpret_cast<sockaddr &>(client_address), client_address_length);
               
            if (accepted != INVALID_SOCKET)
            {
               std::cout << "listening_socket - new connection accepted" << std::endl;

               auto *pConnection = new echo_server_connection(s.get_iocp(), accepted);

               pConnection->accepted();
            }
            else
            {
               accepting = false;
            }
         }
      }

      void on_connection_reset(
         tcp_listening_socket &s) override
      {
         std::cout << "listening_socket - on_connection_reset" << std::endl;

         s.close();

         is_done = true;
      }

      void on_disconnected(
         tcp_listening_socket &s) override
      {
         std::cout << "listening_socket - on_disconnected" << std::endl;

         (void)s;

         is_done = true;
      }

      tcp_listening_socket s;

      bool is_done;
};

int main(int argc, char **argv)
{
   InitialiseWinsock();

   try
   {
      const auto iocp = CreateIOCP();

      echo_server server(iocp);

      sockaddr_in address{};

      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      address.sin_port = htons(5050);

      const int backlog = 10;

      server.listen(reinterpret_cast<const sockaddr &>(address), sizeof address, backlog);

      while (!server.done())
      {
         std::cout << "wait for events" << std::endl;

         // process events

         DWORD numberOfBytes = 0;

         ULONG_PTR completionKey = 0;

         OVERLAPPED *pOverlapped = nullptr;

         if (!GetQueuedCompletionStatus(iocp, &numberOfBytes, &completionKey, &pOverlapped, INFINITE))
         {
            const DWORD lastError = GetLastError();

            if (lastError != ERROR_OPERATION_ABORTED)
            {
               ErrorExit("GetQueuedCompletionStatus");
            }
         }

         auto *pSocket = reinterpret_cast<afd_events*>(completionKey);

         if (pSocket)
         {
            std::cout << "processing events" << std::endl;
            pSocket->handle_events();
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
