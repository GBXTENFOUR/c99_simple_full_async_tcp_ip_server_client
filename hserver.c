// Simple example of server with select() and multiple clients.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "hcomm.h"

void server_shutdown(hserver_t *svr, int code);

/* Start listening socket listen_sock. */
int server_start_listening(hserver_t *svr)
{
  // Obtain a file descriptor for our "listening" socket.
  svr->listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (svr->listen_sock < 0)
  {
#ifdef HCOMM_DEBUG_ERROR
    printf("Error, create socket error: %d \n", errno);
#endif
		return -1;
  }

  int reuse = 1;
  if (setsockopt(svr->listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
  {
#ifdef HCOMM_DEBUG_ERROR
		printf("Error, setsockopt SOL_SOCKET SO_RESUSEADDR error: %d\n", errno);
#endif
		return -1;
  }
  
  memset(&svr->svr_addr, 0, sizeof(svr->svr_addr));
  svr->svr_addr.sin_family = AF_INET;  
  svr->svr_addr.sin_addr.s_addr = INADDR_ANY;
  svr->svr_addr.sin_port = htons(svr->listen_port);

	// Set non-blocking
	int flags = fcntl(svr->listen_sock, F_GETFL, 0);
	if (flags == -1)
	{
#ifdef HCOMM_DEBUG_ERROR
		printf("Error, fcntl F_GETFL error: %d\n", errno);
#endif
		return(14);
	}
	flags |= O_NONBLOCK;
	int result = fcntl(svr->listen_sock, F_SETFL, flags);
	if (result == -1)
	{
#ifdef HCOMM_DEBUG_ERROR
		printf("Error, fcntl F_SETFL error: %d\n", errno);
#endif
		return(15);
	}

  if (bind(svr->listen_sock, (struct sockaddr *)&svr->svr_addr, sizeof(struct sockaddr)) != 0)
  {
#ifdef HCOMM_DEBUG_ERROR
    printf("Error, bind failure: %d\n", errno);
#endif
    return -1;
  }

  // Start accept client connections
  if (listen(svr->listen_sock, LISTEN_MAX) != 0)
  {
#ifdef HCOMM_DEBUG_ERROR
    printf("Error, listening error: %d\n", errno);
#endif
    return -1;
  }
  printf("Info, Listening for incoming connections on port:%d\n", svr->listen_port);
  return 0;
}

void server_shutdown(hserver_t *svr, int code)
{
  int i;

  close(svr->listen_sock);

  for (i = 0; i < MAX_CLIENTS; ++i)
    if (svr->client_list[i].socket != NO_SOCKET)
      close(svr->client_list[i].socket);

  printf("Shutdown server properly.\n");  
}

int server_build_fd_sets(hserver_t *svr)
{
  FD_ZERO(&svr->read_fds);  
  FD_SET(svr->listen_sock, &svr->read_fds);
  for (int i = 0; i < MAX_CLIENTS; ++i)
    if (svr->client_list[i].socket != NO_SOCKET)
      FD_SET(svr->client_list[i].socket, &svr->read_fds);

  FD_ZERO(&svr->write_fds);
  for (int i = 0; i < MAX_CLIENTS; ++i)
    if (svr->client_list[i].socket != NO_SOCKET && svr->client_list[i].send_queue.index > 0)
      FD_SET(svr->client_list[i].socket, &svr->write_fds);

  FD_ZERO(&svr->error_fds);  
  FD_SET(svr->listen_sock, &svr->error_fds);
  for (int i = 0; i < MAX_CLIENTS; ++i)
    if (svr->client_list[i].socket != NO_SOCKET)
      FD_SET(svr->client_list[i].socket, &svr->error_fds);

  return 0;
}

int server_handle_new_connection(hserver_t* svr)
{
  struct sockaddr_in client_addr;
  memset(&client_addr, 0, sizeof(client_addr));
  socklen_t client_len = sizeof(client_addr);
  int new_client_sock = accept(svr->listen_sock, (struct sockaddr *)&client_addr, &client_len);
  if (new_client_sock < 0)
  {
#ifdef HCOMM_DEBUG_ERROR
    printf("Error, accept failure  %d\n", errno);
#endif
    return -1;
  }

  int option = 0;
  int result = setsockopt(new_client_sock, SOL_TCP, TCP_NODELAY, &option, sizeof(option));
  if (result == -1)
  {
#ifdef HCOMM_DEBUG_ERROR
    printf("Error, server_handle_new_connection setsockopt TCP_NODELAY failure %d", errno);
#endif
    return -2;
  }

  option = 1;
  result = setsockopt(new_client_sock, SOL_TCP, TCP_QUICKACK, &option, sizeof(option));
  if (result == -1)
  {
#ifdef HCOMM_DEBUG_ERROR
    printf("Error, server_handle_new_connection setsockopt TCP_QUICKACK failure %d", errno);
#endif
    return -2;
  }
  char client_ipv4_str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &client_addr.sin_addr, client_ipv4_str, INET_ADDRSTRLEN);

  printf("Info, Incoming connection from %s:%d.\n", client_ipv4_str, client_addr.sin_port);

  for (int i = 0; i < MAX_CLIENTS; ++i)
  {
    if (svr->client_list[i].socket == NO_SOCKET)
    {
      svr->client_list[i].socket = new_client_sock;
      svr->client_list[i].address = client_addr;
      svr->client_list[i].send_packet_index = -1;
      svr->client_list[i].receive_packet_index = 0;
      svr->client_list[i].packet_received_callback = 0;
      svr->client_connected_callback(svr, i);
      return 0;
    }
  }
#ifdef HCOMM_DEBUG_ERROR
  printf("Error, Connection limit %d reached. Closing new connection %s:%d.\n", MAX_CLIENTS, client_ipv4_str, client_addr.sin_port);
#endif
  close(new_client_sock);
  return -1;
}

int server_close_client_connection(endpoint_t *client)
{
  printf("Info, Close client socket for %s.\n", get_endpoint_address_str(client));

  close(client->socket);
  client->socket = NO_SOCKET;
  dequeue_all(&client->send_queue);
  client->send_packet_index = -1;
  client->receive_packet_index = 0;
  
  return 0;
}

int server_queue_send_packet(hserver_t* svr, hp_packet_t* new_packet)
{
  /* Queue packet for all clients */
  for (int i = 0; i < MAX_CLIENTS; ++i)
  {
    if (svr->client_list[i].socket != NO_SOCKET)
    {
      if (endpoint_queue_send(&svr->client_list[i], new_packet) != 0)
      {
#ifdef HCOMM_DEBUG_ERROR
        printf("Error, Send queue is full, we lost this packet!\n");
#endif
        continue;
      }
#ifdef HCOM_DEBUG_VERBOSE
      printf("Info, New packet queued for sending.\n");
#endif
    }
  }

  return 0;
}

int server_handle_received_packet(hp_packet_t *packet)
{
  printf("Received packet from client.\n");
  print_packet(packet);
  return 0;
}

int server_init(hserver_t* svr)
{
  int result = server_start_listening(svr);
  if ( result != 0)
      return result;

  for (int i = 0; i < MAX_CLIENTS; ++i)
  {
    svr->client_list[i].socket = NO_SOCKET;
    create_endpoint(&svr->client_list[i]);
  }
  svr->initialized = true;
  return 0;
}

int server_periodic(hserver_t* svr)
{
    int high_sock = svr->listen_sock;
    server_build_fd_sets(svr);

    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
      if (svr->client_list[i].socket > svr->listen_sock)
          high_sock = svr->client_list[i].socket;
    }
    struct timeval select_timeout = { .tv_sec = 0, .tv_usec = 0 };
    int result = select(high_sock + 1, &svr->read_fds, &svr->write_fds, &svr->error_fds, &select_timeout);

    if (result == -1)
    {
#ifdef HCOMM_DEBUG_ERROR
        printf("Error, select failed: %d\n", errno);
#endif
        server_shutdown(svr, EXIT_FAILURE);
    }
    // Select returned something
    else if (result > 0)
    {
      /* All set fds should be checked. */
      if (FD_ISSET(svr->listen_sock, &svr->read_fds))
      {
        server_handle_new_connection(svr);
      }
      if (FD_ISSET(svr->listen_sock, &svr->error_fds))
      {
#ifdef HCOMM_DEBUG_ERROR
        printf("Error, error_fds on listen socket fd.\n");
#endif
        server_shutdown(svr, EXIT_FAILURE);
      }

      for (int i = 0; i < MAX_CLIENTS; ++i)
      {
        if (svr->client_list[i].socket == NO_SOCKET)
          continue;
        
        if (FD_ISSET(svr->client_list[i].socket, &svr->error_fds))
        {
#ifdef HCOMM_DEBUG_ERROR
          printf("Error, error_fds for client fd.\n");
#endif
          svr->client_disconnected_callback(svr, i);
          server_close_client_connection(&svr->client_list[i]);          
          continue;
        }

        if (FD_ISSET(svr->client_list[i].socket, &svr->read_fds))
        {
          if (receive_from_endpoint(&svr->client_list[i]) < 0)
          {              
              svr->client_disconnected_callback(svr, i);
              server_close_client_connection(&svr->client_list[i]);
              continue;
          }
        }

        if (FD_ISSET(svr->client_list[i].socket, &svr->write_fds))
        {
          if (send_to_endpoint(&svr->client_list[i]) < 0)
          {                          
              svr->client_disconnected_callback(svr, i);
              server_close_client_connection(&svr->client_list[i]);
              continue;
          }
        }
      }
    }
    return 0;
}
