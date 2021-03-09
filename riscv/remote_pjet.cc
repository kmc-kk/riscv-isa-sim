#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef AF_INET
#include <sys/socket.h>
#endif
#ifndef INADDR_ANY
#include <netinet/in.h>
#endif

#include <algorithm>
#include <cassert>
#include <cstdio>

#include "remote_pjet.h"

#if 1
#  define D(x) x
#else
#  define D(x)
#endif

/////////// remote_pjet_t

remote_pjet_t::remote_pjet_t(uint16_t port, debug_module_t *dm) :
  dm(dm),
  socket_fd(0),
  client_fd(0),
  recv_start(0),
  recv_end(0)
{
  socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    fprintf(stderr, "remote_bitbang failed to make socket: %s (%d)\n",
        strerror(errno), errno);
    abort();
  }

  fcntl(socket_fd, F_SETFL, O_NONBLOCK);
  int reuseaddr = 1;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr,
        sizeof(int)) == -1) {
    fprintf(stderr, "remote_bitbang failed setsockopt: %s (%d)\n",
        strerror(errno), errno);
    abort();
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(socket_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
    fprintf(stderr, "remote_bitbang failed to bind socket: %s (%d)\n",
        strerror(errno), errno);
    abort();
  }

  if (listen(socket_fd, 1) == -1) {
    fprintf(stderr, "remote_bitbang failed to listen on socket: %s (%d)\n",
        strerror(errno), errno);
    abort();
  }

  socklen_t addrlen = sizeof(addr);
  if (getsockname(socket_fd, (struct sockaddr *) &addr, &addrlen) == -1) {
    fprintf(stderr, "remote_bitbang getsockname failed: %s (%d)\n",
        strerror(errno), errno);
    abort();
  }

  printf("Listening for remote bitbang connection on port %d.\n",
      ntohs(addr.sin_port));
  fflush(stdout);
}

void remote_pjet_t::accept()
{
  client_fd = ::accept(socket_fd, NULL, NULL);
  if (client_fd == -1) {
    if (errno == EAGAIN) {
      // No client waiting to connect right now.
    } else {
      fprintf(stderr, "failed to accept on socket: %s (%d)\n", strerror(errno),
          errno);
      abort();
    }
  } else {
    fcntl(client_fd, F_SETFL, O_NONBLOCK);
  }
}

void remote_pjet_t::tick()
{
  if (client_fd > 0) {
    execute_commands();
  } else {
    this->accept();
  }
}

void remote_pjet_t::execute_commands()
{
  static char send_buf[buf_size];
  unsigned total_processed = 0;
  bool quit = false;
  bool entered_rti = false;
  while (1) {
    if (recv_start < recv_end) {
      unsigned send_offset = 0;
      while (recv_start < recv_end) {
        uint8_t command = recv_buf[recv_start];

        dm->run_test_idle();
        switch (command) {
          case 'B': fprintf(stderr, "*BLINK*\n"); break;
          case 'b': fprintf(stderr, "_______\n"); break;
          case 'w': {
		        recv_start++;
            uint8_t addr = static_cast<uint8_t>(recv_buf[recv_start]);
		        recv_start++;
            uint32_t *data = reinterpret_cast<uint32_t *>(&recv_buf[recv_start]);
            //fprintf(stderr, "DMIWrite: addr = %02x, data = 0x%08x\n", addr, *data);
		  	    dm->dmi_write(addr, *data); 
			      recv_start += 3;
			      total_processed += 3;
			      break;
          }
		      case 'r': {
		        recv_start++;
            uint8_t addr = static_cast<uint8_t>(recv_buf[recv_start]);
			      dm->dmi_read(addr,
              reinterpret_cast<uint32_t *>(&send_buf[send_offset]));
            //fprintf(stderr, "DMIRead: addr = %02x, data = 0x%08x\n", addr, *reinterpret_cast<uint32_t *>(&send_buf[send_offset]));
			      send_offset += 4;
			      break;
          }
          case 'Q': quit = true; break;
            default:
              fprintf(stderr, "remote_pjet got unsupported command '%c'\n",
                      command);
        }
        entered_rti = true;
        recv_start++;
        total_processed++;
      }
      unsigned sent = 0;
      while (sent < send_offset) {
        ssize_t bytes = write(client_fd, send_buf + sent, send_offset);
        if (bytes == -1) {
          fprintf(stderr, "failed to write to socket: %s (%d)\n", strerror(errno), errno);
          abort();
        }
        sent += bytes;
      }
    }

    if (total_processed > buf_size || quit || entered_rti) {
      // Don't go forever, because that could starve the main simulation.
      break;
    }

    recv_start = 0;
    recv_end = read(client_fd, recv_buf, buf_size);

    if (recv_end == -1) {
      if (errno == EAGAIN) {
        break;
      } else {
        fprintf(stderr, "remote_bitbang failed to read on socket: %s (%d)\n",
            strerror(errno), errno);
        abort();
      }
    }

    if (quit) {
      fprintf(stderr, "Remote Bitbang received 'Q'\n");
    }

    if (recv_end == 0 || quit) {
      // The remote disconnected.
      fprintf(stderr, "Received nothing. Quitting.\n");
      close(client_fd);
      client_fd = 0;
      break;
    }
  }
}
