/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                    PHANTOM QRNET TRANSPORT PROTOCOL
 *                      "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Content-addressed secure transport for QRNet distributed file network.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <pthread.h>

#include "phantom_qrnet_transport.h"
#include "phantom_qrnet.h"

/* ==============================================================================
 * Utility Functions
 * ============================================================================== */

/* Convert bytes to hex string */
static void bytes_to_hex_transport(const uint8_t *bytes, size_t len, char *hex_out) {
    for (size_t i = 0; i < len; i++) {
        sprintf(hex_out + (i * 2), "%02x", bytes[i]);
    }
    hex_out[len * 2] = '\0';
}

/* Compute SHA-256 hash of data using OpenSSL 3.0 EVP API */
void qrnet_hash_data(const void *data, size_t size,
                     uint8_t *hash_out, char *hex_out) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx) {
        EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
        EVP_DigestUpdate(ctx, data, size);
        EVP_DigestFinal_ex(ctx, hash_out, NULL);
        EVP_MD_CTX_free(ctx);
    }

    if (hex_out) {
        bytes_to_hex_transport(hash_out, 32, hex_out);
    }
}

/* Verify content matches hash */
int qrnet_verify_content(const void *data, size_t size, const char *expected_hash) {
    uint8_t computed_hash[32];
    char computed_hex[65];

    qrnet_hash_data(data, size, computed_hash, computed_hex);

    return (strcmp(computed_hex, expected_hash) == 0);
}

/* Create directory recursively */
static int mkdir_recursive(const char *path) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

/* ==============================================================================
 * Content Store Implementation
 * ============================================================================== */

qrnet_transport_result_t qrnet_store_init(qrnet_content_store_t **store,
                                           const char *base_path,
                                           uint64_t max_size) {
    if (!store || !base_path) return QRNET_TRANSPORT_INVALID_PARAM;

    qrnet_content_store_t *s = calloc(1, sizeof(qrnet_content_store_t));
    if (!s) return QRNET_TRANSPORT_ERROR;

    strncpy(s->base_path, base_path, sizeof(s->base_path) - 1);
    s->max_size = max_size > 0 ? max_size : (1024ULL * 1024 * 1024); /* 1GB default */

    /* Create storage directory */
    mkdir_recursive(base_path);

    printf("[QRNet Transport] Content store initialized at %s (max %llu MB)\n",
           base_path, (unsigned long long)(s->max_size / (1024 * 1024)));

    *store = s;
    return QRNET_TRANSPORT_OK;
}

qrnet_transport_result_t qrnet_store_put(qrnet_content_store_t *store,
                                          const void *data, size_t size,
                                          const char *original_name,
                                          const char *content_type,
                                          char *hash_out) {
    if (!store || !data || size == 0) return QRNET_TRANSPORT_INVALID_PARAM;

    /* Check size limits */
    if (size > QRNET_MAX_CONTENT_SIZE) {
        printf("[QRNet Transport] Content too large: %zu bytes\n", size);
        return QRNET_TRANSPORT_ERROR;
    }

    if (store->total_size + size > store->max_size) {
        printf("[QRNet Transport] Store full, cannot add %zu bytes\n", size);
        return QRNET_TRANSPORT_STORE_FULL;
    }

    /* Compute hash */
    uint8_t hash_bytes[32];
    char hash_hex[65];
    qrnet_hash_data(data, size, hash_bytes, hash_hex);

    /* Check if already exists */
    if (qrnet_store_has(store, hash_hex)) {
        printf("[QRNet Transport] Content already exists: %s\n", hash_hex);
        if (hash_out) strcpy(hash_out, hash_hex);
        return QRNET_TRANSPORT_OK;
    }

    /* Create content entry */
    qrnet_content_entry_t *entry = calloc(1, sizeof(qrnet_content_entry_t));
    if (!entry) return QRNET_TRANSPORT_ERROR;

    strcpy(entry->hash_hex, hash_hex);
    memcpy(entry->hash_bytes, hash_bytes, 32);
    entry->size = size;
    entry->created = time(NULL);
    entry->last_accessed = time(NULL);
    entry->status = QRNET_CONTENT_LOCAL;

    if (original_name) {
        strncpy(entry->original_name, original_name, sizeof(entry->original_name) - 1);
    }
    if (content_type) {
        strncpy(entry->content_type, content_type, sizeof(entry->content_type) - 1);
    }

    /* Build storage path: base/ab/cd/abcdef... */
    snprintf(entry->local_path, sizeof(entry->local_path),
             "%s/%.2s/%.2s/%s",
             store->base_path, hash_hex, hash_hex + 2, hash_hex);

    /* Create directory structure */
    char dir_path[640];
    snprintf(dir_path, sizeof(dir_path), "%s/%.2s/%.2s",
             store->base_path, hash_hex, hash_hex + 2);
    mkdir_recursive(dir_path);

    /* Write content to file */
    int fd = open(entry->local_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("[QRNet Transport] Failed to create file: %s\n", entry->local_path);
        free(entry);
        return QRNET_TRANSPORT_ERROR;
    }

    ssize_t written = write(fd, data, size);
    close(fd);

    if (written != (ssize_t)size) {
        printf("[QRNet Transport] Failed to write content\n");
        unlink(entry->local_path);
        free(entry);
        return QRNET_TRANSPORT_ERROR;
    }

    /* Add to store */
    entry->next = store->entries;
    store->entries = entry;
    store->entry_count++;
    store->total_size += size;
    store->bytes_stored += size;
    store->items_stored++;

    if (hash_out) strcpy(hash_out, hash_hex);

    printf("[QRNet Transport] Stored content: %s (%zu bytes)\n", hash_hex, size);

    return QRNET_TRANSPORT_OK;
}

qrnet_transport_result_t qrnet_store_put_file(qrnet_content_store_t *store,
                                               const char *filepath,
                                               char *hash_out) {
    if (!store || !filepath) return QRNET_TRANSPORT_INVALID_PARAM;

    /* Read file */
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        printf("[QRNet Transport] Cannot open file: %s\n", filepath);
        return QRNET_TRANSPORT_ERROR;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return QRNET_TRANSPORT_ERROR;
    }

    if (st.st_size > QRNET_MAX_CONTENT_SIZE) {
        close(fd);
        return QRNET_TRANSPORT_ERROR;
    }

    void *data = malloc(st.st_size);
    if (!data) {
        close(fd);
        return QRNET_TRANSPORT_ERROR;
    }

    ssize_t bytes_read = read(fd, data, st.st_size);
    close(fd);

    if (bytes_read != st.st_size) {
        free(data);
        return QRNET_TRANSPORT_ERROR;
    }

    /* Extract filename */
    const char *name = strrchr(filepath, '/');
    name = name ? name + 1 : filepath;

    /* Detect content type from extension */
    const char *ext = strrchr(name, '.');
    const char *content_type = "application/octet-stream";
    if (ext) {
        if (strcmp(ext, ".txt") == 0) content_type = "text/plain";
        else if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) content_type = "text/html";
        else if (strcmp(ext, ".json") == 0) content_type = "application/json";
        else if (strcmp(ext, ".png") == 0) content_type = "image/png";
        else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) content_type = "image/jpeg";
        else if (strcmp(ext, ".pdf") == 0) content_type = "application/pdf";
    }

    qrnet_transport_result_t result = qrnet_store_put(store, data, st.st_size,
                                                       name, content_type, hash_out);
    free(data);
    return result;
}

qrnet_transport_result_t qrnet_store_get(qrnet_content_store_t *store,
                                          const char *hash_hex,
                                          void **data_out, size_t *size_out) {
    if (!store || !hash_hex || !data_out || !size_out) {
        return QRNET_TRANSPORT_INVALID_PARAM;
    }

    qrnet_content_entry_t *entry = qrnet_store_lookup(store, hash_hex);
    if (!entry) {
        return QRNET_TRANSPORT_NOT_FOUND;
    }

    /* Read content */
    int fd = open(entry->local_path, O_RDONLY);
    if (fd < 0) {
        return QRNET_TRANSPORT_ERROR;
    }

    void *data = malloc(entry->size);
    if (!data) {
        close(fd);
        return QRNET_TRANSPORT_ERROR;
    }

    ssize_t bytes_read = read(fd, data, entry->size);
    close(fd);

    if (bytes_read != (ssize_t)entry->size) {
        free(data);
        return QRNET_TRANSPORT_ERROR;
    }

    /* Verify hash */
    if (!qrnet_verify_content(data, entry->size, hash_hex)) {
        printf("[QRNet Transport] Hash mismatch for %s!\n", hash_hex);
        free(data);
        return QRNET_TRANSPORT_HASH_MISMATCH;
    }

    /* Update stats */
    entry->last_accessed = time(NULL);
    entry->access_count++;
    store->bytes_served += entry->size;
    store->items_served++;

    *data_out = data;
    *size_out = entry->size;

    return QRNET_TRANSPORT_OK;
}

int qrnet_store_has(qrnet_content_store_t *store, const char *hash_hex) {
    return (qrnet_store_lookup(store, hash_hex) != NULL);
}

qrnet_content_entry_t *qrnet_store_lookup(qrnet_content_store_t *store,
                                           const char *hash_hex) {
    if (!store || !hash_hex) return NULL;

    qrnet_content_entry_t *entry = store->entries;
    while (entry) {
        if (strcmp(entry->hash_hex, hash_hex) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

qrnet_transport_result_t qrnet_store_pin(qrnet_content_store_t *store,
                                          const char *hash_hex) {
    qrnet_content_entry_t *entry = qrnet_store_lookup(store, hash_hex);
    if (!entry) return QRNET_TRANSPORT_NOT_FOUND;

    entry->status = QRNET_CONTENT_PINNED;
    return QRNET_TRANSPORT_OK;
}

void qrnet_store_cleanup(qrnet_content_store_t *store) {
    if (!store) return;

    /* Note: In true Phantom fashion, we don't delete content,
     * but we do free memory structures */
    qrnet_content_entry_t *entry = store->entries;
    while (entry) {
        qrnet_content_entry_t *next = entry->next;
        free(entry);
        entry = next;
    }

    free(store);
}

/* ==============================================================================
 * Transport System Implementation
 * ============================================================================== */

qrnet_transport_result_t qrnet_transport_init(qrnet_transport_t **transport,
                                               struct qrnet_system *qrnet,
                                               int port) {
    if (!transport) return QRNET_TRANSPORT_INVALID_PARAM;

    qrnet_transport_t *t = calloc(1, sizeof(qrnet_transport_t));
    if (!t) return QRNET_TRANSPORT_ERROR;

    t->qrnet = qrnet;
    t->port = port > 0 ? port : QRNET_DEFAULT_PORT;
    t->listen_fd = -1;
    t->max_concurrent = 10;
    t->enable_tls = 1;
    t->auto_announce = 1;
    t->next_transfer_id = 1;

    /* Initialize content store */
    qrnet_transport_result_t result = qrnet_store_init(&t->store,
                                                        QRNET_CONTENT_STORE_DIR,
                                                        0);
    if (result != QRNET_TRANSPORT_OK) {
        free(t);
        return result;
    }

    printf("[QRNet Transport] Initialized on port %d\n", t->port);

    *transport = t;
    return QRNET_TRANSPORT_OK;
}

qrnet_transport_result_t qrnet_transport_listen(qrnet_transport_t *transport) {
    if (!transport) return QRNET_TRANSPORT_INVALID_PARAM;

    /* Create socket */
    transport->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (transport->listen_fd < 0) {
        printf("[QRNet Transport] Failed to create socket: %s\n", strerror(errno));
        return QRNET_TRANSPORT_NETWORK_ERROR;
    }

    /* Allow reuse */
    int opt = 1;
    setsockopt(transport->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(transport->port);

    if (bind(transport->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[QRNet Transport] Failed to bind port %d: %s\n",
               transport->port, strerror(errno));
        close(transport->listen_fd);
        transport->listen_fd = -1;
        return QRNET_TRANSPORT_NETWORK_ERROR;
    }

    /* Listen */
    if (listen(transport->listen_fd, 10) < 0) {
        printf("[QRNet Transport] Failed to listen: %s\n", strerror(errno));
        close(transport->listen_fd);
        transport->listen_fd = -1;
        return QRNET_TRANSPORT_NETWORK_ERROR;
    }

    transport->running = 1;
    printf("[QRNet Transport] Listening on port %d\n", transport->port);

    return QRNET_TRANSPORT_OK;
}

qrnet_transport_result_t qrnet_transport_stop(qrnet_transport_t *transport) {
    if (!transport) return QRNET_TRANSPORT_INVALID_PARAM;

    transport->running = 0;

    if (transport->listen_fd >= 0) {
        close(transport->listen_fd);
        transport->listen_fd = -1;
    }

    /* Close peer connections */
    qrnet_peer_t *peer = transport->peers;
    while (peer) {
        if (peer->socket_fd >= 0) {
            close(peer->socket_fd);
        }
        peer = peer->next;
    }

    return QRNET_TRANSPORT_OK;
}

qrnet_transport_result_t qrnet_transport_add_peer(qrnet_transport_t *transport,
                                                   const char *address,
                                                   int port,
                                                   const char *node_id) {
    if (!transport || !address) return QRNET_TRANSPORT_INVALID_PARAM;

    qrnet_peer_t *peer = calloc(1, sizeof(qrnet_peer_t));
    if (!peer) return QRNET_TRANSPORT_ERROR;

    snprintf(peer->address, sizeof(peer->address), "%s", address);
    peer->port = port > 0 ? port : QRNET_DEFAULT_PORT;
    peer->socket_fd = -1;
    peer->state = QRNET_PEER_DISCONNECTED;

    if (node_id) {
        strncpy(peer->node_id, node_id, sizeof(peer->node_id) - 1);
    }

    peer->next = transport->peers;
    transport->peers = peer;
    transport->peer_count++;

    printf("[QRNet Transport] Added peer: %s:%d\n", address, peer->port);

    return QRNET_TRANSPORT_OK;
}

qrnet_transport_result_t qrnet_transport_connect(qrnet_transport_t *transport,
                                                  const char *address,
                                                  int port) {
    if (!transport || !address) return QRNET_TRANSPORT_INVALID_PARAM;

    /* Find or create peer */
    qrnet_peer_t *peer = transport->peers;
    while (peer) {
        if (strcmp(peer->address, address) == 0 && peer->port == port) {
            break;
        }
        peer = peer->next;
    }

    if (!peer) {
        qrnet_transport_result_t result = qrnet_transport_add_peer(transport,
                                                                    address, port, NULL);
        if (result != QRNET_TRANSPORT_OK) return result;
        peer = transport->peers;
    }

    /* Resolve address */
    struct hostent *host = gethostbyname(address);
    if (!host) {
        printf("[QRNet Transport] Cannot resolve: %s\n", address);
        return QRNET_TRANSPORT_NETWORK_ERROR;
    }

    /* Create socket */
    peer->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (peer->socket_fd < 0) {
        return QRNET_TRANSPORT_NETWORK_ERROR;
    }

    /* Connect */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, host->h_addr, host->h_length);
    addr.sin_port = htons(port);

    peer->state = QRNET_PEER_CONNECTING;

    if (connect(peer->socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[QRNet Transport] Connect failed: %s\n", strerror(errno));
        close(peer->socket_fd);
        peer->socket_fd = -1;
        peer->state = QRNET_PEER_DISCONNECTED;
        return QRNET_TRANSPORT_NETWORK_ERROR;
    }

    peer->state = QRNET_PEER_CONNECTED;
    peer->connected_at = time(NULL);
    peer->last_seen = time(NULL);

    printf("[QRNet Transport] Connected to %s:%d\n", address, port);

    return QRNET_TRANSPORT_OK;
}

void qrnet_transport_cleanup(qrnet_transport_t *transport) {
    if (!transport) return;

    qrnet_transport_stop(transport);

    /* Free peers */
    qrnet_peer_t *peer = transport->peers;
    while (peer) {
        qrnet_peer_t *next = peer->next;
        free(peer);
        peer = next;
    }

    /* Free transfers */
    qrnet_transfer_t *transfer = transport->transfers;
    while (transfer) {
        qrnet_transfer_t *next = transfer->next;
        if (transfer->buffer) free(transfer->buffer);
        free(transfer);
        transfer = next;
    }

    /* Cleanup store */
    qrnet_store_cleanup(transport->store);

    free(transport);
}

/* ==============================================================================
 * Content Transfer Implementation
 * ============================================================================== */

qrnet_transport_result_t qrnet_publish_content(qrnet_transport_t *transport,
                                                const void *data, size_t size,
                                                const char *name,
                                                char *hash_out) {
    if (!transport || !data) return QRNET_TRANSPORT_INVALID_PARAM;

    /* Store locally */
    qrnet_transport_result_t result = qrnet_store_put(transport->store,
                                                       data, size,
                                                       name, NULL, hash_out);
    if (result != QRNET_TRANSPORT_OK) {
        return result;
    }

    /* Announce to peers if enabled */
    if (transport->auto_announce && hash_out) {
        qrnet_announce_content(transport, hash_out);
    }

    return QRNET_TRANSPORT_OK;
}

qrnet_transport_result_t qrnet_publish_file(qrnet_transport_t *transport,
                                             const char *filepath,
                                             char *hash_out) {
    if (!transport || !filepath) return QRNET_TRANSPORT_INVALID_PARAM;

    qrnet_transport_result_t result = qrnet_store_put_file(transport->store,
                                                            filepath, hash_out);
    if (result != QRNET_TRANSPORT_OK) {
        return result;
    }

    if (transport->auto_announce && hash_out) {
        qrnet_announce_content(transport, hash_out);
    }

    return QRNET_TRANSPORT_OK;
}

qrnet_transport_result_t qrnet_fetch_content(qrnet_transport_t *transport,
                                              const char *hash_hex,
                                              void **data_out,
                                              size_t *size_out) {
    if (!transport || !hash_hex || !data_out || !size_out) {
        return QRNET_TRANSPORT_INVALID_PARAM;
    }

    /* Check local store first */
    qrnet_transport_result_t result = qrnet_store_get(transport->store,
                                                       hash_hex,
                                                       data_out, size_out);
    if (result == QRNET_TRANSPORT_OK) {
        printf("[QRNet Transport] Content found locally: %s\n", hash_hex);
        return QRNET_TRANSPORT_OK;
    }

    /* Query peers for content */
    if (transport->peer_count == 0) {
        printf("[QRNet Transport] No peers to fetch from\n");
        return QRNET_TRANSPORT_NO_PEERS;
    }

    /* Try each connected peer */
    qrnet_peer_t *peer = transport->peers;
    while (peer) {
        if (peer->state == QRNET_PEER_CONNECTED ||
            peer->state == QRNET_PEER_AUTHENTICATED) {

            /* Send content query */
            qrnet_msg_content_info_t query;
            memset(&query, 0, sizeof(query));
            query.header.version = QRNET_TRANSPORT_VERSION;
            query.header.msg_type = QRNET_MSG_CONTENT_QUERY;
            query.header.payload_len = sizeof(query) - sizeof(qrnet_msg_header_t);

            /* Convert hex hash to bytes */
            for (int i = 0; i < 32; i++) {
                unsigned int byte;
                sscanf(hash_hex + i * 2, "%02x", &byte);
                query.header.hash[i] = (uint8_t)byte;
            }

            ssize_t sent = send(peer->socket_fd, &query, sizeof(query), 0);
            if (sent == sizeof(query)) {
                /* Wait for response */
                qrnet_msg_header_t response;
                ssize_t received = recv(peer->socket_fd, &response, sizeof(response), 0);

                if (received == sizeof(response) &&
                    response.msg_type == QRNET_MSG_CONTENT_HAVE) {

                    /* Request content */
                    qrnet_msg_header_t request;
                    memset(&request, 0, sizeof(request));
                    request.version = QRNET_TRANSPORT_VERSION;
                    request.msg_type = QRNET_MSG_CONTENT_REQ;
                    memcpy(request.hash, query.header.hash, 32);

                    send(peer->socket_fd, &request, sizeof(request), 0);

                    /* Receive content chunks */
                    /* TODO: Implement chunked receive */
                    printf("[QRNet Transport] Content transfer from %s not yet implemented\n",
                           peer->address);
                }
            }
        }
        peer = peer->next;
    }

    return QRNET_TRANSPORT_NOT_FOUND;
}

qrnet_transport_result_t qrnet_fetch_for_code(qrnet_transport_t *transport,
                                               struct qrnet_code *code,
                                               void **data_out,
                                               size_t *size_out) {
    if (!transport || !code) return QRNET_TRANSPORT_INVALID_PARAM;

    /* Get hash from QRNet code */
    return qrnet_fetch_content(transport, code->content_hash.hex, data_out, size_out);
}

qrnet_transport_result_t qrnet_query_content(qrnet_transport_t *transport,
                                              const char *hash_hex,
                                              char **peer_addresses,
                                              int *peer_count) {
    if (!transport || !hash_hex) return QRNET_TRANSPORT_INVALID_PARAM;

    int found = 0;
    qrnet_peer_t *peer = transport->peers;

    while (peer && found < QRNET_MAX_PEERS) {
        if (peer->state >= QRNET_PEER_CONNECTED) {
            /* Send query - for now just check locally connected peers */
            /* TODO: Actually query each peer */
            if (peer_addresses) {
                peer_addresses[found] = strdup(peer->address);
            }
            found++;
        }
        peer = peer->next;
    }

    if (peer_count) *peer_count = found;
    return found > 0 ? QRNET_TRANSPORT_OK : QRNET_TRANSPORT_NOT_FOUND;
}

qrnet_transport_result_t qrnet_announce_content(qrnet_transport_t *transport,
                                                 const char *hash_hex) {
    if (!transport || !hash_hex) return QRNET_TRANSPORT_INVALID_PARAM;

    qrnet_msg_header_t announce;
    memset(&announce, 0, sizeof(announce));
    announce.version = QRNET_TRANSPORT_VERSION;
    announce.msg_type = QRNET_MSG_ANNOUNCE;

    /* Convert hex to bytes */
    for (int i = 0; i < 32; i++) {
        unsigned int byte;
        sscanf(hash_hex + i * 2, "%02x", &byte);
        announce.hash[i] = (uint8_t)byte;
    }

    /* Send to all connected peers */
    int announced = 0;
    qrnet_peer_t *peer = transport->peers;
    while (peer) {
        if (peer->state >= QRNET_PEER_CONNECTED && peer->socket_fd >= 0) {
            ssize_t sent = send(peer->socket_fd, &announce, sizeof(announce), 0);
            if (sent == sizeof(announce)) {
                announced++;
            }
        }
        peer = peer->next;
    }

    printf("[QRNet Transport] Announced %s to %d peers\n", hash_hex, announced);

    return QRNET_TRANSPORT_OK;
}

/* ==============================================================================
 * Transfer Management
 * ============================================================================== */

qrnet_transfer_t *qrnet_get_transfer(qrnet_transport_t *transport,
                                      uint32_t transfer_id) {
    if (!transport) return NULL;

    qrnet_transfer_t *t = transport->transfers;
    while (t) {
        if (t->transfer_id == transfer_id) return t;
        t = t->next;
    }
    return NULL;
}

qrnet_transport_result_t qrnet_cancel_transfer(qrnet_transport_t *transport,
                                                uint32_t transfer_id) {
    qrnet_transfer_t *t = qrnet_get_transfer(transport, transfer_id);
    if (!t) return QRNET_TRANSPORT_NOT_FOUND;

    t->state = QRNET_TRANSFER_FAILED;

    if (t->socket_fd >= 0) {
        close(t->socket_fd);
        t->socket_fd = -1;
    }

    return QRNET_TRANSPORT_OK;
}

int qrnet_transfer_progress(qrnet_transfer_t *transfer) {
    if (!transfer || transfer->total_size == 0) return 0;
    return (int)((transfer->transferred * 100) / transfer->total_size);
}
