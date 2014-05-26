/*
   Reimplementation of sslh in C

# Copyright (C) 2007-2008  Yves Rutschle
# 
# This program is free software; you can redistribute it
# and/or modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later
# version.
# 
# This program is distributed in the hope that it will be
# useful, but WITHOUT ANY WARRANTY; without even the implied
# warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.  See the GNU General Public License for more
# details.
# 
# The full text for the General Public License is here:
# http://www.gnu.org/licenses/gpl.html

*/

#include "common.h"

const char* server_type = "sslh-fork";

#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

/* shovels data from one fd to the other and vice-versa 
   returns after one socket closed
 */
int shovel(struct connection *cnx)
{
   fd_set fds;
   int res, i;
   int max_fd = MAX(cnx->q[0].fd, cnx->q[1].fd) + 1;

   FD_ZERO(&fds);
   while (1) {
      FD_SET(cnx->q[0].fd, &fds);
      FD_SET(cnx->q[1].fd, &fds);

      res = select(
                   max_fd,
                   &fds,
                   NULL,
                   NULL,
                   NULL
                  );
      CHECK_RES_DIE(res, "select");

      for (i = 0; i < 2; i++) {
          if (FD_ISSET(cnx->q[i].fd, &fds)) {
              res = fd2fd(&cnx->q[1-i], &cnx->q[i]);
              if (!res) {
                  if (verbose) 
                      fprintf(stderr, "%s %s", i ? "client" : "server", "socket closed\n");
                  return res;
              }
          }
      }
   }
}

/* Child process that finds out what to connect to and proxies 
 */
void start_shoveler(int in_socket)
{
   fd_set fds;
   struct timeval tv;
   struct addrinfo *saddr;
   int res;
   int out_socket;
   struct connection cnx;
   T_PROTO_ID prot;

   init_cnx(&cnx);

   FD_ZERO(&fds);
   FD_SET(in_socket, &fds);
   memset(&tv, 0, sizeof(tv));
   tv.tv_sec = probing_timeout;
   res = select(in_socket + 1, &fds, NULL, NULL, &tv);
   if (res == -1)
      perror("select");

   cnx.q[0].fd = in_socket;

   if (FD_ISSET(in_socket, &fds)) {
       /* Received data: figure out what protocol it is */
       prot = probe_client_protocol(&cnx);
   } else {
       /* Timed out: it's necessarily SSH */
       prot = PROT_SSH;
   }

   saddr = &protocols[prot].saddr;
   if (protocols[prot].service && 
       check_access_rights(in_socket, protocols[prot].service)) {
       exit(0);
   }

   /* Connect the target socket */
   out_socket = connect_addr(saddr, protocols[prot].description);
   CHECK_RES_DIE(out_socket, "connect");

   cnx.q[1].fd = out_socket;

   log_connection(&cnx);

   flush_defered(&cnx.q[1]);

   shovel(&cnx);

   close(in_socket);
   close(out_socket);
   
   if (verbose)
      fprintf(stderr, "connection closed down\n");

   exit(0);
}

void main_loop(int listen_sockets[], int num_addr_listen)
{
    int in_socket, i;

    for (i = 0; i < num_addr_listen; i++) {
        if (!fork()) {
            while (1)
            {
                in_socket = accept(listen_sockets[i], 0, 0);
                if (verbose) fprintf(stderr, "accepted fd %d\n", in_socket);

                if (!fork())
                {
                    close(listen_sockets[i]);
                    start_shoveler(in_socket);
                    exit(0);
                }
                close(in_socket);
            }
        }
    }
    wait(NULL);
}

/* The actual main is in common.c: it's the same for both version of
 * the server
 */

