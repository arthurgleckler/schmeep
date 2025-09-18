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

#define MAX_MESSAGE_LENGTH 1048576
#define SCHEME_REPL_UUID "611a1a1a-94ba-11f0-b0a8-5f754c08f133"

// Forward declarations
char* scan_known_addresses();

int send_message(int sock, const char* message) {
    size_t len = strlen(message);
    uint32_t network_len = htonl(len);

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
    printf("Scanning paired Bluetooth devices for SchemeREPL service...\n");

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

    printf("Found %d active connections, checking for SchemeREPL...\n", cl->conn_num);

    for (int i = 0; i < cl->conn_num; i++) {
        char addr_str[19];
        ba2str(&ci[i].bdaddr, addr_str);

        printf("Checking %s...", addr_str);
        fflush(stdout);

        // Try to find SchemeREPL service on this device
        sdp_session_t* session = sdp_connect(BDADDR_ANY, &ci[i].bdaddr, SDP_RETRY_IF_BUSY);
        if (!session) {
            printf(" (SDP connection failed)\n");
            continue;
        }

        // Look for SchemeREPL service by name
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
                    if (strstr(service_name->val.str, "SchemeREPL")) {
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
            printf(" SchemeREPL found!\n");
            char* result_addr = malloc(19);
            strcpy(result_addr, addr_str);
            free(cl);
            close(sock);
            return result_addr;
        } else {
            printf(" (no SchemeREPL service)\n");
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

        // Look for SchemeREPL service
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
                    if (strstr(service_name->val.str, "SchemeREPL")) {
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
            printf(" SchemeREPL found!\n");
            char* result_addr = malloc(19);
            strcpy(result_addr, known_addresses[i]);
            return result_addr;
        } else {
            printf(" (no SchemeREPL service)\n");
        }
    }

    return NULL;
}

int main(int argc, char* argv[]) {
    const char* bt_addr = NULL;

    if (argc == 1) {
        // Auto-discovery mode
        char* discovered_addr = scan_paired_devices();
        if (!discovered_addr) {
            fprintf(stderr, "No SchemeREPL service found\n");
            fprintf(stderr, "Usage: %s [bluetooth_address]\n", argv[0]);
            fprintf(stderr, "Example: %s AA:BB:CC:DD:EE:FF\n", argv[0]);
            return 1;
        }
        bt_addr = discovered_addr;
        printf("Using discovered device: %s\n", bt_addr);
    } else if (argc == 2) {
        // Manual address mode
        bt_addr = argv[1];
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
    printf("Type Scheme expressions (or 'quit' to exit):\n\n");

    // Interactive REPL
    char input[1024];
    while (1) {
        printf("scheme> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
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

        // Send expression and get result
        if (send_message(sock, input) < 0) {
            break;
        }

        char* result = receive_message(sock);
        if (!result) {
            break;
        }

        printf(" => %s\n\n", result);
        free(result);
    }

    close(sock);
    printf("Connection closed.\n");

    // Clean up discovered address if allocated
    if (argc == 1 && bt_addr) {
        free((char*)bt_addr);
    }

    return 0;
}