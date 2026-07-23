// Enables POSIX/macOS socket and clock feature declarations used by C HTTP/WebSocket bridge
#define _POSIX_C_SOURCE 200112L
#define _DARWIN_C_SOURCE

#include "eps_simulator.h"
#include "spacecan_services.h"
#include "transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Public demo HTTP port for local bridge service, not a secret
#define HTTP_PORT 8080u
#define MAX_WS_CLIENTS 8u
#define HTTP_REQUEST_MAX 2048u
#define HTTP_PATH_MAX 512u
#define STATIC_FILE_PATH_MAX 1024u
#define JSON_MAX 512u
#define SHA1_DIGEST_LEN 20u
#define DASHBOARD_DIST_DIR "openmct/dist"
// Fixed WebSocket GUID from RFC 6455, used to compute Sec-WebSocket-Accept
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

static volatile sig_atomic_t keep_running = 1;

static const char *dashboard_html(void);

typedef struct {
    bool valid;
    uint8_t node_id;
    uint16_t sequence;
    eps_state_t state;
    uint16_t bus_voltage_mv;
    int16_t bus_current_ma;
    uint8_t battery_percent;
    int16_t temperature_cdeg;
    uint8_t status_flags;
    long timestamp_ms;
    char json[JSON_MAX];
} telemetry_snapshot_t;

typedef struct {
    uint32_t state[5];
    uint64_t bit_count;
    uint8_t buffer[64];
    size_t buffer_len;
} sha1_ctx_t;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
}

static long now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

static uint32_t rol32(uint32_t value, unsigned int bits)
{
    return (value << bits) | (value >> (32u - bits));
}

// Compresses one 512-bit SHA-1 block for WebSocket accept-key calculation
static void sha1_process_block(sha1_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t w[80];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    size_t i;

    for (i = 0u; i < 16u; ++i) {
        w[i] = ((uint32_t)block[i * 4u] << 24u) |
               ((uint32_t)block[i * 4u + 1u] << 16u) |
               ((uint32_t)block[i * 4u + 2u] << 8u) |
               (uint32_t)block[i * 4u + 3u];
    }
    for (i = 16u; i < 80u; ++i) {
        w[i] = rol32(w[i - 3u] ^ w[i - 8u] ^ w[i - 14u] ^ w[i - 16u], 1u);
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];

    for (i = 0u; i < 80u; ++i) {
        uint32_t f;
        uint32_t k;
        uint32_t temp;
        if (i < 20u) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        } else if (i < 40u) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60u) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        temp = rol32(a, 5u) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30u);
        b = a;
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

static void sha1_init(sha1_ctx_t *ctx)
{
    // Standard SHA-1 initial hash values from SHA-1 specification
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xEFCDAB89u;
    ctx->state[2] = 0x98BADCFEu;
    ctx->state[3] = 0x10325476u;
    ctx->state[4] = 0xC3D2E1F0u;
    ctx->bit_count = 0u;
    ctx->buffer_len = 0u;
}

// Streams arbitrary input into 64-byte SHA-1 blocks without dynamic allocation
static void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t offset = 0u;
    ctx->bit_count += (uint64_t)len * 8u;

    while (offset < len) {
        size_t available = 64u - ctx->buffer_len;
        size_t chunk = (len - offset) < available ? (len - offset) : available;
        memcpy(&ctx->buffer[ctx->buffer_len], &data[offset], chunk);
        ctx->buffer_len += chunk;
        offset += chunk;
        if (ctx->buffer_len == 64u) {
            sha1_process_block(ctx, ctx->buffer);
            ctx->buffer_len = 0u;
        }
    }
}

// Applies SHA-1 padding and writes 20-byte digest used by WebSocket handshake
static void sha1_final(sha1_ctx_t *ctx, uint8_t digest[SHA1_DIGEST_LEN])
{
    uint8_t pad = 0x80u;
    uint8_t zero = 0u;
    uint8_t len_be[8];
    size_t i;
    uint64_t bits = ctx->bit_count;

    sha1_update(ctx, &pad, 1u);
    while (ctx->buffer_len != 56u) {
        sha1_update(ctx, &zero, 1u);
    }
    for (i = 0u; i < 8u; ++i) {
        len_be[7u - i] = (uint8_t)(bits >> (i * 8u));
    }
    sha1_update(ctx, len_be, sizeof(len_be));

    for (i = 0u; i < 5u; ++i) {
        digest[i * 4u] = (uint8_t)(ctx->state[i] >> 24u);
        digest[i * 4u + 1u] = (uint8_t)(ctx->state[i] >> 16u);
        digest[i * 4u + 2u] = (uint8_t)(ctx->state[i] >> 8u);
        digest[i * 4u + 3u] = (uint8_t)ctx->state[i];
    }
}

// Encodes SHA-1 digest into Sec-WebSocket-Accept header value
static size_t base64_encode(const uint8_t *src, size_t len, char *out, size_t out_capacity)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i;
    size_t pos = 0u;

    for (i = 0u; i < len; i += 3u) {
        size_t remaining = len - i;
        uint32_t octet_a = src[i];
        uint32_t octet_b = remaining > 1u ? src[i + 1u] : 0u;
        uint32_t octet_c = remaining > 2u ? src[i + 2u] : 0u;
        uint32_t triple = (octet_a << 16u) | (octet_b << 8u) | octet_c;

        if (pos + 4u >= out_capacity) {
            return 0u;
        }
        out[pos++] = alphabet[(triple >> 18u) & 0x3Fu];
        out[pos++] = alphabet[(triple >> 12u) & 0x3Fu];
        out[pos++] = remaining > 1u ? alphabet[(triple >> 6u) & 0x3Fu] : '=';
        out[pos++] = remaining > 2u ? alphabet[triple & 0x3Fu] : '=';
    }
    if (pos >= out_capacity) {
        return 0u;
    }
    out[pos] = '\0';
    return pos;
}

// Builds RFC 6455 accept key from browser-provided Sec-WebSocket-Key
static void websocket_accept_key(const char *client_key, char *out, size_t out_capacity)
{
    sha1_ctx_t sha1;
    uint8_t digest[SHA1_DIGEST_LEN];
    char combined[256];

    // Browser WebSocket handshake formula: base64(sha1(client_key + RFC6455 GUID))
    snprintf(combined, sizeof(combined), "%s%s", client_key, WS_GUID);
    sha1_init(&sha1);
    sha1_update(&sha1, (const uint8_t *)combined, strlen(combined));
    sha1_final(&sha1, digest);
    (void)base64_encode(digest, sizeof(digest), out, out_capacity);
}

static int send_all(int fd, const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t sent_total = 0u;
    while (sent_total < len) {
        ssize_t sent = send(fd, &bytes[sent_total], len - sent_total, 0);
        if (sent <= 0) {
            return -1;
        }
        sent_total += (size_t)sent;
    }
    return 0;
}

static int send_http_response(int fd, const char *content_type, const char *body)
{
    char header[256];
    int header_len = snprintf(header,
                              sizeof(header),
                              "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                              content_type,
                              (unsigned int)strlen(body));
    if (header_len < 0 || (size_t)header_len >= sizeof(header)) {
        return -1;
    }
    return send_all(fd, header, (size_t)header_len) == 0 && send_all(fd, body, strlen(body)) == 0 ? 0 : -1;
}

static int send_http_status(int fd, unsigned int status, const char *reason, const char *body)
{
    char header[256];
    int header_len = snprintf(header,
                              sizeof(header),
                              "HTTP/1.1 %u %s\r\nContent-Type: text/plain; charset=utf-8\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                              status,
                              reason,
                              (unsigned int)strlen(body));
    if (header_len < 0 || (size_t)header_len >= sizeof(header)) {
        return -1;
    }
    return send_all(fd, header, (size_t)header_len) == 0 && send_all(fd, body, strlen(body)) == 0 ? 0 : -1;
}

static const char *content_type_for_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext == NULL) {
        return "application/octet-stream";
    }
    if (strcmp(ext, ".html") == 0) {
        return "text/html; charset=utf-8";
    }
    if (strcmp(ext, ".js") == 0) {
        return "text/javascript; charset=utf-8";
    }
    if (strcmp(ext, ".css") == 0) {
        return "text/css; charset=utf-8";
    }
    if (strcmp(ext, ".json") == 0) {
        return "application/json; charset=utf-8";
    }
    if (strcmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcmp(ext, ".png") == 0) {
        return "image/png";
    }
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcmp(ext, ".gif") == 0) {
        return "image/gif";
    }
    if (strcmp(ext, ".webp") == 0) {
        return "image/webp";
    }
    if (strcmp(ext, ".woff") == 0) {
        return "font/woff";
    }
    if (strcmp(ext, ".woff2") == 0) {
        return "font/woff2";
    }
    if (strcmp(ext, ".ttf") == 0) {
        return "font/ttf";
    }
    return "application/octet-stream";
}

static bool request_path_safe(const char *path)
{
    return path[0] == '/' && strstr(path, "..") == NULL && strchr(path, '\\') == NULL;
}

static int send_static_file(int fd, const char *request_path)
{
    char path[HTTP_PATH_MAX];
    char file_path[STATIC_FILE_PATH_MAX];
    struct stat info;
    FILE *file;
    char header[256];
    char buffer[4096];
    size_t read_len;
    int header_len;
    const char *query;

    if (!request_path_safe(request_path)) {
        return send_http_status(fd, 403u, "Forbidden", "Forbidden");
    }

    query = strchr(request_path, '?');
    if (query != NULL) {
        size_t path_len = (size_t)(query - request_path);
        if (path_len >= sizeof(path)) {
            return send_http_status(fd, 414u, "URI Too Long", "URI Too Long");
        }
        memcpy(path, request_path, path_len);
        path[path_len] = '\0';
    } else {
        if (strlen(request_path) >= sizeof(path)) {
            return send_http_status(fd, 414u, "URI Too Long", "URI Too Long");
        }
        strcpy(path, request_path);
    }

    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }

    if (snprintf(file_path, sizeof(file_path), "%s%s", DASHBOARD_DIST_DIR, path) < 0) {
        return -1;
    }
    if (stat(file_path, &info) != 0 || !S_ISREG(info.st_mode)) {
        if (snprintf(file_path, sizeof(file_path), "%s/index.html", DASHBOARD_DIST_DIR) < 0) {
            return -1;
        }
        if (stat(file_path, &info) != 0 || !S_ISREG(info.st_mode)) {
            return send_http_response(fd, "text/html; charset=utf-8", dashboard_html());
        }
    }

    file = fopen(file_path, "rb");
    if (file == NULL) {
        return send_http_status(fd, 404u, "Not Found", "Not Found");
    }

    header_len = snprintf(header,
                          sizeof(header),
                          "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",
                          content_type_for_path(file_path),
                          (unsigned long)info.st_size);
    if (header_len < 0 || (size_t)header_len >= sizeof(header) || send_all(fd, header, (size_t)header_len) != 0) {
        fclose(file);
        return -1;
    }

    while ((read_len = fread(buffer, 1u, sizeof(buffer), file)) > 0u) {
        if (send_all(fd, buffer, read_len) != 0) {
            fclose(file);
            return -1;
        }
    }
    fclose(file);
    return 0;
}

static const char *dashboard_html(void)
{
    return "<!doctype html><html><head><meta charset='utf-8'><title>AetherFlow</title>"
           "<style>body{font-family:system-ui;margin:2rem;background:#101418;color:#e6edf3}"
           "h1{color:#8bd5ff}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:1rem}"
           ".card{background:#17202a;border:1px solid #263646;border-radius:12px;padding:1rem}.v{font-size:2rem}</style></head>"
           "<body><h1>AetherFlow live EPS telemetry</h1><p id='status'>connecting...</p><div class='grid' id='grid'></div>"
           "<script>const keys=['sequence','state','bus_voltage_mv','bus_current_ma','battery_percent','temperature_cdeg','status_flags'];"
           "const grid=document.getElementById('grid');for(const k of keys){const d=document.createElement('div');d.className='card';d.innerHTML='<b>'+k+'</b><div class=v id='+k+'>-</div>';grid.appendChild(d);}"
           "const ws=new WebSocket('ws://'+location.host+'/realtime');ws.onopen=()=>status.textContent='connected';ws.onclose=()=>status.textContent='disconnected';"
           "ws.onmessage=e=>{const t=JSON.parse(e.data);for(const k of keys){const el=document.getElementById(k);if(el)el.textContent=t[k];}}</script></body></html>";
}

static int send_ws_text(int fd, const char *text)
{
    uint8_t header[4];
    size_t len = strlen(text);

    header[0] = 0x81u;
    if (len < 126u) {
        header[1] = (uint8_t)len;
        return send_all(fd, header, 2u) == 0 && send_all(fd, text, len) == 0 ? 0 : -1;
    }
    if (len <= 65535u) {
        header[1] = 126u;
        header[2] = (uint8_t)(len >> 8u);
        header[3] = (uint8_t)len;
        return send_all(fd, header, 4u) == 0 && send_all(fd, text, len) == 0 ? 0 : -1;
    }
    return -1;
}

// Opens small blocking HTTP listener used for health, JSON snapshot and WebSocket upgrade
static uint16_t http_port_from_env(void)
{
    const char *value = getenv("PORT");
    char *end = NULL;
    unsigned long port;

    if (value == NULL || *value == '\0') {
        return HTTP_PORT;
    }

    errno = 0;
    port = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || port == 0u || port > 65535u) {
        fprintf(stderr, "bridge_service: invalid PORT value '%s', using %u\n", value, (unsigned int)HTTP_PORT);
        return HTTP_PORT;
    }
    return (uint16_t)port;
}

static int open_http_server(uint16_t port)
{
    int fd;
    int yes = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) {
        close(fd);
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 8) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool request_target(const char *request, char *out, size_t out_capacity)
{
    const char *path_start;
    const char *path_end;
    size_t path_len;

    if (strncmp(request, "GET ", 4u) != 0) {
        return false;
    }

    path_start = request + 4;
    path_end = strchr(path_start, ' ');
    if (path_end == NULL) {
        return false;
    }

    path_len = (size_t)(path_end - path_start);
    if (path_len == 0u || path_len >= out_capacity) {
        return false;
    }

    memcpy(out, path_start, path_len);
    out[path_len] = '\0';
    return true;
}

static const char *find_header_value(const char *request, const char *header_name, char *out, size_t out_capacity)
{
    const char *line = request;
    size_t header_len = strlen(header_name);

    while (line != NULL && *line != '\0') {
        const char *next = strstr(line, "\r\n");
        size_t line_len = next == NULL ? strlen(line) : (size_t)(next - line);
        if (line_len > header_len + 1u && strncasecmp(line, header_name, header_len) == 0 && line[header_len] == ':') {
            const char *value = line + header_len + 1u;
            size_t value_len;
            while (*value == ' ') {
                ++value;
            }
            value_len = line_len - (size_t)(value - line);
            if (value_len >= out_capacity) {
                value_len = out_capacity - 1u;
            }
            memcpy(out, value, value_len);
            out[value_len] = '\0';
            return out;
        }
        if (next == NULL) {
            break;
        }
        line = next + 2;
    }
    return NULL;
}

static void compact_ws_clients(int ws_clients[MAX_WS_CLIENTS])
{
    size_t i;
    for (i = 0u; i < MAX_WS_CLIENTS; ++i) {
        if (ws_clients[i] < 0) {
            size_t j;
            for (j = i + 1u; j < MAX_WS_CLIENTS; ++j) {
                if (ws_clients[j] >= 0) {
                    ws_clients[i] = ws_clients[j];
                    ws_clients[j] = -1;
                    break;
                }
            }
        }
    }
}

static void broadcast_ws(int ws_clients[MAX_WS_CLIENTS], const char *json)
{
    size_t i;
    for (i = 0u; i < MAX_WS_CLIENTS; ++i) {
        if (ws_clients[i] >= 0 && send_ws_text(ws_clients[i], json) != 0) {
            close(ws_clients[i]);
            ws_clients[i] = -1;
        }
    }
    compact_ws_clients(ws_clients);
}

// Converts decoded EPS snapshot into compact JSON shared by HTTP and WebSocket paths
static void telemetry_to_json(telemetry_snapshot_t *telemetry)
{
    snprintf(telemetry->json,
             sizeof(telemetry->json),
             "{\"node\":%u,\"service\":%u,\"subtype\":%u,\"sequence\":%u,\"state\":\"%s\","
             "\"bus_voltage_mv\":%u,\"bus_current_ma\":%d,\"battery_percent\":%u,"
             "\"temperature_cdeg\":%d,\"status_flags\":%u,\"timestamp_ms\":%ld}",
             (unsigned int)telemetry->node_id,
             (unsigned int)SPACECAN_SERVICE_HOUSEKEEPING,
             (unsigned int)SPACECAN_HK_SUBTYPE_REPORT,
             (unsigned int)telemetry->sequence,
             eps_state_name(telemetry->state),
             (unsigned int)telemetry->bus_voltage_mv,
             (int)telemetry->bus_current_ma,
             (unsigned int)telemetry->battery_percent,
             (int)telemetry->temperature_cdeg,
             (unsigned int)telemetry->status_flags,
             telemetry->timestamp_ms);
}

// Maps SpaceCAN service 3 subtype 25 payload bytes into named EPS telemetry fields
static bool decode_eps_housekeeping(uint8_t node_id,
                                    const spacecan_packet_view_t *view,
                                    telemetry_snapshot_t *telemetry)
{
    if (view->service != SPACECAN_SERVICE_HOUSEKEEPING || view->subtype != SPACECAN_HK_SUBTYPE_REPORT) {
        return false;
    }
    if (view->payload_len != EPS_HOUSEKEEPING_PAYLOAD_LEN) {
        return false;
    }

    telemetry->valid = true;
    telemetry->node_id = node_id;
    telemetry->sequence = spacecan_get_u16_be(&view->payload[0]);
    telemetry->state = (eps_state_t)view->payload[2];
    telemetry->bus_voltage_mv = spacecan_get_u16_be(&view->payload[3]);
    telemetry->bus_current_ma = spacecan_get_i16_be(&view->payload[5]);
    telemetry->battery_percent = view->payload[7];
    telemetry->temperature_cdeg = spacecan_get_i16_be(&view->payload[8]);
    telemetry->status_flags = view->payload[10];
    telemetry->timestamp_ms = now_ms();
    telemetry_to_json(telemetry);
    return true;
}

static void handle_http_client(int server_fd,
                               int ws_clients[MAX_WS_CLIENTS],
                               const telemetry_snapshot_t *telemetry)
{
    int client_fd = accept(server_fd, NULL, NULL);
    char request[HTTP_REQUEST_MAX];
    char path[HTTP_PATH_MAX];
    ssize_t received;

    if (client_fd < 0) {
        return;
    }

    received = recv(client_fd, request, sizeof(request) - 1u, 0);
    if (received <= 0) {
        close(client_fd);
        return;
    }
    request[received] = '\0';
    if (!request_target(request, path, sizeof(path))) {
        (void)send_http_status(client_fd, 405u, "Method Not Allowed", "Method Not Allowed");
        close(client_fd);
        return;
    }

    if (strcmp(path, "/realtime") == 0) {
        char key[160];
        char accept_key[64];
        char response[256];
        size_t i;
        int response_len;

        if (find_header_value(request, "Sec-WebSocket-Key", key, sizeof(key)) == NULL) {
            close(client_fd);
            return;
        }
        websocket_accept_key(key, accept_key, sizeof(accept_key));
        response_len = snprintf(response,
                                sizeof(response),
                                "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",
                                accept_key);
        if (response_len < 0 || (size_t)response_len >= sizeof(response) || send_all(client_fd, response, (size_t)response_len) != 0) {
            close(client_fd);
            return;
        }
        for (i = 0u; i < MAX_WS_CLIENTS; ++i) {
            if (ws_clients[i] < 0) {
                ws_clients[i] = client_fd;
                if (telemetry->valid) {
                    (void)send_ws_text(client_fd, telemetry->json);
                }
                return;
            }
        }
        close(client_fd);
        return;
    }

    if (strcmp(path, "/telemetry/latest") == 0) {
        if (telemetry->valid) {
            (void)send_http_response(client_fd, "application/json", telemetry->json);
        } else {
            (void)send_http_response(client_fd, "application/json", "{\"valid\":false}");
        }
    } else if (strcmp(path, "/health") == 0) {
        (void)send_http_response(client_fd, "application/json", "{\"status\":\"ok\"}");
    } else {
        (void)send_static_file(client_fd, path);
    }
    close(client_fd);
}

static void handle_can_frame(const can_frame_t *frame,
                             spacecan_reassembly_t *reassembly,
                             telemetry_snapshot_t *telemetry,
                             int ws_clients[MAX_WS_CLIENTS])
{
    spacecan_id_t parsed_id;
    uint8_t packet[SPACECAN_PACKET_MAX_SIZE];
    size_t packet_len = 0u;
    spacecan_packet_view_t view;
    spacecan_status_t status;

    if (spacecan_parse_can_id(frame->id, &parsed_id) != SPACECAN_OK || parsed_id.frame_class != SPACECAN_FRAME_REPLY) {
        return;
    }

    status = spacecan_reassembly_accept(reassembly, frame, packet, sizeof(packet), &packet_len);
    if (status == SPACECAN_ERR_IN_PROGRESS) {
        return;
    }
    if (status != SPACECAN_OK) {
        fprintf(stderr, "bridge_service: reassembly error %d for id=0x%03X\n", (int)status, (unsigned int)frame->id);
        spacecan_reassembly_reset(reassembly);
        return;
    }
    if (spacecan_packet_parse(packet, packet_len, &view) != SPACECAN_OK) {
        fputs("bridge_service: failed to parse SpaceCAN packet\n", stderr);
        return;
    }
    if (decode_eps_housekeeping(parsed_id.node_id, &view, telemetry)) {
        printf("bridge_service: HK %s\n", telemetry->json);
        fflush(stdout);
        broadcast_ws(ws_clients, telemetry->json);
    }
}

int main(void)
{
    udp_transport_t transport;
    uint16_t http_port = http_port_from_env();
    int server_fd;
    int ws_clients[MAX_WS_CLIENTS];
    spacecan_reassembly_t reassembly;
    telemetry_snapshot_t telemetry;
    size_t i;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    for (i = 0u; i < MAX_WS_CLIENTS; ++i) {
        ws_clients[i] = -1;
    }
    memset(&telemetry, 0, sizeof(telemetry));
    spacecan_reassembly_reset(&reassembly);

    if (udp_transport_open(&transport, AETHERFLOW_UDP_GROUP, AETHERFLOW_UDP_PORT) != TRANSPORT_OK) {
        fprintf(stderr,
                "bridge_service: failed to open UDP multicast bus %s:%u\n",
                AETHERFLOW_UDP_GROUP,
                (unsigned int)AETHERFLOW_UDP_PORT);
        return 1;
    }

    server_fd = open_http_server(http_port);
    if (server_fd < 0) {
        fprintf(stderr, "bridge_service: failed to open HTTP/WebSocket server on port %u\n", (unsigned int)http_port);
        udp_transport_close(&transport);
        return 1;
    }

    printf("bridge_service: UDP multicast bus %s:%u HTTP/WebSocket http://0.0.0.0:%u/\n",
           AETHERFLOW_UDP_GROUP,
           (unsigned int)AETHERFLOW_UDP_PORT,
           (unsigned int)http_port);

    while (keep_running) {
        fd_set read_fds;
        struct timeval timeout;
        int max_fd = transport.fd > server_fd ? transport.fd : server_fd;
        int ready;

        FD_ZERO(&read_fds);
        FD_SET(transport.fd, &read_fds);
        FD_SET(server_fd, &read_fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;

        ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            fputs("bridge_service: select failed\n", stderr);
            break;
        }
        if (ready == 0) {
            continue;
        }
        if (FD_ISSET(server_fd, &read_fds)) {
            handle_http_client(server_fd, ws_clients, &telemetry);
        }
        if (FD_ISSET(transport.fd, &read_fds)) {
            can_frame_t frame;
            if (udp_transport_recv(&transport, &frame, 0) == TRANSPORT_OK) {
                handle_can_frame(&frame, &reassembly, &telemetry, ws_clients);
            }
        }
    }

    for (i = 0u; i < MAX_WS_CLIENTS; ++i) {
        if (ws_clients[i] >= 0) {
            close(ws_clients[i]);
        }
    }
    close(server_fd);
    udp_transport_close(&transport);
    puts("bridge_service: stopped");
    return 0;
}
