#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <termios.h>
#include <sys/select.h>

#define MAX_MESSAGE_LENGTH 1048576
#define SCHEME_REPL_UUID "611a1a1a-94ba-11f0-b0a8-5f754c08f133"
#define CACHE_DIR ".cache/chb"
#define CACHE_FILE "mac-address.txt"

#define MSG_TYPE_EXPRESSION 0x00
#define MSG_TYPE_INTERRUPT 0x01

#define SERVICE_NAME "CHB"

typedef struct {
  char *message;
  enum { MSG_EXPRESSION, MSG_INTERRUPT, MSG_QUIT } type;
} message_t;

static pthread_t input_thread_id;
static message_t *pending_message = NULL;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t response_cond = PTHREAD_COND_INITIALIZER;
static volatile sig_atomic_t interrupt_pending = 0;
static int signal_pipe[2] = { -1, -1 };

bool check_address_for_scheme_repl(const char *address);
char *get_cache_file_path();
void *input_thread(void *arg);
char *load_cached_address();
char *receive_message(int sock);
char *receive_message_with_signal_check(int sock);
void save_cached_address(const char *address);
char *scan_known_addresses();
int send_expression_message(int sock, const char *message);
int send_interrupt_message(int sock);
void sigint_handler(int sig);

char *get_cache_file_path()
{
  const char *home = getenv("HOME");

  if (!home) {
    return NULL;
  }

  char *path =
      malloc(strlen(home) + strlen(CACHE_DIR) + strlen(CACHE_FILE) + 3);

  if (!path) {
    return NULL;
  }

  sprintf(path, "%s/%s/%s", home, CACHE_DIR, CACHE_FILE);
  return path;
}

char *load_cached_address()
{
  char *cache_path = get_cache_file_path();

  if (!cache_path) {
    return NULL;
  }

  FILE *file = fopen(cache_path, "r");

  free(cache_path);

  if (!file) {
    return NULL;
  }

  char *address = malloc(19);	// AA:BB:CC:DD:EE:FF + null terminator

  if (!address) {
    fclose(file);
    return NULL;
  }

  if (fgets(address, 19, file) == NULL) {
    free(address);
    fclose(file);
    return NULL;
  }

  fclose(file);

  address[17] = '\0';

  if (strlen(address) != 17) {
    free(address);
    return NULL;
  }

  return address;
}

void save_cached_address(const char *address)
{
  if (!address) {
    return;
  }

  char *cache_path = get_cache_file_path();

  if (!cache_path) {
    return;
  }

  const char *home = getenv("HOME");

  if (home) {
    char *cache_dir = malloc(strlen(home) + strlen(CACHE_DIR) + 2);

    if (cache_dir) {
      sprintf(cache_dir, "%s/%s", home, CACHE_DIR);
      if (mkdir(cache_dir, 0755) != 0 && errno != EEXIST) {
	perror("Failed to create cache directory.");
	free(cache_dir);
	return;
      }
      free(cache_dir);
    }
  }

  FILE *file = fopen(cache_path, "w");

  free(cache_path);

  if (!file) {
    return;
  }

  fprintf(file, "%s\n", address);
  fclose(file);
}

bool check_address_for_scheme_repl(const char *address)
{
  printf("Checking cached address %s.\n", address);
  fflush(stdout);

  bdaddr_t target;

  str2ba(address, &target);

  sdp_session_t *session = sdp_connect(BDADDR_ANY, &target, SDP_RETRY_IF_BUSY);

  if (!session) {
    printf("Connection failed.\n");
    return false;
  }

  uuid_t rfcomm_uuid;

  sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);

  sdp_list_t *search_list = sdp_list_append(NULL, &rfcomm_uuid);
  uint32_t range = 0x0000ffff;
  sdp_list_t *attr_list = sdp_list_append(NULL, &range);
  sdp_list_t *rsp_list = NULL;

  int result = sdp_service_search_attr_req(session, search_list,
					   SDP_ATTR_REQ_RANGE, attr_list,
					   &rsp_list);

  bool found_scheme_repl = false;

  if (result == 0) {
    sdp_list_t *r = rsp_list;

    for (; r && !found_scheme_repl; r = r->next) {
      sdp_record_t *rec = (sdp_record_t *) r->data;
      sdp_data_t *service_name = sdp_data_get(rec, SDP_ATTR_SVCNAME_PRIMARY);

      if (service_name && service_name->dtd == SDP_TEXT_STR8) {
	if (strstr(service_name->val.str, SERVICE_NAME)) {
	  found_scheme_repl = true;
	}
      }
    }
  }

  if (search_list)
    sdp_list_free(search_list, 0);
  if (attr_list)
    sdp_list_free(attr_list, 0);
  if (rsp_list)
    sdp_list_free(rsp_list, 0);
  sdp_close(session);

  if (found_scheme_repl) {
    printf("CHB service found.\n");
    return true;
  } else {
    printf("No CHB service found\n");
    return false;
  }
}

void sigint_handler(int sig)
{
  if (sig == SIGINT) {
    char byte = 1;

    write(signal_pipe[1], &byte, 1);
    interrupt_pending = 1;
  }
}

int send_expression_message(int sock, const char *message)
{
  size_t len = strlen(message);
  uint32_t network_len = htonl(len);
  uint8_t msg_type = MSG_TYPE_EXPRESSION;

  if (send(sock, &msg_type, 1, 0) != 1) {
    perror("Failed to send message type.");
    return -1;
  }

  if (send(sock, &network_len, 4, 0) != 4) {
    perror("Failed to send length.");
    return -1;
  }

  if (send(sock, message, len, 0) != (ssize_t) len) {
    perror("Failed to send message.");
    return -1;
  }

  printf("%s\n", message);
  return 0;
}

int send_interrupt_message(int sock)
{
  uint8_t msg_type = MSG_TYPE_INTERRUPT;

  if (send(sock, &msg_type, 1, 0) != 1) {
    perror("Failed to send interrupt.");
    return -1;
  }
  return 0;
}

char *receive_message_with_interrupt_check(int sock)
{
  while (1) {
    fd_set readfds;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    FD_SET(signal_pipe[0], &readfds);

    int max_fd = (sock > signal_pipe[0]) ? sock : signal_pipe[0];
    int select_result = select(max_fd + 1, &readfds, NULL, NULL, NULL);

    if (select_result < 0) {
      if (errno == EINTR)
	continue;
      perror("Select failed.");
      return NULL;
    }

    if (FD_ISSET(signal_pipe[0], &readfds)) {
      char byte;

      read(signal_pipe[0], &byte, 1);
      send_interrupt_message(sock);
      interrupt_pending = 0;

      char *interrupt_response = receive_message(sock);

      if (interrupt_response) {
	printf("⇒ %s\n", interrupt_response);
	free(interrupt_response);
      }
      printf("scheme> ");
      fflush(stdout);

      continue;
    }

    if (FD_ISSET(sock, &readfds)) {
      return receive_message(sock);
    }
  }
}

char *receive_message(int sock)
{
  uint32_t network_len;

  ssize_t bytes_received = 0;

  while (bytes_received < 4) {
    ssize_t result =
	recv(sock, ((char *)&network_len) + bytes_received, 4 - bytes_received,
	     0);

    if (result <= 0) {
      if (result < 0)
	perror("Failed to receive length.");
      return NULL;
    }
    bytes_received += result;
  }

  uint32_t len = ntohl(network_len);

  if (len > MAX_MESSAGE_LENGTH) {
    fprintf(stderr, "Message too long: %u bytes\n", len);
    return NULL;
  }

  char *buffer = malloc(len + 1);

  if (!buffer) {
    perror("Failed to allocate buffer.");
    return NULL;
  }

  bytes_received = 0;
  while (bytes_received < (ssize_t) len) {
    ssize_t result =
	recv(sock, buffer + bytes_received, len - bytes_received, 0);
    if (result <= 0) {
      if (result < 0)
	perror("Failed to receive message.");
      free(buffer);
      return NULL;
    }
    bytes_received += result;
  }

  buffer[len] = '\0';
  return buffer;
}

int find_service_channel(const char *bt_addr)
{
  uuid_t uuid;
  sdp_session_t *session;
  sdp_list_t *search_list;
  sdp_list_t *attr_list;
  sdp_list_t *rsp_list = NULL;
  bdaddr_t target;
  int channel = -1;

  str2ba(bt_addr, &target);

  // Parse UUID string manually.
  uint8_t uuid_bytes[16];
  unsigned int u[16];

  if (sscanf
      (SCHEME_REPL_UUID,
       "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
       &u[0], &u[1], &u[2], &u[3], &u[4], &u[5], &u[6], &u[7], &u[8], &u[9],
       &u[10], &u[11], &u[12], &u[13], &u[14], &u[15]) != 16) {
    fprintf(stderr, "Invalid UUID format.\n");
    return -1;
  }
  for (int i = 0; i < 16; i++) {
    uuid_bytes[i] = (uint8_t) u[i];
  }
  sdp_uuid128_create(&uuid, uuid_bytes);

  session = sdp_connect(BDADDR_ANY, &target, SDP_RETRY_IF_BUSY);
  if (!session) {
    perror("Failed to connect to SDP server.");
    return -1;
  }

  search_list = sdp_list_append(NULL, &uuid);
  uint32_t range = 0x0000ffff;

  attr_list = sdp_list_append(NULL, &range);

  int result = sdp_service_search_attr_req(session, search_list,
					   SDP_ATTR_REQ_RANGE, attr_list,
					   &rsp_list);

  if (result == 0) {
    sdp_list_t *r = rsp_list;

    for (; r; r = r->next) {
      sdp_record_t *rec = (sdp_record_t *) r->data;
      sdp_list_t *proto_list;

      if (sdp_get_access_protos(rec, &proto_list) == 0) {
	sdp_list_t *p = proto_list;

	for (; p; p = p->next) {
	  sdp_list_t *pds = (sdp_list_t *) p->data;

	  for (; pds; pds = pds->next) {
	    sdp_data_t *d = (sdp_data_t *) pds->data;
	    int proto = 0;

	    for (; d; d = d->next) {
	      switch (d->dtd) {
	      case SDP_UUID16:
	      case SDP_UUID32:
	      case SDP_UUID128:
		proto = sdp_uuid_to_proto(&d->val.uuid);
		break;
	      case SDP_UINT8:
		if (proto == RFCOMM_UUID) {
		  channel = d->val.uint8;
		  printf("Found service on RFCOMM channel %d.\n", channel);
		  goto cleanup;
		}
		break;
	      }
	    }
	  }
	}
	if (proto_list)
	  sdp_list_free(proto_list, 0);
      }
    }
  }

 cleanup:
  if (search_list)
    sdp_list_free(search_list, 0);
  if (attr_list)
    sdp_list_free(attr_list, 0);
  if (rsp_list)
    sdp_list_free(rsp_list, 0);
  sdp_close(session);

  return channel;
}

char *scan_paired_devices()
{
  printf("Scanning all paired and connected Bluetooth devices for CHB service.\n");

  int dev_id = hci_get_route(NULL);

  if (dev_id < 0) {
    perror("No Bluetooth adapter found.");
    return NULL;
  }

  int sock = hci_open_dev(dev_id);

  if (sock < 0) {
    perror("Failed to open HCI device.");
    return NULL;
  }

  struct hci_conn_list_req *cl;
  struct hci_conn_info *ci;
  int max_conn = 20;

  cl = malloc(max_conn * sizeof(*ci) + sizeof(*cl));
  if (!cl) {
    close(sock);
    return NULL;
  }

  cl->dev_id = dev_id;
  cl->conn_num = max_conn;
  ci = cl->conn_info;

  if (ioctl(sock, HCIGETCONNLIST, (void *)cl) < 0) {
    printf("Could not get active connections.  Scanning paired devices only.\n");
    free(cl);
    close(sock);
    return NULL;
  }

  printf("Found %d active connections.  Checking for CHB.\n", cl->conn_num);

  for (int i = 0; i < cl->conn_num; i++) {
    char addr_str[19];

    ba2str(&ci[i].bdaddr, addr_str);

    printf("Checking %s.\n", addr_str);
    fflush(stdout);

    sdp_session_t *session =
	sdp_connect(BDADDR_ANY, &ci[i].bdaddr, SDP_RETRY_IF_BUSY);
    if (!session) {
      printf("SDP connection failed.\n");
      continue;
    }

    uuid_t rfcomm_uuid;

    sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
    sdp_list_t *search_list = sdp_list_append(NULL, &rfcomm_uuid);
    uint32_t range = 0x0000ffff;
    sdp_list_t *attr_list = sdp_list_append(NULL, &range);
    sdp_list_t *rsp_list = NULL;

    int result = sdp_service_search_attr_req(session, search_list,
					     SDP_ATTR_REQ_RANGE, attr_list,
					     &rsp_list);

    bool found_scheme_repl = false;

    if (result == 0) {
      sdp_list_t *r = rsp_list;

      for (; r && !found_scheme_repl; r = r->next) {
	sdp_record_t *rec = (sdp_record_t *) r->data;
	sdp_data_t *service_name = sdp_data_get(rec, SDP_ATTR_SVCNAME_PRIMARY);

	if (service_name && service_name->dtd == SDP_TEXT_STR8) {
	  if (strstr(service_name->val.str, SERVICE_NAME)) {
	    found_scheme_repl = true;
	  }
	}
      }
    }

    if (search_list)
      sdp_list_free(search_list, 0);
    if (attr_list)
      sdp_list_free(attr_list, 0);
    if (rsp_list)
      sdp_list_free(rsp_list, 0);
    sdp_close(session);

    if (found_scheme_repl) {
      printf("CHB service found.\n");
      char *result_addr = malloc(19);

      strcpy(result_addr, addr_str);
      free(cl);
      close(sock);
      return result_addr;
    } else {
      printf("No CHB service.\n");
    }
  }

  free(cl);
  close(sock);
  return NULL;
}

char *scan_known_addresses()
{
  printf("Trying known address patterns.\n");

  const char *known_addresses[] = {
    "B0:D5:FB:99:14:B0",	// Your device
    NULL
  };

  for (int i = 0; known_addresses[i] != NULL; i++) {
    printf("Checking %s.\n", known_addresses[i]);
    fflush(stdout);

    bdaddr_t target;

    str2ba(known_addresses[i], &target);

    sdp_session_t *session =
	sdp_connect(BDADDR_ANY, &target, SDP_RETRY_IF_BUSY);
    if (!session) {
      printf("Connection failed.\n");
      continue;
    }

    uuid_t rfcomm_uuid;

    sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
    sdp_list_t *search_list = sdp_list_append(NULL, &rfcomm_uuid);
    uint32_t range = 0x0000ffff;
    sdp_list_t *attr_list = sdp_list_append(NULL, &range);
    sdp_list_t *rsp_list = NULL;

    int result = sdp_service_search_attr_req(session, search_list,
					     SDP_ATTR_REQ_RANGE, attr_list,
					     &rsp_list);

    bool found_scheme_repl = false;

    if (result == 0) {
      sdp_list_t *r = rsp_list;

      for (; r && !found_scheme_repl; r = r->next) {
	sdp_record_t *rec = (sdp_record_t *) r->data;
	sdp_data_t *service_name = sdp_data_get(rec, SDP_ATTR_SVCNAME_PRIMARY);

	if (service_name && service_name->dtd == SDP_TEXT_STR8) {
	  if (strstr(service_name->val.str, SERVICE_NAME)) {
	    found_scheme_repl = true;
	  }
	}
      }
    }

    if (search_list)
      sdp_list_free(search_list, 0);
    if (attr_list)
      sdp_list_free(attr_list, 0);
    if (rsp_list)
      sdp_list_free(rsp_list, 0);
    sdp_close(session);

    if (found_scheme_repl) {
      printf("CHB service found.\n");
      char *result_addr = malloc(19);

      strcpy(result_addr, known_addresses[i]);
      return result_addr;
    } else {
      printf("No CHB service found.\n");
    }
  }

  return NULL;
}

int main(int argc, char *argv[])
{
  const char *bt_addr = NULL;

  if (argc == 1) {
    char *cached_addr = load_cached_address();
    char *discovered_addr = NULL;

    if (cached_addr) {
      if (check_address_for_scheme_repl(cached_addr)) {
	discovered_addr = cached_addr;
	printf("Using cached device: %s.\n", discovered_addr);
      } else {
	free(cached_addr);
	cached_addr = NULL;
      }
    }

    if (!discovered_addr) {
      printf("No cached address or cached address failed.  Scanning devices.\n");
      discovered_addr = scan_paired_devices();
      if (!discovered_addr) {
	fprintf(stderr, "No CHB service found\n");
	fprintf(stderr, "Usage: %s [bluetooth_address]\n", argv[0]);
	fprintf(stderr, "Example: %s AA:BB:CC:DD:EE:FF\n", argv[0]);
	return 1;
      }
      printf("Using discovered device: %s\n", discovered_addr);
      save_cached_address(discovered_addr);
    }

    bt_addr = discovered_addr;
  } else if (argc == 2) {
    bt_addr = argv[1];
    save_cached_address(bt_addr);
  } else {
    fprintf(stderr, "Usage: %s [bluetooth_address]\n", argv[0]);
    fprintf(stderr, "Example: %s AA:BB:CC:DD:EE:FF\n", argv[0]);
    fprintf(stderr, "If no address provided, will auto-discover\n");
    return 1;
  }

  printf("Searching for service with UUID %s.\n", SCHEME_REPL_UUID);
  int port = find_service_channel(bt_addr);

  if (port < 0) {
    fprintf(stderr, "Service not found\n");
    return 1;
  }

  int sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

  if (sock < 0) {
    perror("Failed to create socket.");
    return 1;
  }
  // No socket timeout.

  struct sockaddr_rc addr = { 0 };
  addr.rc_family = AF_BLUETOOTH;
  addr.rc_channel = port;
  str2ba(bt_addr, &addr.rc_bdaddr);

  printf("Connecting to %s on channel %d.\n", bt_addr, port);
  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Failed to connect.");
    close(sock);
    return 1;
  }

  printf("Connected! Starting REPL session.\n");
  printf ("Type Scheme expressions (or 'quit' to exit).");
  printf("  Press Ctrl-C to interrupt long-running evaluations.\n\n");

  if (pipe(signal_pipe) < 0) {
    perror("Failed to create signal pipe.");
    close(sock);
    return 1;
  }

  struct sigaction sa;

  sa.sa_handler = sigint_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGINT, &sa, NULL) < 0) {
    perror("Failed to set signal handler.");
    close(sock);
    return 1;
  }
  if (pthread_create(&input_thread_id, NULL, input_thread, &sock) != 0) {
    perror("Failed to create input thread.");
    close(sock);
    return 1;
  }
  while (1) {
    pthread_mutex_lock(&queue_mutex);
    while (!pending_message) {
      pthread_cond_wait(&queue_cond, &queue_mutex);
    }

    message_t *msg = pending_message;

    pending_message = NULL;
    pthread_mutex_unlock(&queue_mutex);
    if (msg->type == MSG_QUIT) {
      free(msg->message);
      free(msg);
      break;
    }
    if (msg->type == MSG_EXPRESSION) {
      send_expression_message(sock, msg->message);
    }

    char *result = receive_message_with_interrupt_check(sock);

    if (!result) {
      free(msg->message);
      free(msg);
      break;
    }
    printf("⇒ %s\n\n", result);
    fflush(stdout);
    free(result);
    free(msg->message);
    free(msg);
    pthread_mutex_lock(&queue_mutex);
    pthread_cond_signal(&response_cond);
    pthread_mutex_unlock(&queue_mutex);
  }

  pthread_join(input_thread_id, NULL);
  close(sock);
  close(signal_pipe[0]);
  close(signal_pipe[1]);
  printf("Connection closed.\n");
  if (argc == 1 && bt_addr) {
    free((char *)bt_addr);
  }
  return 0;
}

void *input_thread(void *arg)
{
  int sock = *(int *)arg;
  char input[1024];
  bool stdin_is_terminal = isatty(STDIN_FILENO);

  while (1) {
    if (stdin_is_terminal) {
      printf("scheme> ");
      fflush(stdout);
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t nread = getline(&line, &len, stdin);

    if (nread == -1) {
      if (line)
	free(line);
      break;
    }
    strncpy(input, line, sizeof(input) - 1);
    input[sizeof(input) - 1] = '\0';
    free(line);

    size_t input_len = strlen(input);

    if (input_len > 0 && input[input_len - 1] == '\n') {
      input[input_len - 1] = '\0';
    }
    if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0
	|| strcmp(input, ":q") == 0) {
      break;
    }
    if (strlen(input) == 0) {
      continue;
    }

    message_t *msg = malloc(sizeof(message_t));

    msg->message = strdup(input);
    msg->type = MSG_EXPRESSION;
    pthread_mutex_lock(&queue_mutex);
    pending_message = msg;
    pthread_cond_signal(&queue_cond);
    pthread_cond_wait(&response_cond, &queue_mutex);
    pthread_mutex_unlock(&queue_mutex);
    if (!stdin_is_terminal) {
      break;
    }
  }

  message_t *quit_msg = malloc(sizeof(message_t));

  quit_msg->message = NULL;
  quit_msg->type = MSG_QUIT;
  pthread_mutex_lock(&queue_mutex);
  pending_message = quit_msg;
  pthread_cond_signal(&queue_cond);
  pthread_mutex_unlock(&queue_mutex);
  return NULL;
}
