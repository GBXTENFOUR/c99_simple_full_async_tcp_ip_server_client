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

#include "hcomm.h"

int prepare_packet(char *sender, char *data, hp_packet_t *packet)
{
    packet->header.message_size = snprintf((char *)packet->message, HP_MESSAGE_MAX_SIZE, "%s", data);
    return 0;
}

int print_packet(hp_packet_t *packet)
{
    printf("Message: \"%s\"\n", packet->message);
    return 0;
}

int create_packet_queue(packet_queue_t *queue, int queue_size)
{
    queue->data = calloc(queue_size, sizeof(hp_packet_t));
    queue->size = queue_size;
    queue->index = 0;

    return 0;
}

void delete_packet_queue(packet_queue_t *queue)
{
    free(queue->data);
    queue->data = NULL;
}

int enqueue(packet_queue_t *queue, hp_packet_t *packet)
{
    if (queue->index == queue->size)
        return -1;

    memcpy(&queue->data[queue->index], packet, sizeof(hp_packet_t));
    queue->index++;

    return 0;
}

int dequeue(packet_queue_t *queue, hp_packet_t *packet)
{
    if (queue->index == 0)
        return -1;

    memcpy(packet, &queue->data[queue->index - 1], sizeof(hp_packet_t));
    queue->index--;

    return 0;
}

int dequeue_all(packet_queue_t *queue)
{
    queue->index = 0;
    return 0;
}

int delete_endpoint(endpoint_t *endpoint)
{
    close(endpoint->socket);
    endpoint->socket = NO_SOCKET;
    delete_packet_queue(&endpoint->send_queue);
    return 0;
}

int create_endpoint(endpoint_t *endpoint)
{
    create_packet_queue(&endpoint->send_queue, PACKET_QUEUE_SIZE);

    endpoint->send_packet_index = -1;
    endpoint->receive_packet_index = 0;

    return 0;
}

char *get_endpoint_address_str(endpoint_t *endpoint)
{
    static char ret[INET_ADDRSTRLEN + 10];
    char endpoint_ipv4_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &endpoint->address.sin_addr, endpoint_ipv4_str, INET_ADDRSTRLEN);
    sprintf(ret, "%s:%d", endpoint_ipv4_str, endpoint->address.sin_port);
    return ret;
}

char* get_address_str(struct sockaddr_in* addr)
{
    static char ret[INET_ADDRSTRLEN + 10];
    char endpoint_ipv4_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr->sin_addr, endpoint_ipv4_str, INET_ADDRSTRLEN);
    sprintf(ret, "%s:%d", endpoint_ipv4_str, addr->sin_port);
    return ret;
}

int endpoint_queue_send(endpoint_t *endpoint, hp_packet_t *packet)
{
    return enqueue(&endpoint->send_queue, packet);
}

int receive_bytes_from_endpoint(endpoint_t *endpoint)
{
    // printf("Info, Ready to receive %d bytes from %s.\n", sizeof(endpoint->received_packet.header), get_endpoint_address_str(endpoint));

    ssize_t received_count = 0;
    size_t received_total = 0;
    do
    {
        // If the header was completely received move onto receiving the actual message payload 
        if (endpoint->receive_packet_index == endpoint->bytes_expected_to_receive)
        {
            return endpoint->bytes_expected_to_receive;
        }

        // Count bytes left to receive
        size_t left_to_receive = endpoint->bytes_expected_to_receive - endpoint->receive_packet_index;
        if (left_to_receive > HP_MAX_PACKET_SIZE - endpoint->receive_packet_index)
        {
#ifdef HCOMM_DEBUG_ERROR
            printf("Error, bytes left to receive %d > %d room left in the packet length truncating message to fit\n",
                left_to_receive, HP_MAX_PACKET_SIZE - endpoint->receive_packet_index);
#endif
            left_to_receive = HP_MAX_PACKET_SIZE - endpoint->receive_packet_index;
        }
#ifdef HCOM_DEBUG_VERBOSE
        printf("Info, Let's try to receive %zd bytes...\n", left_to_receive);
#endif        
        received_count = recv(endpoint->socket, (char *)endpoint->received_packet_address + endpoint->receive_packet_index, left_to_receive, MSG_DONTWAIT);
        if (received_count < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
#ifdef HCOM_DEBUG_VERBOSE
                printf("Info, endpoint is not ready, try again later.\n");
#endif
                return 0;
            }
            else
            {
#ifdef HCOMM_DEBUG_ERROR
                printf("Error, recv from endpoint error: %d\n", errno);
#endif
                return HP_SOCKET_READ_ERROR;
            }
        }
        else if (received_count == 0)
        {
#ifdef HCOM_DEBUG_VERBOSE
            printf("Info, recv 0 bytes. Peer gracefully shutdown.\n");
#endif
            return HP_SOCKET_ZERO_READ;
        }
        else
        {
            endpoint->receive_packet_index += received_count;
            received_total += received_count;
#ifdef HCOM_DEBUG_VERBOSE
            printf("Info, recv %zd bytes\n", received_count);
#endif
        }
    } while (received_count > 0);
#ifdef HCOM_DEBUG_VERBOSE
    printf("Info, Total recv %zu bytes.\n", received_total);
#endif
    return received_total;
}

/* Receive packet from endpoint and handle it with packet_handler(). */
int receive_from_endpoint(endpoint_t *endpoint)
{    
    int received_total = 0;
    switch(endpoint->receiving_state)
    {
        case RECEIVING_NONE:
            endpoint->receiving_state = RECEIVING_HEADER;
            endpoint->bytes_expected_to_receive = sizeof(endpoint->received_packet.header);
            endpoint->receive_packet_index = 0;
            endpoint->received_packet_address = endpoint->received_packet.raw;
            endpoint->receive_error = HP_ENOERR;
#ifdef HCOM_DEBUG_VERBOSE
            printf("Info, Ready to receive header from %s \n", get_endpoint_address_str(endpoint));
#endif
            // Note: Intentional fallthrough
        case RECEIVING_HEADER:
            received_total = receive_bytes_from_endpoint(endpoint);
            if (received_total < 0)
            {
                endpoint->receiving_state = RECEIVING_DONE;
                endpoint->receive_error = received_total;            
            }
            else if (received_total == sizeof(endpoint->received_packet.header))
            {
                if (endpoint->received_packet.header.message_size > HP_MAX_PACKET_SIZE - sizeof(endpoint->received_packet.header))
                {
#ifdef HCOMM_DEBUG_ERROR
                    printf("Error, Received a header with invalid message size of %d bytes from %s \n", 
                        endpoint->received_packet.header.message_size, 
                        get_endpoint_address_str(endpoint));
#endif

                    endpoint->receive_error = HP_EMSGSIZE;
                    endpoint->receiving_state = RECEIVING_DONE;
                }
                else
                {
                    endpoint->receiving_state = RECEIVING_MESSAGE;
                    endpoint->bytes_expected_to_receive = endpoint->received_packet.header.message_size;
                    endpoint->received_packet_address += sizeof(endpoint->received_packet.header);
                    endpoint->receive_packet_index = 0;
#ifdef HCOM_DEBUG_VERBOSE
                    printf("Info, Received header from %s next, receiving message of %d bytes\n", 
                        get_endpoint_address_str(endpoint), 
                        endpoint->bytes_expected_to_receive);
#endif
                }
            }
            break;
        case RECEIVING_MESSAGE:
            received_total = receive_bytes_from_endpoint(endpoint);
            if (received_total < 0)
            {
                endpoint->receiving_state = RECEIVING_DONE;
                endpoint->receive_error = received_total;            
            }
            else if (received_total == endpoint->bytes_expected_to_receive)
            {
                endpoint->receiving_state = RECEIVING_DONE;                
                endpoint->receive_error = HP_ENOERR;
#ifdef HCOM_DEBUG_VERBOSE
                printf("Info, Received message of %d bytes from %s\n",                         
                        endpoint->bytes_expected_to_receive,
                        get_endpoint_address_str(endpoint));
#endif
                endpoint->packet_received_callback(endpoint, &endpoint->received_packet);

            }
            break;
        case RECEIVING_DONE:
            endpoint->receiving_state = RECEIVING_NONE;
            break;
    }
    return received_total;
}

int send_to_endpoint(endpoint_t *endpoint)
{
#ifdef HCOM_DEBUG_VERBOSE
    printf("Info, Sending to %s\n", get_endpoint_address_str(endpoint));
#endif

    size_t bytes_to_send = 0;
    ssize_t sent_count = 0;
    size_t sent_total = 0;
    do
    {
        // If the index packet was completely sent and there are packets in queue, send them
        if (endpoint->send_packet_index < 0 || endpoint->send_packet_index == sizeof(endpoint->send_packet.header) + endpoint->send_packet.header.message_size )
        {
#ifdef HCOM_DEBUG_VERBOSE
            printf("Info, There are no pending packets to send, maybe we can find one in the queue... \n");
#endif
            if (dequeue(&endpoint->send_queue, &endpoint->send_packet) != 0)
            {
                endpoint->send_packet_index = -1;
#ifdef HCOM_DEBUG_VERBOSE
                printf("Info, There is nothing to send anymore.\n");
#endif
                break;
            }
#ifdef HCOM_DEBUG_VERBOSE
            printf("Info, popped a packet from the queue and we'll send it.\n");
#endif
            endpoint->send_packet_index = 0;
        }

        // Count bytes to send.
        bytes_to_send = sizeof(endpoint->send_packet.header) + endpoint->send_packet.header.message_size - endpoint->send_packet_index;
        if (bytes_to_send > HP_MAX_PACKET_SIZE)
        {
#ifdef HCOMM_DEBUG_ERROR
            printf("Error, bytes to send %d > max packet length %d, truncating message,\n", bytes_to_send, HP_MAX_PACKET_SIZE);
#endif
            bytes_to_send = HP_MAX_PACKET_SIZE;
        }
#ifdef HCOM_DEBUG_VERBOSE
        printf("Info, Let's try to send %zd bytes...\n", bytes_to_send);
#endif
        sent_count = send(endpoint->socket, (char *)&endpoint->send_packet + endpoint->send_packet_index, bytes_to_send, 0);
        if (sent_count < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
#ifdef HCOM_DEBUG_VERBOSE
                printf("Info, the endpoint is not ready, try again later.\n");
#endif
                return 0;
            }
            else
            {
#ifdef HCOMM_DEBUG_ERROR
                printf("Error, send to endpoint error: %d\n", errno);
#endif
                return HP_SOCKET_WRITE_ERROR;
            }
        }
        // We have sent as many as possible
        else if (sent_count == 0)
        {
#ifdef HCOM_DEBUG_VERBOSE
            printf("Info, sent 0 bytes. Endpoint can't accept data right now. Try again later.\n");
#endif
            break;
        }
        else
        {
            endpoint->send_packet_index += sent_count;
            sent_total += sent_count;
#ifdef HCOM_DEBUG_VERBOSE
            printf("Info, sent %zd bytes.\n", sent_count);
#endif
        }
    } while (sent_count > 0);
#ifdef HCOM_DEBUG_VERBOSE
    printf("Info, Total sent %zu bytes.\n", sent_total);
#endif
    return sent_total;
}

