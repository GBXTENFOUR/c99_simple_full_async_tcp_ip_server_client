#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// #define HCOM_DEBUG_VERBOSE
#define HCOMM_DEBUG_ERROR
//#define HCOMM_DEBUG_INFO

typedef enum
{
    HP_SOCKET_WRITE_ERROR = -3,         /*!< Writing to a socket returned an error other than EAGAIN or EWOULDBLOCK */
    HP_SOCKET_ZERO_READ = -2,           /*!< Reading from a socket returned zero */
    HP_SOCKET_READ_ERROR = -1,          /*!< Reading from a socket returned an error other than EAGAIN or EWOULDBLOCK */
    HP_ENOERR = 0,                      /*!< No error. */
    HP_EMSGSIZE = 1,                    /*!< Message too long */
    HP_EINVAL = 2,                      /*!< Illegal argument. */
    HP_EPORTERR = 3,                    /*!< Porting layer error. */
    HP_ENORES = 4,                      /*!< Insufficient resources. */
    HP_EIO = 5,                         /*!< I/O error. */
    HP_EILLSTATE = 6,                   /*!< Protocol stack in illegal state. */
    HP_EAGAIN = 7,                      /*!< Retry I/O operation. */
    HP_ETIMEDOUT = 8,                   /*!< Timeout error occurred. */
    HP_EX_REPLY_MSG_EXPECTED = 10,      /*!< Reply message type expected. */
    HP_EX_INVALID_MSG_SIZE = 11,        /*!< Invalid message size. */
} HP_ERROR;


#define HP_MAX_PACKET_SIZE           ( 1024 )                                     /*!< Maximum size of a packet.  */
#define HP_PACKET_HEADER_SIZE        ( 8 )                                        /*!< Size of a packet header.   */
#define HP_PACKET_PAYLOAD_OFF        ( HP_PACKET_HEADER_SIZE )                    /*!< Offset of payload within the packat. */
#define HP_MESSAGE_MAX_SIZE          ( HP_MAX_PACKET_SIZE -  HP_PACKET_HEADER_SIZE)   /*!< Maximum size of a packet.  */

#define PACKET_QUEUE_SIZE           (100)

typedef enum
{
    HP_MSG_CMD = 0,
    HP_MSG_REPLY = 1
} hp_message_type;

typedef union
{	
	struct
	{
		uint8_t version;
		uint8_t message_type; // hp_message_type
		uint16_t message_size;
		uint32_t stamp;
	};
    uint8_t raw[HP_PACKET_HEADER_SIZE];
} hp_packet_header;

typedef union
{
	uint8_t raw[HP_MAX_PACKET_SIZE];
	struct 
	{
		hp_packet_header header;
    uint8_t message[HP_MAX_PACKET_SIZE - HP_PACKET_HEADER_SIZE];
	};
} hp_packet_t;

// packet queue --------------------------------------------------------------

typedef struct
{
  int size;
  hp_packet_t *data;
  int index;
} packet_queue_t;

typedef enum
{
  RECEIVING_NONE = 0,
  RECEIVING_HEADER = 1,
  RECEIVING_MESSAGE,  
  RECEIVING_DONE
} receiving_state;

// endpoint -----------------------------------------------------------------------
struct endpoint_t;
typedef struct endpoint_t endpoint_t;
typedef int (*packet_received_callback_t)(endpoint_t* peer, hp_packet_t *);

struct endpoint_t
{
  int socket;
  struct sockaddr_in address;
  // Packets waiting to be sent
  packet_queue_t send_queue;
  // Buffered sending packet. In case we doesn't send whole packet per one call send().
  // And sending_index is a pointer to the part of data that will be send next call.
  hp_packet_t send_packet;
  int send_packet_index;
  // The same for the receiving packet.
  hp_packet_t received_packet;
  uint8_t* received_packet_address;
  int receive_packet_index;
  packet_received_callback_t packet_received_callback;
  receiving_state receiving_state;
  HP_ERROR receive_error;
  uint16_t bytes_expected_to_receive;
};

int delete_endpoint(endpoint_t *endpoint);
int create_endpoint(endpoint_t *endpoint);
int print_packet(hp_packet_t *packet);
int receive_from_endpoint(endpoint_t *endpoint);
int send_to_endpoint(endpoint_t *endpoint);
char *get_endpoint_address_str(endpoint_t *endpoint);
char* get_address_str(struct sockaddr_in* addr);
int dequeue_all(packet_queue_t *queue);
int endpoint_queue_send(endpoint_t *endpoint, hp_packet_t *packet);
int prepare_packet(char *sender, char *data, hp_packet_t *packet);
int read_from_stdin(char *read_buffer, size_t max_len);

#define MAX_CLIENTS 10
#define NO_SOCKET -1
#define LISTEN_MAX 32

// Forward declarations
struct hserver_t;
typedef struct hserver_t hserver_t;

typedef int (*client_callback_t)(hserver_t* svr, int i);

struct hserver_t
{ 
  int listen_sock; 
  uint16_t listen_port;
  struct sockaddr_in svr_addr;
  endpoint_t client_list[MAX_CLIENTS];
  fd_set read_fds;
  fd_set write_fds;
  fd_set error_fds;
  bool initialized;
  client_callback_t client_connected_callback;
  client_callback_t client_disconnected_callback;
};

int server_init(hserver_t* svr);
int server_periodic(hserver_t* svr);
int server_queue_send_packet(hserver_t* svr, hp_packet_t* new_packet);

typedef enum
{
	CONNECTION_STATE_DISCONNECTED=0,
	CONNECTION_STATE_INPROGRESS,
	CONNECTION_STATE_CONNECTED
} connection_state_t;

struct hclient_t;
typedef struct hclient_t hclient_t;
typedef int (*connection_callback_t)(hclient_t* cli);

struct hclient_t
{
  char* server_address;
  uint16_t server_port;  
  endpoint_t server_endpoint;  
  connection_state_t connection_state;
  fd_set read_fds;
  fd_set write_fds;
  fd_set error_fds;
  connection_callback_t connected_callback;
  connection_callback_t disconnected_callback;
};

int client_init(hclient_t *cli);
int client_periodic(hclient_t *cli);
#endif /* COMMON_H */
