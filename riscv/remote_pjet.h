#ifndef REMOTE_PJET_H
#define REMOTE_PJET_H

#include <stdint.h>

#include "debug_module.h"

class remote_pjet_t
{
public:
  // Create a new server, listening for connections from localhost on the given
  // port.
  remote_pjet_t(uint16_t port, debug_module_t *dm);

  // Do a bit of work.
  void tick();

private:
  debug_module_t *dm;

  int socket_fd;
  int client_fd;

  static const ssize_t buf_size = 64 * 1024;
  char recv_buf[buf_size];
  ssize_t recv_start, recv_end;

  // Check for a client connecting, and accept if there is one.
  void accept();
  // Execute any commands the client has for us.
  void execute_commands();
};

#endif

