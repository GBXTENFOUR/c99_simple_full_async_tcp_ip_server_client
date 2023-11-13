#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include "hcomm.h"

int client_disconnect(hclient_t *cli, int error_code)
{
  connection_state_t prev_connection_state = cli->connection_state;
  delete_endpoint(&cli->server_endpoint);  
  cli->connection_state = CONNECTION_STATE_DISCONNECTED;
  if (prev_connection_state == CONNECTION_STATE_CONNECTED)
  {
    cli->disconnected_callback(cli);
  }
  return 0;
}

int client_connect(hclient_t *cli)
{
  switch (cli->connection_state)
  {
  case CONNECTION_STATE_DISCONNECTED:
    create_endpoint(&cli->server_endpoint);
    // Create socket
    cli->server_endpoint.socket = socket(AF_INET, SOCK_STREAM, 0);
    if (cli->server_endpoint.socket < 0)
    {
#ifdef HCOMM_DEBUG_ERROR
      printf("Error, Failed to create socket: %d\n", errno);
#endif
      client_disconnect(cli, errno);
      return -1;
    }
    // Allow IP address reuse
    int reuseAddr = 1;
    int result = setsockopt(cli->server_endpoint.socket, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr));
    if (result == -1)
    {
#ifdef HCOMM_DEBUG_ERROR
      printf("Error, ConnectNode setsockopt SOL_SOCKET SO_RESUSEADDR failure %d", errno);
#endif      
      client_disconnect(cli, errno);
      return result;
    }
    // Set non-blocking
    int flags = fcntl(cli->server_endpoint.socket, F_GETFL, 0);
    if (flags == -1)
    {
#ifdef HCOMM_DEBUG_ERROR
      printf("Error, ConnectNode fcntl F_GETFL failure %d", errno);
#endif
      client_disconnect(cli, errno);
      return flags;
    }
    flags |= O_NONBLOCK;
    result = fcntl(cli->server_endpoint.socket, F_SETFL, flags);
    if (result == -1)
    {
#ifdef HCOMM_DEBUG_ERROR
      printf("Error, ConnectNode fcntl F_SETFL failure %d", errno);
#endif
      client_disconnect(cli, errno);
      return result;
    }
    // Set up address
    struct sockaddr_in server_sockaddr;
    memset(&server_sockaddr, 0, sizeof(server_sockaddr));
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(cli->server_address);
    server_sockaddr.sin_port = htons(cli->server_port);
    cli->server_endpoint.address = server_sockaddr;

    result = connect(cli->server_endpoint.socket, (struct sockaddr *)&cli->server_endpoint.address, sizeof(struct sockaddr));
    if (result < 0)
    {
      if (errno == EINPROGRESS)
      {
        cli->connection_state = CONNECTION_STATE_INPROGRESS;
      }
      else
      {
#ifdef HCOMM_DEBUG_ERROR
        printf("Error, client_connect failure %d\n", errno);
#endif
        client_disconnect(cli, errno);
      }
      return result;
    }
    // Intentional fallthrough
    break;
  case CONNECTION_STATE_INPROGRESS:
  {
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
    fd_set connect_fd_set; 
    FD_ZERO(&connect_fd_set); 
    FD_SET(cli->server_endpoint.socket, &connect_fd_set); 
    int result = select(cli->server_endpoint.socket + 1, NULL, &connect_fd_set, NULL, &tv);
    if (result < 0 && errno != EINTR) 
    { 
        printf("Error, select for connecting %d - %s\n", errno, strerror(errno)); 
        client_disconnect(cli, errno);
        return result;
    } 
    else if (result > 0)
    {
        // Socket selected for write 
        int valopt = 0;
        socklen_t lon = sizeof(int); 
        valopt = 0;
        if (getsockopt(cli->server_endpoint.socket, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon) < 0) 
        { 
            printf("Error, in getsockopt() %d - %s\n", errno, strerror(errno)); 
            client_disconnect(cli, errno);
            return -2;
        } 
        // Check the value returned... 
        if (valopt) 
        { 
            printf("Error, in delayed connection() %d - %s\n", valopt, strerror(valopt)); 
            client_disconnect(cli, errno);
            return -3;
        }
        else
        {
          int option = 0;
          result = setsockopt(cli->server_endpoint.socket, SOL_TCP, TCP_NODELAY, &option, sizeof(option));
          if (result == -1)
          {
      #ifdef HCOMM_DEBUG_ERROR
            printf("Error, client_connect setsockopt TCP_NODELAY failure %d", errno);
      #endif
            client_disconnect(cli, errno);
            return result;
          }
          option = 1;
          result = setsockopt(cli->server_endpoint.socket, SOL_TCP, TCP_QUICKACK, &option, sizeof(option));
          if (result == -1)
          {
      #ifdef HCOMM_DEBUG_ERROR
            printf("Error, client_connect setsockopt TCP_QUICKACK failure %d", errno);
      #endif
            client_disconnect(cli, errno);
            return result;
          }

          printf("Connected to %s:%d.\n", cli->server_address, cli->server_port);
          cli->connection_state = CONNECTION_STATE_CONNECTED;
          cli->connected_callback(cli);          
        }
    }
  }
  break;
  case CONNECTION_STATE_CONNECTED:
    break;
  }
  return 0;
}

int client_build_fd_sets(hclient_t *cli)
{
  FD_ZERO(&cli->read_fds);
  FD_SET(cli->server_endpoint.socket, &cli->read_fds);

  FD_ZERO(&cli->write_fds);
  // If there is smth to send, set up write_fd for server_endpoint socket
  if (&cli->server_endpoint.send_queue.index > 0)
    FD_SET(cli->server_endpoint.socket, &cli->write_fds);

  FD_ZERO(&cli->error_fds);
  FD_SET(cli->server_endpoint.socket, &cli->error_fds);

  return 0;
}

int client_init(hclient_t *cli)
{
  client_connect(cli);
  return 0;
}

int client_periodic(hclient_t *cli)
{
  // If not connected, try to connect
  if (cli->connection_state != CONNECTION_STATE_CONNECTED)
  {
    client_connect(cli);
    return -1;
  }
  int maxfd = cli->server_endpoint.socket;
  // Select updates fd_set's, so we need to build fd_set's before each select()call.
  client_build_fd_sets(cli);
  // Don't wait on the select just read it
  struct timeval select_timeout = {.tv_sec = 0, .tv_usec = 0};
  int result = select(maxfd + 1, &cli->read_fds, &cli->write_fds, &cli->error_fds, &select_timeout);
  if (result == -1)
  {
#ifdef HCOMM_DEBUG_ERROR
    printf("Error, select failed: %d\n", errno);
#endif
    client_disconnect(cli, errno);
  }
  else if (result > 0)
  {
    if (FD_ISSET(cli->server_endpoint.socket, &cli->read_fds))
    {
      if (cli->connection_state == CONNECTION_STATE_CONNECTED)
      {
        if((result = receive_from_endpoint(&cli->server_endpoint)) < 0)
            client_disconnect(cli, result);
      }
    }

    if (FD_ISSET(cli->server_endpoint.socket, &cli->write_fds))
    {
      if (cli->connection_state == CONNECTION_STATE_CONNECTED)
      {
        if ((result = send_to_endpoint(&cli->server_endpoint)) < 0)
            client_disconnect(cli, result);
      }
    }

    if (FD_ISSET(cli->server_endpoint.socket, &cli->error_fds))
    {
#ifdef HCOMM_DEBUG_ERROR
      printf("Error, error_fds for server_endpoint.\n");
#endif
      client_disconnect(cli, errno);
    }
  }
  return 0;
}
