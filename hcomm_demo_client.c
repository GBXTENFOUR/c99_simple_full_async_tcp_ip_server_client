#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include "hcomm.h"

#define HCOMM_DEBUG_BANDWIDTH

#ifdef HCOMM_DEBUG_BANDWIDTH
typedef struct 
{
  int bytes_sent;
  int bytes_received;
  int prev_bytes_sent;
  int prev_bytes_received;
  uint32_t prev_time_ms;
} bandwidth_t;

bandwidth_t bandwidth = { 0,0,0,0,0};
#define BANDWIDTH_CALCULATION_INTERVAL_MS 5000 // Five seconds
#endif

int packet_received(endpoint_t* peer, hp_packet_t* packet)
{
#ifdef HCOMM_DEBUG_INFO
    printf("Info, client RX from %s containing a message of %d bytes.\n", get_endpoint_address_str(peer), packet->header.message_size);    
#endif
    // Send a reply packet back
    hp_packet_t reply_packet;
    memset(&reply_packet, 0, sizeof(reply_packet));
    // Specify the size of the message inside the reply packet
    reply_packet.header.message_size = snprintf((char *)reply_packet.message, HP_MESSAGE_MAX_SIZE, "Reply to peer %s\r\n", get_endpoint_address_str(peer));

#ifdef HCOMM_DEBUG_INFO
    printf("Info, client TX to %s a message of %d bytes.\n", get_endpoint_address_str(peer), reply_packet.header.message_size);
#endif
    endpoint_queue_send(peer, &reply_packet);

#ifdef HCOMM_DEBUG_BANDWIDTH
    bandwidth.bytes_received += sizeof(packet->header) + packet->header.message_size;
    bandwidth.bytes_sent += sizeof(reply_packet.header) + reply_packet.header.message_size;
#endif
    return 0;
}

int connected_callback(hclient_t* cli)
{
    printf("Info, connected to %s\n", get_endpoint_address_str(&cli->server_endpoint));
    // Setup the receive callback
    cli->server_endpoint.packet_received_callback = packet_received;

    // Send a reply packet back
    hp_packet_t reply_packet;
    memset(&reply_packet, 0, sizeof(reply_packet));
    // Specify the size of the message inside the reply packet
    reply_packet.header.message_size = snprintf((char *)reply_packet.message, HP_MESSAGE_MAX_SIZE, "Saying Hello to peer %s\r\n", get_endpoint_address_str(&cli->server_endpoint));
    endpoint_queue_send(&cli->server_endpoint, &reply_packet);
    return 0;
}

int disconnected_callback(hclient_t* cli)
{
    printf("Info, disconnected \n");
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

void calculate_bandwitdh()
{
    struct timespec current_time;
    uint32_t current_time_ms = 0;
    clock_gettime(CLOCK_REALTIME, &current_time);    
    current_time_ms = current_time.tv_sec * 1000 + current_time.tv_nsec / 1000000;
    uint32_t elapsed_time_ms = current_time_ms - bandwidth.prev_time_ms;
    if ( elapsed_time_ms > BANDWIDTH_CALCULATION_INTERVAL_MS)
    {
        printf("RX bytes/sec: %d TX bytes/sec: %d \n", 
                (bandwidth.bytes_received - bandwidth.prev_bytes_received) * 1000 / elapsed_time_ms,
                (bandwidth.bytes_sent - bandwidth.prev_bytes_sent) * 1000 / elapsed_time_ms);

        bandwidth.prev_time_ms = current_time_ms;
        bandwidth.prev_bytes_received = bandwidth.bytes_received;
        bandwidth.prev_bytes_sent = bandwidth.bytes_sent;
    }
    
}

#pragma pack(push,1)
typedef struct
{
  uint8_t x1;
  uint32_t x2;
  uint8_t x3;
  uint64_t x4;
} sample_struct_t;
#pragma pack(pop)

int main(int argc, char **argv)
{
    printf("%zu\n\n", offsetof(sample_struct_t, x3));
    printf("%zu\n\n", offsetof(sample_struct_t, x4));
    printf("%zu\n\n", sizeof(sample_struct_t));

    setup_signals();

    hclient_t cli = {.server_address = argv[1],
                     .server_port = 31000,
                     .connected_callback = connected_callback,
                     .disconnected_callback = disconnected_callback };

    client_init(&cli);

    while(true)
    {
        client_periodic(&cli);
#ifdef HCOMM_DEBUG_BANDWIDTH
        calculate_bandwitdh();
#endif
    }
    return 0;
}
