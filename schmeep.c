#include <arpa/inet.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define CACHE_DIR ".cache/schmeep"
#define CACHE_FILE "mac-address.txt"
#define CMD_A2C_EVALUATION_COMPLETE 255
#define CMD_C2A_EVALUATE 254
#define CMD_C2A_INTERRUPT 255
#define CMD_C2A_MIN_COMMAND CMD_C2A_EVALUATE
#define MAX_MESSAGE_LENGTH 1048576
#define SCHMEEP_UUID "611a1a1a-94ba-11f0-b0a8-5f754c08f133"
#define SERVICE_NAME "schmeep"

static pthread_t input_thread_id;
static pthread_t stream_thread_id;

bool check_address_for_scheme_repl(const char *address);
bool check_device_for_schmeep_service(const bdaddr_t *bdaddr);
char *get_cache_file_path();
void *input_thread(void *arg);
char *load_cached_address();
void save_cached_address(const char *address);
int send_data_block(int sock, const char *data, size_t length);
int send_evaluate_command(int sock);
int send_interrupt_command(int sock);
void protocol_handler_thread(void *arg);
void sigint_handler(int sig);

char *get_cache_file_path() {
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

char *load_cached_address() {
  char *cache_path = get_cache_file_path();

  if (!cache_path) {
    return NULL;
  }

  FILE *file = fopen(cache_path, "r");

  free(cache_path);

  if (!file) {
    return NULL;
  }

  char *address = malloc(19); // AA:BB:CC:DD:EE:FF + null terminator

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

void save_cached_address(const char *address) {
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

bool check_device_for_schmeep_service(const bdaddr_t *bdaddr) {
  sdp_session_t *session = sdp_connect(BDADDR_ANY, bdaddr, SDP_RETRY_IF_BUSY);

  if (!session) {
    return false;
  }

  uuid_t rfcomm_uuid;

  sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);

  sdp_list_t *search_list = sdp_list_append(NULL, &rfcomm_uuid);
  uint32_t range = 0x0000ffff;
  sdp_list_t *attr_list = sdp_list_append(NULL, &range);
  sdp_list_t *rsp_list = NULL;

  int result = sdp_service_search_attr_req(
      session, search_list, SDP_ATTR_REQ_RANGE, attr_list, &rsp_list);

  bool found_schmeep = false;

  if (result == 0) {
    sdp_list_t *r = rsp_list;

    for (; r && !found_schmeep; r = r->next) {
      sdp_record_t *rec = (sdp_record_t *)r->data;
      sdp_data_t *service_name = sdp_data_get(rec, SDP_ATTR_SVCNAME_PRIMARY);

      if (service_name && service_name->dtd == SDP_TEXT_STR8) {
	if (strstr(service_name->val.str, SERVICE_NAME)) {
	  found_schmeep = true;
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

  return found_schmeep;
}

bool check_address_for_scheme_repl(const char *address) {
  printf("Checking cached address %s.\n", address);
  fflush(stdout);

  bdaddr_t target;

  str2ba(address, &target);

  bool found = check_device_for_schmeep_service(&target);

  printf(found ? "Schmeep service found.\n" : "No Schmeep service found\n");
  return found;
}

static int global_sock = -1;

void sigint_handler(int sig) {
  if (sig == SIGINT && global_sock != -1) {
    send_interrupt_command(global_sock);
    printf("\n");
    fflush(stdout);
  }
}

int send_data_block(int sock, const char *data, size_t length) {
  if (length >= CMD_C2A_MIN_COMMAND) {
    fprintf(stderr, "Data block too large: %zu bytes\n", length);
    return -1;
  }

  uint8_t length_byte = (uint8_t)length;

  if (send(sock, &length_byte, 1, 0) != 1) {
    perror("Failed to send length byte.");
    return -1;
  }

  if (send(sock, data, length, 0) != (ssize_t)length) {
    perror("Failed to send data block.");
    return -1;
  }

  return 0;
}

int send_command(uint8_t command, char *message, int sock) {
  if (send(sock, &command, 1, 0) != 1) {
    perror(message);
    return -1;
  }
  return 0;
}

int send_evaluate_command(int sock) {
  send_command(CMD_C2A_EVALUATE, "Failed to send evaluate command.", sock);
}

int send_interrupt_command(int sock) {
  send_command(CMD_C2A_INTERRUPT, "Failed to send interrupt command.", sock);
}

int receive_data_block(int sock, char *buffer, int max_size) {
  unsigned char length_or_command;
  ssize_t result = recv(sock, &length_or_command, 1, 0);

  if (result <= 0) {
    return -1;
  }

  if (length_or_command == CMD_A2C_EVALUATION_COMPLETE) {
    printf("scheme> ");
    fflush(stdout);
    return 0;
  }

  if (length_or_command > max_size) {
    fprintf(stderr, "Data block too large: %d bytes\n", length_or_command);
    return -1;
  }

  int bytes_read = 0;

  while (bytes_read < length_or_command) {
    result = recv(sock, buffer + bytes_read, length_or_command - bytes_read, 0);
    if (result <= 0) {
      return -1;
    }
    bytes_read += result;
  }

  return length_or_command;
}

void protocol_handler_thread(void *arg) {
  int sock = *(int *)arg;
  char buffer[255];

  while (1) {
    int block_size = receive_data_block(sock, buffer, sizeof(buffer) - 1);

    if (block_size < 0) {
      break;
    }

    if (block_size == 0) {
      continue;
    }

    buffer[block_size] = '\0';
    printf("%s", buffer);
    fflush(stdout);
  }
}

int find_service_channel(const char *bt_addr) {
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

  if (sscanf(SCHMEEP_UUID,
	     "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%"
	     "02x",
	     &u[0], &u[1], &u[2], &u[3], &u[4], &u[5], &u[6], &u[7], &u[8],
	     &u[9], &u[10], &u[11], &u[12], &u[13], &u[14], &u[15]) != 16) {
    fprintf(stderr, "Invalid UUID format.\n");
    return -1;
  }
  for (int i = 0; i < 16; i++) {
    uuid_bytes[i] = (uint8_t)u[i];
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

  int result = sdp_service_search_attr_req(
      session, search_list, SDP_ATTR_REQ_RANGE, attr_list, &rsp_list);

  if (result == 0) {
    sdp_list_t *r = rsp_list;

    for (; r; r = r->next) {
      sdp_record_t *rec = (sdp_record_t *)r->data;
      sdp_list_t *proto_list;

      if (sdp_get_access_protos(rec, &proto_list) == 0) {
	sdp_list_t *p = proto_list;

	for (; p; p = p->next) {
	  sdp_list_t *pds = (sdp_list_t *)p->data;

	  for (; pds; pds = pds->next) {
	    sdp_data_t *d = (sdp_data_t *)pds->data;
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

char *scan_active_paired_devices() {
  printf("Scanning all paired and connected Bluetooth devices for Schmeep "
	 "service.\n");

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
    printf("Could not get active connections.\n");
    free(cl);
    close(sock);
    return NULL;
  }

  printf("Found %d active connections.  Checking for Schmeep.\n", cl->conn_num);

  for (int i = 0; i < cl->conn_num; i++) {
    char addr_str[19];

    ba2str(&ci[i].bdaddr, addr_str);
    printf("Checking %s.\n", addr_str);
    fflush(stdout);

    if (check_device_for_schmeep_service(&ci[i].bdaddr)) {
      printf("Schmeep service found.\n");

      char *result_addr = malloc(19);

      strcpy(result_addr, addr_str);
      free(cl);
      close(sock);
      return result_addr;
    } else {
      printf("No Schmeep service.\n");
    }
  }

  free(cl);
  close(sock);
  return NULL;
}

void usage(char *command) {
  fprintf(stderr, "Usage: %s [bluetooth_address]\n", command);
  fprintf(stderr, "Example: %s AA:BB:CC:DD:EE:FF\n\n", command);
  fprintf(stderr, "If no address is provided, will auto-discover.\n");
}

int main(int argc, char *argv[]) {
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
      printf("Scanning devices.\n");
      discovered_addr = scan_active_paired_devices();
      if (!discovered_addr) {
	fprintf(stderr, "No Schmeep service found.\n");
	usage(argv[0]);
	return 1;
      }
      printf("Using discovered device: %s.\n", discovered_addr);
      save_cached_address(discovered_addr);
    }
    bt_addr = discovered_addr;
  } else if (argc == 2) {
    bt_addr = argv[1];
    save_cached_address(bt_addr);
  } else {
    usage(argv[0]);
    return 1;
  }

  printf("Searching for service with UUID %s.\n", SCHMEEP_UUID);
  int port = find_service_channel(bt_addr);

  if (port < 0) {
    fprintf(stderr, "Service not found\n");
    return 1;
  }

  struct sockaddr_rc addr = {0};

  addr.rc_family = AF_BLUETOOTH;
  addr.rc_channel = port;
  str2ba(bt_addr, &addr.rc_bdaddr);

  int sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

  if (sock < 0) {
    perror("Failed to create socket.");
    return 1;
  }

  int reuse = 1;

  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    perror("Failed to set SO_REUSEADDR.");
    close(sock);
    return 1;
  }

  printf("Connecting to %s on channel %d.\n", bt_addr, port);

  int connect_attempts = 0;
  int max_attempts = 4;

  while (connect_attempts < max_attempts) {
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
      break;
    }

    if (errno == EBUSY && connect_attempts < max_attempts - 1) {
      printf("Connection busy.  Waiting for BlueZ cleanup (attempt %d/%d).\n",
	     connect_attempts + 1, max_attempts);
      close(sock);
      sleep(4);

      sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
      if (sock < 0) {
	perror("Failed to recreate socket.");
	return 1;
      }

      int reuse = 1;
      if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
	  0) {
	perror("Failed to set SO_REUSEADDR on retry.");
	close(sock);
	return 1;
      }

      connect_attempts++;
    } else {
      perror("Failed to connect.");
      close(sock);
      return 1;
    }
  }

  printf("Connected! Starting REPL session.\n");
  printf("Type Scheme expressions.");
  printf("  Press Ctrl-C to interrupt long-running evaluations.\n\n");

  struct sigaction sa;

  sa.sa_handler = sigint_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGINT, &sa, NULL) < 0) {
    perror("Failed to set signal handler.");
    close(sock);
    return 1;
  }
  global_sock = sock;

  if (pthread_create(&stream_thread_id, NULL,
		     (void *(*)(void *))protocol_handler_thread, &sock) != 0) {
    perror("Failed to create protocol handler thread.");
    close(sock);
    return 1;
  }

  printf("scheme> ");
  fflush(stdout);

  if (pthread_create(&input_thread_id, NULL, input_thread, &sock) != 0) {
    perror("Failed to create input thread.");
    close(sock);
    return 1;
  }

  pthread_join(input_thread_id, NULL);
  pthread_join(stream_thread_id, NULL);
  shutdown(sock, SHUT_RDWR);
  close(sock);
  printf("Connection closed.\n");
  if (argc == 1 && bt_addr) {
    free((char *)bt_addr);
  }
  return 0;
}

int send_expression_in_blocks(int sock, const char *expression) {
  size_t length = strlen(expression);
  size_t sent = 0;

  while (sent < length) {
    size_t remaining = length - sent;
    size_t block_size = (remaining >= CMD_C2A_MIN_COMMAND)
			    ? CMD_C2A_MIN_COMMAND - 1
			    : remaining;

    if (send_data_block(sock, expression + sent, block_size) < 0) {
      return -1;
    }
    sent += block_size;
  }

  return send_evaluate_command(sock);
}

void *input_thread(void *arg) {
  int sock = *(int *)arg;
  bool stdin_is_terminal = isatty(STDIN_FILENO);

  while (1) {

    char *line = NULL;
    size_t length = 0;
    ssize_t nread = getline(&line, &length, stdin);

    if (nread == -1) {
      if (line)
	free(line);
      break;
    }

    if (send_expression_in_blocks(sock, line) < 0) {
      fprintf(stderr, "Failed to send expression.\n");
      free(line);
      break;
    }

    free(line);

    if (!stdin_is_terminal) {
      break;
    }
  }

  return NULL;
}
