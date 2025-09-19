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

#define MAX_MESSAGE_LENGTH 1048576
#define SCHEME_REPL_UUID "611a1a1a-94ba-11f0-b0a8-5f754c08f133"
#define CACHE_DIR ".cache/chb"
#define CACHE_FILE "mac-address.txt"

#define MSG_TYPE_EXPRESSION 0x00
#define MSG_TYPE_INTERRUPT 0x01

static volatile sig_atomic_t interrupt_requested = 0;
static volatile sig_atomic_t input_complete = 0;
static volatile sig_atomic_t message_sent = 0;
static int global_sock = -1;
static pthread_mutex_t message_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t message_cond = PTHREAD_COND_INITIALIZER;

// Forward declarations
char* scan_known_addresses();
char* load_cached_address();
void save_cached_address(const char* address);
char* get_cache_file_path();
bool check_address_for_scheme_repl(const char* address);
void sigint_handler(int sig);
void* input_thread(void* arg);
int send_expression_message(int sock, const char* message);
int send_interrupt_message(int sock);

// Get the full path to the cache file
char* get_cache_file_path() {
    const char* home = getenv("HOME");
    if (!home) {
        return NULL;
    }

    char* path = malloc(strlen(home) + strlen(CACHE_DIR) + strlen(CACHE_FILE) + 3);
    if (!path) {
        return NULL;
    }

    sprintf(path, "%s/%s/%s", home, CACHE_DIR, CACHE_FILE);
    return path;
}

// Load cached MAC address if available
char* load_cached_address() {
    char* cache_path = get_cache_file_path();
    if (!cache_path) {
        return NULL;
    }

    FILE* file = fopen(cache_path, "r");
    free(cache_path);

    if (!file) {
        return NULL;
    }

    char* address = malloc(19); // AA:BB:CC:DD:EE:FF + null terminator
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

    // Remove newline if present
    size_t len = strlen(address);
    if (len > 0 && address[len-1] == '\n') {
        address[len-1] = '\0';
    }

    // Validate format (should be XX:XX:XX:XX:XX:XX)
    if (strlen(address) != 17) {
        free(address);
        return NULL;
    }

    return address;
}

// Save MAC address to cache
void save_cached_address(const char* address) {
    if (!address) {
        return;
    }

    char* cache_path = get_cache_file_path();
    if (!cache_path) {
        return;
    }

    // Create cache directory if it doesn't exist
    const char* home = getenv("HOME");
    if (home) {
        char* cache_dir = malloc(strlen(home) + strlen(CACHE_DIR) + 2);
        if (cache_dir) {
            sprintf(cache_dir, "%s/%s", home, CACHE_DIR);
            mkdir(cache_dir, 0755); // Create directory, ignore if exists
            free(cache_dir);
        }
    }

    FILE* file = fopen(cache_path, "w");
    free(cache_path);

    if (!file) {
        return;
    }

    fprintf(file, "%s\n", address);
    fclose(file);
}

// Check if a specific address has CHB service
bool check_address_for_scheme_repl(const char* address) {
    printf("Checking cached address %s...", address);
    fflush(stdout);

    bdaddr_t target;
    str2ba(address, &target);

    sdp_session_t* session = sdp_connect(BDADDR_ANY, &target, SDP_RETRY_IF_BUSY);
    if (!session) {
        printf(" (connection failed)\n");
        return false;
    }

    // Look for CHB service
    uuid_t rfcomm_uuid;
    sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
    sdp_list_t* search_list = sdp_list_append(NULL, &rfcomm_uuid);
    uint32_t range = 0x0000ffff;
    sdp_list_t* attr_list = sdp_list_append(NULL, &range);
    sdp_list_t* rsp_list = NULL;

    int result = sdp_service_search_attr_req(session, search_list,
                                           SDP_ATTR_REQ_RANGE, attr_list, &rsp_list);

    bool found_scheme_repl = false;
    if (result == 0) {
        sdp_list_t* r = rsp_list;
        for (; r && !found_scheme_repl; r = r->next) {
            sdp_record_t* rec = (sdp_record_t*) r->data;
            sdp_data_t* service_name = sdp_data_get(rec, SDP_ATTR_SVCNAME_PRIMARY);
            if (service_name && service_name->dtd == SDP_TEXT_STR8) {
                if (strstr(service_name->val.str, "CHB")) {
                    found_scheme_repl = true;
                }
            }
        }
    }

    if (search_list) sdp_list_free(search_list, 0);
    if (attr_list) sdp_list_free(attr_list, 0);
    if (rsp_list) sdp_list_free(rsp_list, 0);
    sdp_close(session);

    if (found_scheme_repl) {
        printf(" CHB found!\n");
        return true;
    } else {
        printf(" (no CHB service)\n");
        return false;
    }
}

void sigint_handler(int sig) {
    if (sig == SIGINT) {
        interrupt_requested = 1;
        if (global_sock >= 0) {
            send_interrupt_message(global_sock);
            printf("^C\n");
        }
    }
}

int send_expression_message(int sock, const char* message) {
    size_t len = strlen(message);
    uint32_t network_len = htonl(len);
    uint8_t msg_type = MSG_TYPE_EXPRESSION;

    // Send message type
    if (send(sock, &msg_type, 1, 0) != 1) {
        perror("Failed to send message type");
        return -1;
    }

    // Send length prefix
    if (send(sock, &network_len, 4, 0) != 4) {
        perror("Failed to send length");
        return -1;
    }

    // Send message
    if (send(sock, message, len, 0) != (ssize_t)len) {
        perror("Failed to send message");
        return -1;
    }

    printf("Sent: %s\n", message);
    return 0;
}

int send_interrupt_message(int sock) {
    uint8_t msg_type = MSG_TYPE_INTERRUPT;

    if (send(sock, &msg_type, 1, 0) != 1) {
        perror("Failed to send interrupt");
        return -1;
    }

    return 0;
}

char* receive_message(int sock) {
    uint32_t network_len;

    // Read length prefix
    if (recv(sock, &network_len, 4, MSG_WAITALL) != 4) {
        perror("Failed to receive length");
        return NULL;
    }

    uint32_t len = ntohl(network_len);
    if (len > MAX_MESSAGE_LENGTH) {
        fprintf(stderr, "Message too long: %u bytes\n", len);
        return NULL;
    }

    printf("Expecting %u bytes...\n", len);

    // Allocate buffer and read message
    char* buffer = malloc(len + 1);
    if (!buffer) {
        perror("Failed to allocate buffer");
        return NULL;
    }

    if (recv(sock, buffer, len, MSG_WAITALL) != (ssize_t)len) {
        perror("Failed to receive message");
        free(buffer);
        return NULL;
    }

    buffer[len] = '\0';
    printf("Received: %s\n", buffer);
    return buffer;
}

int find_service_channel(const char* bt_addr) {
    uuid_t uuid;
    sdp_session_t* session;
    sdp_list_t* search_list;
    sdp_list_t* attr_list;
    sdp_list_t* rsp_list = NULL;
    bdaddr_t target;
    int channel = -1;

    // Convert address string to bdaddr_t
    str2ba(bt_addr, &target);

    // Parse UUID string manually (611a1a1a-94ba-11f0-b0a8-5f754c08f133)
    uint8_t uuid_bytes[16];
    unsigned int u[16];
    if (sscanf(SCHEME_REPL_UUID, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
               &u[0], &u[1], &u[2], &u[3], &u[4], &u[5], &u[6], &u[7],
               &u[8], &u[9], &u[10], &u[11], &u[12], &u[13], &u[14], &u[15]) != 16) {
        fprintf(stderr, "Invalid UUID format\n");
        return -1;
    }
    for (int i = 0; i < 16; i++) {
        uuid_bytes[i] = (uint8_t)u[i];
    }
    sdp_uuid128_create(&uuid, uuid_bytes);

    // Connect to SDP server
    session = sdp_connect(BDADDR_ANY, &target, SDP_RETRY_IF_BUSY);
    if (!session) {
        perror("Failed to connect to SDP server");
        return -1;
    }

    // Set up search criteria
    search_list = sdp_list_append(NULL, &uuid);
    uint32_t range = 0x0000ffff;
    attr_list = sdp_list_append(NULL, &range);

    // Perform SDP search
    int result = sdp_service_search_attr_req(session, search_list,
                                           SDP_ATTR_REQ_RANGE, attr_list, &rsp_list);

    if (result == 0) {
        sdp_list_t* r = rsp_list;
        for (; r; r = r->next) {
            sdp_record_t* rec = (sdp_record_t*) r->data;
            sdp_list_t* proto_list;

            if (sdp_get_access_protos(rec, &proto_list) == 0) {
                sdp_list_t* p = proto_list;
                for (; p; p = p->next) {
                    sdp_list_t* pds = (sdp_list_t*)p->data;
                    for (; pds; pds = pds->next) {
                        sdp_data_t* d = (sdp_data_t*)pds->data;
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
                                        printf("Found service on RFCOMM channel %d\n", channel);
                                        goto cleanup;
                                    }
                                    break;
                            }
                        }
                    }
                }
                if (proto_list) sdp_list_free(proto_list, 0);
            }
        }
    }

cleanup:
    if (search_list) sdp_list_free(search_list, 0);
    if (attr_list) sdp_list_free(attr_list, 0);
    if (rsp_list) sdp_list_free(rsp_list, 0);
    sdp_close(session);

    return channel;
}

char* scan_paired_devices() {
    printf("Scanning paired Bluetooth devices for CHB service...\n");

    int dev_id = hci_get_route(NULL);
    if (dev_id < 0) {
        perror("No Bluetooth adapter found");
        return NULL;
    }

    int sock = hci_open_dev(dev_id);
    if (sock < 0) {
        perror("Failed to open HCI device");
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

    if (ioctl(sock, HCIGETCONNLIST, (void *) cl) < 0) {
        // No active connections, try a different approach
        free(cl);
        close(sock);
        return scan_known_addresses();
    }

    printf("Found %d active connections, checking for CHB...\n", cl->conn_num);

    for (int i = 0; i < cl->conn_num; i++) {
        char addr_str[19];
        ba2str(&ci[i].bdaddr, addr_str);

        printf("Checking %s...", addr_str);
        fflush(stdout);

        // Try to find CHB service on this device
        sdp_session_t* session = sdp_connect(BDADDR_ANY, &ci[i].bdaddr, SDP_RETRY_IF_BUSY);
        if (!session) {
            printf(" (SDP connection failed)\n");
            continue;
        }

        // Look for CHB service by name
        uuid_t rfcomm_uuid;
        sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
        sdp_list_t* search_list = sdp_list_append(NULL, &rfcomm_uuid);
        uint32_t range = 0x0000ffff;
        sdp_list_t* attr_list = sdp_list_append(NULL, &range);
        sdp_list_t* rsp_list = NULL;

        int result = sdp_service_search_attr_req(session, search_list,
                                               SDP_ATTR_REQ_RANGE, attr_list, &rsp_list);

        bool found_scheme_repl = false;
        if (result == 0) {
            sdp_list_t* r = rsp_list;
            for (; r && !found_scheme_repl; r = r->next) {
                sdp_record_t* rec = (sdp_record_t*) r->data;
                sdp_data_t* service_name = sdp_data_get(rec, SDP_ATTR_SVCNAME_PRIMARY);
                if (service_name && service_name->dtd == SDP_TEXT_STR8) {
                    if (strstr(service_name->val.str, "CHB")) {
                        found_scheme_repl = true;
                    }
                }
            }
        }

        if (search_list) sdp_list_free(search_list, 0);
        if (attr_list) sdp_list_free(attr_list, 0);
        if (rsp_list) sdp_list_free(rsp_list, 0);
        sdp_close(session);

        if (found_scheme_repl) {
            printf(" CHB found!\n");
            char* result_addr = malloc(19);
            strcpy(result_addr, addr_str);
            free(cl);
            close(sock);
            return result_addr;
        } else {
            printf(" (no CHB service)\n");
        }
    }

    free(cl);
    close(sock);
    return NULL;
}

char* scan_known_addresses() {
    // Try some common patterns and recently used addresses
    printf("Trying known address patterns...\n");

    // You can add your device address here for faster discovery
    const char* known_addresses[] = {
        "B0:D5:FB:99:14:B0",  // Your device
        NULL
    };

    for (int i = 0; known_addresses[i] != NULL; i++) {
        printf("Checking %s...", known_addresses[i]);
        fflush(stdout);

        bdaddr_t target;
        str2ba(known_addresses[i], &target);

        sdp_session_t* session = sdp_connect(BDADDR_ANY, &target, SDP_RETRY_IF_BUSY);
        if (!session) {
            printf(" (connection failed)\n");
            continue;
        }

        // Look for CHB service
        uuid_t rfcomm_uuid;
        sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
        sdp_list_t* search_list = sdp_list_append(NULL, &rfcomm_uuid);
        uint32_t range = 0x0000ffff;
        sdp_list_t* attr_list = sdp_list_append(NULL, &range);
        sdp_list_t* rsp_list = NULL;

        int result = sdp_service_search_attr_req(session, search_list,
                                               SDP_ATTR_REQ_RANGE, attr_list, &rsp_list);

        bool found_scheme_repl = false;
        if (result == 0) {
            sdp_list_t* r = rsp_list;
            for (; r && !found_scheme_repl; r = r->next) {
                sdp_record_t* rec = (sdp_record_t*) r->data;
                sdp_data_t* service_name = sdp_data_get(rec, SDP_ATTR_SVCNAME_PRIMARY);
                if (service_name && service_name->dtd == SDP_TEXT_STR8) {
                    if (strstr(service_name->val.str, "CHB")) {
                        found_scheme_repl = true;
                    }
                }
            }
        }

        if (search_list) sdp_list_free(search_list, 0);
        if (attr_list) sdp_list_free(attr_list, 0);
        if (rsp_list) sdp_list_free(rsp_list, 0);
        sdp_close(session);

        if (found_scheme_repl) {
            printf(" CHB found!\n");
            char* result_addr = malloc(19);
            strcpy(result_addr, known_addresses[i]);
            return result_addr;
        } else {
            printf(" (no CHB service)\n");
        }
    }

    return NULL;
}

int main(int argc, char* argv[]) {
    const char* bt_addr = NULL;

    if (argc == 1) {
        // Auto-discovery mode - try cached address first
        char* cached_addr = load_cached_address();
        char* discovered_addr = NULL;

        if (cached_addr) {
            if (check_address_for_scheme_repl(cached_addr)) {
                discovered_addr = cached_addr;
                printf("Using cached device: %s\n", discovered_addr);
            } else {
                free(cached_addr);
                cached_addr = NULL;
            }
        }

        if (!discovered_addr) {
            printf("No cached address or cached address failed, scanning devices...\n");
            discovered_addr = scan_paired_devices();
            if (!discovered_addr) {
                fprintf(stderr, "No CHB service found\n");
                fprintf(stderr, "Usage: %s [bluetooth_address]\n", argv[0]);
                fprintf(stderr, "Example: %s AA:BB:CC:DD:EE:FF\n", argv[0]);
                return 1;
            }
            printf("Using discovered device: %s\n", discovered_addr);
            // Save new address to cache
            save_cached_address(discovered_addr);
        }

        bt_addr = discovered_addr;
    } else if (argc == 2) {
        // Manual address mode
        bt_addr = argv[1];
        // Save manually specified address to cache for future use
        save_cached_address(bt_addr);
    } else {
        fprintf(stderr, "Usage: %s [bluetooth_address]\n", argv[0]);
        fprintf(stderr, "Example: %s AA:BB:CC:DD:EE:FF\n", argv[0]);
        fprintf(stderr, "If no address provided, will auto-discover\n");
        return 1;
    }

    // Find service channel using UUID
    printf("Searching for service with UUID %s...\n", SCHEME_REPL_UUID);
    int port = find_service_channel(bt_addr);
    if (port < 0) {
        fprintf(stderr, "Service not found\n");
        return 1;
    }

    // Create RFCOMM socket
    int sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (sock < 0) {
        perror("Failed to create socket");
        return 1;
    }

    // No socket timeout - let user control evaluation time with Ctrl-C

    // Set up connection address
    struct sockaddr_rc addr = {0};
    addr.rc_family = AF_BLUETOOTH;
    addr.rc_channel = port;
    str2ba(bt_addr, &addr.rc_bdaddr);

    // Connect to device
    printf("Connecting to %s on channel %d...\n", bt_addr, port);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Failed to connect");
        close(sock);
        return 1;
    }

    printf("Connected! Starting REPL session...\n");
    printf("Type Scheme expressions (or 'quit' to exit). Press Ctrl-C to interrupt long-running evaluations.\n\n");

    // Set up signal handler
    global_sock = sock;
    signal(SIGINT, sigint_handler);

    // Create input thread
    pthread_t input_tid;
    if (pthread_create(&input_tid, NULL, input_thread, &sock) != 0) {
        perror("Failed to create input thread");
        close(sock);
        return 1;
    }

    // Main thread handles responses
    while (1) {
        // Wait for a message to be sent before trying to receive response
        pthread_mutex_lock(&message_mutex);
        while (!message_sent && !input_complete) {
            pthread_cond_wait(&message_cond, &message_mutex);
        }

        // If input completed without sending a message, exit
        if (input_complete && !message_sent) {
            pthread_mutex_unlock(&message_mutex);
            break;
        }

        // Reset the flag for next iteration
        message_sent = 0;
        pthread_mutex_unlock(&message_mutex);

        char* result = receive_message(sock);
        if (!result) {
            break;
        }

        if (interrupt_requested) {
            printf(" => %s\n\n", result);
            interrupt_requested = 0;
        } else {
            printf(" => %s\n\n", result);
        }
        free(result);

        // For piped input, exit after receiving the first response
        if (input_complete) {
            break;
        }
    }

    // Clean up
    pthread_cancel(input_tid);
    pthread_join(input_tid, NULL);

    close(sock);
    printf("Connection closed.\n");

    // Clean up discovered address if allocated
    if (argc == 1 && bt_addr) {
        free((char*)bt_addr);
    }

    return 0;
}

void* input_thread(void* arg) {
    int sock = *(int*)arg;
    char input[1024];
    bool stdin_is_terminal = isatty(STDIN_FILENO);

    while (1) {
        if (stdin_is_terminal) {
            printf("scheme> ");
            fflush(stdout);
        }

        if (!fgets(input, sizeof(input), stdin)) {
            // EOF reached - for piped input, wait a bit for response then exit
            if (!stdin_is_terminal) {
                sleep(1);  // Give time for response
            }
            break;
        }

        // Remove newline
        size_t len = strlen(input);
        if (len > 0 && input[len-1] == '\n') {
            input[len-1] = '\0';
        }

        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0 || strcmp(input, ":q") == 0) {
            break;
        }

        if (strlen(input) == 0) {
            continue;
        }

        // Send expression
        if (send_expression_message(sock, input) < 0) {
            break;
        }

        // Signal that a message was sent
        pthread_mutex_lock(&message_mutex);
        message_sent = 1;
        pthread_cond_signal(&message_cond);
        pthread_mutex_unlock(&message_mutex);

        // For piped input, signal completion but don't close socket yet
        if (!stdin_is_terminal) {
            input_complete = 1;
            break;
        }
    }

    // Signal completion to main thread
    pthread_mutex_lock(&message_mutex);
    input_complete = 1;
    pthread_cond_signal(&message_cond);
    pthread_mutex_unlock(&message_mutex);

    // For interactive mode, signal main thread to exit
    if (stdin_is_terminal) {
        shutdown(sock, SHUT_RDWR);
    }
    return NULL;
}