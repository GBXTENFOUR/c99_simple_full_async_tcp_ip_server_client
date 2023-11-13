#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include "hcomm.h"

int packet_received(endpoint_t* peer, hp_packet_t* packet)
{
#ifdef HCOMM_DEBUG_INFO
    printf("Info, server RX from %s containing a message of %d bytes\n", get_endpoint_address_str(peer), packet->header.message_size);
#endif
    // Send a reply packet back
    hp_packet_t reply_packet;
    memset(&reply_packet, 0, sizeof(reply_packet));
    // Specify the size of the message inside the reply packet
    reply_packet.header.message_size = snprintf((char *)reply_packet.message, HP_MESSAGE_MAX_SIZE, "Reply to peer %s\r\n", get_endpoint_address_str(peer));
#ifdef HCOMM_DEBUG_INFO
    printf("Info, server TX to %s containing a message of %d bytes\n", get_endpoint_address_str(peer), reply_packet.header.message_size);
#endif
    endpoint_queue_send(peer, &reply_packet);
    return 0;
}

int client_connected_callback(hserver_t* svr, int i)
{
    printf("Info, new client connected from %s\n", get_endpoint_address_str(&svr->client_list[i]));
    // Setup the receive callback
    svr->client_list[i].packet_received_callback = packet_received;
    // Send a welcome packet back
    hp_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    // Specify the size of the message inside the packet
    packet.header.message_size = snprintf((char *)packet.message, HP_MESSAGE_MAX_SIZE, "Welcome client#%d\r\n", i);
    server_queue_send_packet(svr, &packet);
    return 0;
}

int client_disconnected_callback(hserver_t* svr, int i)
{
    printf("Info, client disconnected.\n");
    return 0;
}

void handle_signal_action(int sig_number)
{
  if (sig_number == SIGINT) 
  {
    printf("SIGINT was caught!\n");
    exit(EXIT_FAILURE);
  }
  else if (sig_number == SIGPIPE) 
  {
    printf("SIGPIPE was caught!\n");
  }
}

int setup_signals()
{
  struct sigaction sa;
  sa.sa_handler = handle_signal_action;
  if (sigaction(SIGINT, &sa, 0) != 0) 
  {
    perror("sigaction()");
    return -1;
  }
  if (sigaction(SIGPIPE, &sa, 0) != 0) 
  {
    perror("sigaction()");
    return -1;
  }
  
  return 0;
}

int main(int argc, char **argv)
{
    setup_signals();
    hserver_t svr = {.listen_port = 31000,
                     .client_connected_callback = client_connected_callback,
                     .client_disconnected_callback = client_disconnected_callback};

    if (server_init(&svr) < 0)
    {
        printf("Error, cannot initialize server on port: %d\n", svr.listen_port);
        exit(EXIT_FAILURE);
    }
    while (true)
    {
        server_periodic(&svr);
    }
    return 0;
}
