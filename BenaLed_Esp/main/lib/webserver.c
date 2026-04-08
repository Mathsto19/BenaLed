#include "webserver.h"
#include "httpd.h"
#include "matrix_task.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define TAG "WEB_SERVER"

#ifndef CONFIG_LWIP_MAX_SOCKETS
#define CONFIG_LWIP_MAX_SOCKETS 16
#endif

#define ADMIN_URI "/admin"
#define ADMIN_PASSWORD "1925"
#define ADMIN_AUTH_REALM "BenaLed Admin"
#define ADMIN_AUTH_MAX_LEN 200
#define ADMIN_BASIC_DECODED_MAX 96
#define ADMIN_POST_BODY_MAX 384
#define ADMIN_USER_ID_MAX_LEN 46
#define ADMIN_QUEUE_CAPACITY 24
#define ADMIN_JSON_MAX 4096

typedef struct
{
    bool queue_mode_enabled;
    bool rotation_paused;
    uint16_t default_turn_sec;
    uint16_t min_turn_sec;
    uint16_t max_turn_sec;
    uint16_t max_users;
    size_t queue_len;
    char queue_users[ADMIN_QUEUE_CAPACITY][ADMIN_USER_ID_MAX_LEN];
    uint64_t turn_started_us;
} admin_state_t;

typedef struct
{
    bool queue_mode_enabled;
    bool rotation_paused;
    uint16_t default_turn_sec;
    uint16_t min_turn_sec;
    uint16_t max_turn_sec;
    uint16_t max_users;
    size_t queue_len;
    char queue_users[ADMIN_QUEUE_CAPACITY][ADMIN_USER_ID_MAX_LEN];
} admin_state_snapshot_t;

static admin_state_t s_admin_state = {
    .queue_mode_enabled = false,
    .rotation_paused = false,
    .default_turn_sec = 30,
    .min_turn_sec = 10,
    .max_turn_sec = 120,
    .max_users = (WIFI_AP_MAX_CONN > 0) ? WIFI_AP_MAX_CONN : 1,
    .queue_len = 0,
    .turn_started_us = 0,
};
static portMUX_TYPE s_admin_state_mux = portMUX_INITIALIZER_UNLOCKED;

static bool is_hex_char(char c)
{
    return isxdigit((unsigned char)c) != 0;
}

static uint8_t hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
    {
        return (uint8_t)(c - '0');
    }

    c = (char)tolower((unsigned char)c);
    return (uint8_t)(10 + (c - 'a'));
}

static esp_err_t parse_legacy_text_matrix_to_rgb(const uint8_t *payload, size_t len, uint8_t *rgb_out)
{
    if (payload == NULL || rgb_out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t pixel_count = (size_t)MATRIX_WIDTH * (size_t)MATRIX_HEIGHT;
    size_t found = 0;

    for (size_t i = 0; i + 7 < len && found < pixel_count; i++)
    {
        if (payload[i] != '"')
        {
            continue;
        }

        size_t start = i + 1;
        if (start < len && payload[start] == '#')
        {
            start++;
        }

        if (start + 6 >= len)
        {
            continue;
        }

        if (!is_hex_char((char)payload[start + 0]) ||
            !is_hex_char((char)payload[start + 1]) ||
            !is_hex_char((char)payload[start + 2]) ||
            !is_hex_char((char)payload[start + 3]) ||
            !is_hex_char((char)payload[start + 4]) ||
            !is_hex_char((char)payload[start + 5]))
        {
            continue;
        }

        if (payload[start + 6] != '"')
        {
            continue;
        }

        size_t out = found * 3;
        rgb_out[out + 0] = (uint8_t)((hex_nibble((char)payload[start + 0]) << 4) | hex_nibble((char)payload[start + 1]));
        rgb_out[out + 1] = (uint8_t)((hex_nibble((char)payload[start + 2]) << 4) | hex_nibble((char)payload[start + 3]));
        rgb_out[out + 2] = (uint8_t)((hex_nibble((char)payload[start + 4]) << 4) | hex_nibble((char)payload[start + 5]));
        found++;

        i = start + 6;
    }

    if (found != pixel_count)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

static size_t admin_effective_max_users_locked(void)
{
    size_t connection_cap = WIFI_AP_MAX_CONN;
    if (connection_cap == 0)
    {
        connection_cap = 1;
    }
    if (connection_cap > ADMIN_QUEUE_CAPACITY)
    {
        connection_cap = ADMIN_QUEUE_CAPACITY;
    }

    size_t effective = (size_t)s_admin_state.max_users;
    if (effective == 0)
    {
        effective = 1;
    }
    if (effective > ADMIN_QUEUE_CAPACITY)
    {
        effective = ADMIN_QUEUE_CAPACITY;
    }
    if (effective > connection_cap)
    {
        effective = connection_cap;
    }
    return effective;
}

static void admin_trim_queue_locked(void)
{
    size_t max_users = admin_effective_max_users_locked();
    if (s_admin_state.queue_len > max_users)
    {
        s_admin_state.queue_len = max_users;
    }

    for (size_t i = s_admin_state.queue_len; i < ADMIN_QUEUE_CAPACITY; i++)
    {
        s_admin_state.queue_users[i][0] = '\0';
    }

    if (s_admin_state.queue_len == 0)
    {
        s_admin_state.turn_started_us = 0;
    }
}

static void admin_apply_policy_guardrails_locked(void)
{
    size_t connection_cap = WIFI_AP_MAX_CONN;
    if (connection_cap == 0)
    {
        connection_cap = 1;
    }
    if (connection_cap > ADMIN_QUEUE_CAPACITY)
    {
        connection_cap = ADMIN_QUEUE_CAPACITY;
    }

    if (s_admin_state.min_turn_sec == 0)
    {
        s_admin_state.min_turn_sec = 1;
    }

    if (s_admin_state.max_turn_sec < s_admin_state.min_turn_sec)
    {
        s_admin_state.max_turn_sec = s_admin_state.min_turn_sec;
    }

    if (s_admin_state.default_turn_sec < s_admin_state.min_turn_sec)
    {
        s_admin_state.default_turn_sec = s_admin_state.min_turn_sec;
    }

    if (s_admin_state.default_turn_sec > s_admin_state.max_turn_sec)
    {
        s_admin_state.default_turn_sec = s_admin_state.max_turn_sec;
    }

    if (s_admin_state.max_users == 0)
    {
        s_admin_state.max_users = 1;
    }

    if (s_admin_state.max_users > ADMIN_QUEUE_CAPACITY)
    {
        s_admin_state.max_users = ADMIN_QUEUE_CAPACITY;
    }
    if (s_admin_state.max_users > connection_cap)
    {
        s_admin_state.max_users = (uint16_t)connection_cap;
    }

    admin_trim_queue_locked();
}

static uint16_t webserver_safe_max_open_sockets(void)
{
    uint16_t lwip_limit = 1;
#if CONFIG_LWIP_MAX_SOCKETS > 3
    lwip_limit = (uint16_t)(CONFIG_LWIP_MAX_SOCKETS - 3);
#endif

    uint16_t desired = 12;
    if (desired > lwip_limit)
    {
        desired = lwip_limit;
    }

    if (desired == 0)
    {
        desired = 1;
    }

    return desired;
}

static int admin_find_user_index_locked(const char *user_id)
{
    if (user_id == NULL || user_id[0] == '\0')
    {
        return -1;
    }

    for (size_t i = 0; i < s_admin_state.queue_len; i++)
    {
        if (strcmp(s_admin_state.queue_users[i], user_id) == 0)
        {
            return (int)i;
        }
    }

    return -1;
}

static void admin_rotate_queue_once_locked(void)
{
    if (s_admin_state.queue_len <= 1)
    {
        return;
    }

    char first_user[ADMIN_USER_ID_MAX_LEN];
    strlcpy(first_user, s_admin_state.queue_users[0], sizeof(first_user));

    for (size_t i = 1; i < s_admin_state.queue_len; i++)
    {
        strlcpy(s_admin_state.queue_users[i - 1], s_admin_state.queue_users[i], ADMIN_USER_ID_MAX_LEN);
    }

    strlcpy(s_admin_state.queue_users[s_admin_state.queue_len - 1], first_user, ADMIN_USER_ID_MAX_LEN);
}

static void admin_refresh_rotation_locked(uint64_t now_us)
{
    if (s_admin_state.queue_len == 0)
    {
        s_admin_state.turn_started_us = 0;
        return;
    }

    if (s_admin_state.turn_started_us == 0)
    {
        s_admin_state.turn_started_us = now_us;
        return;
    }

    if (!s_admin_state.queue_mode_enabled || s_admin_state.rotation_paused || s_admin_state.queue_len <= 1)
    {
        return;
    }

    uint64_t turn_duration_us = (uint64_t)s_admin_state.default_turn_sec * 1000000ULL;
    if (turn_duration_us == 0)
    {
        turn_duration_us = 1000000ULL;
    }

    uint64_t elapsed_us = now_us - s_admin_state.turn_started_us;
    if (elapsed_us < turn_duration_us)
    {
        return;
    }

    uint64_t steps = elapsed_us / turn_duration_us;
    size_t effective_steps = (size_t)(steps % (uint64_t)s_admin_state.queue_len);

    for (size_t i = 0; i < effective_steps; i++)
    {
        admin_rotate_queue_once_locked();
    }

    s_admin_state.turn_started_us = now_us - (elapsed_us % turn_duration_us);
}

static bool admin_append_json_raw(char *json, size_t json_size, size_t *offset, const char *raw)
{
    size_t raw_len = strlen(raw);
    if (*offset + raw_len >= json_size)
    {
        return false;
    }

    memcpy(json + *offset, raw, raw_len);
    *offset += raw_len;
    json[*offset] = '\0';
    return true;
}

static bool admin_append_json_char(char *json, size_t json_size, size_t *offset, char value)
{
    if (*offset + 1 >= json_size)
    {
        return false;
    }

    json[*offset] = value;
    (*offset)++;
    json[*offset] = '\0';
    return true;
}

static bool admin_append_json_quoted(char *json, size_t json_size, size_t *offset, const char *text)
{
    if (!admin_append_json_char(json, json_size, offset, '"'))
    {
        return false;
    }

    const unsigned char *cursor = (const unsigned char *)(text ? text : "");
    while (*cursor != '\0')
    {
        switch (*cursor)
        {
        case '\\':
            if (!admin_append_json_raw(json, json_size, offset, "\\\\"))
            {
                return false;
            }
            break;
        case '"':
            if (!admin_append_json_raw(json, json_size, offset, "\\\""))
            {
                return false;
            }
            break;
        case '\n':
            if (!admin_append_json_raw(json, json_size, offset, "\\n"))
            {
                return false;
            }
            break;
        case '\r':
            if (!admin_append_json_raw(json, json_size, offset, "\\r"))
            {
                return false;
            }
            break;
        case '\t':
            if (!admin_append_json_raw(json, json_size, offset, "\\t"))
            {
                return false;
            }
            break;
        default:
            if (*cursor < 0x20)
            {
                if (!admin_append_json_raw(json, json_size, offset, "\\u00"))
                {
                    return false;
                }
                const char hex_digits[] = "0123456789abcdef";
                if (!admin_append_json_char(json, json_size, offset, hex_digits[(*cursor >> 4) & 0x0F]) ||
                    !admin_append_json_char(json, json_size, offset, hex_digits[*cursor & 0x0F]))
                {
                    return false;
                }
            }
            else if (!admin_append_json_char(json, json_size, offset, (char)*cursor))
            {
                return false;
            }
            break;
        }

        cursor++;
    }

    return admin_append_json_char(json, json_size, offset, '"');
}

static void admin_copy_snapshot(admin_state_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return;
    }

    uint64_t now_us = (uint64_t)esp_timer_get_time();

    taskENTER_CRITICAL(&s_admin_state_mux);
    admin_refresh_rotation_locked(now_us);

    snapshot->queue_mode_enabled = s_admin_state.queue_mode_enabled;
    snapshot->rotation_paused = s_admin_state.rotation_paused;
    snapshot->default_turn_sec = s_admin_state.default_turn_sec;
    snapshot->min_turn_sec = s_admin_state.min_turn_sec;
    snapshot->max_turn_sec = s_admin_state.max_turn_sec;
    snapshot->max_users = s_admin_state.max_users;
    snapshot->queue_len = s_admin_state.queue_len;

    for (size_t i = 0; i < s_admin_state.queue_len && i < ADMIN_QUEUE_CAPACITY; i++)
    {
        strlcpy(snapshot->queue_users[i], s_admin_state.queue_users[i], ADMIN_USER_ID_MAX_LEN);
    }
    taskEXIT_CRITICAL(&s_admin_state_mux);
}

static bool admin_get_client_identifier(httpd_req_t *req, char *out, size_t out_size)
{
    if (req == NULL || out == NULL || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0)
    {
        return false;
    }

    struct sockaddr_storage peer_addr = {0};
    socklen_t peer_addr_len = sizeof(peer_addr);
    if (getpeername(sockfd, (struct sockaddr *)&peer_addr, &peer_addr_len) != 0)
    {
        return false;
    }

    if (peer_addr.ss_family == AF_INET)
    {
        const struct sockaddr_in *addr4 = (const struct sockaddr_in *)&peer_addr;
        return inet_ntop(AF_INET, &addr4->sin_addr, out, out_size) != NULL;
    }

    if (peer_addr.ss_family == AF_INET6)
    {
        const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)&peer_addr;
        return inet_ntop(AF_INET6, &addr6->sin6_addr, out, out_size) != NULL;
    }

    return false;
}

static bool admin_record_sender_and_check_turn(const char *sender_id)
{
    bool can_send = true;
    uint64_t now_us = (uint64_t)esp_timer_get_time();

    taskENTER_CRITICAL(&s_admin_state_mux);
    admin_refresh_rotation_locked(now_us);

    if (s_admin_state.queue_mode_enabled && sender_id != NULL && sender_id[0] != '\0')
    {
        int sender_index = admin_find_user_index_locked(sender_id);
        if (sender_index < 0)
        {
            size_t max_users = admin_effective_max_users_locked();
            if (s_admin_state.queue_len < max_users)
            {
                strlcpy(s_admin_state.queue_users[s_admin_state.queue_len], sender_id, ADMIN_USER_ID_MAX_LEN);
                s_admin_state.queue_len++;
                if (s_admin_state.turn_started_us == 0)
                {
                    s_admin_state.turn_started_us = now_us;
                }
            }
        }

        if (s_admin_state.queue_len > 0)
        {
            can_send = strcmp(s_admin_state.queue_users[0], sender_id) == 0;
        }
    }
    taskEXIT_CRITICAL(&s_admin_state_mux);

    return can_send;
}

static int admin_base64_char_value(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c - 'A';
    }

    if (c >= 'a' && c <= 'z')
    {
        return c - 'a' + 26;
    }

    if (c >= '0' && c <= '9')
    {
        return c - '0' + 52;
    }

    if (c == '+')
    {
        return 62;
    }

    if (c == '/')
    {
        return 63;
    }

    return -1;
}

static bool admin_decode_base64(const char *encoded, uint8_t *decoded, size_t decoded_size, size_t *decoded_len)
{
    if (encoded == NULL || decoded == NULL || decoded_size == 0)
    {
        return false;
    }

    size_t out_len = 0;
    int accumulator = 0;
    int bits_collected = -8;

    for (const unsigned char *cursor = (const unsigned char *)encoded; *cursor != '\0'; cursor++)
    {
        if (isspace(*cursor))
        {
            continue;
        }

        if (*cursor == '=')
        {
            break;
        }

        int value = admin_base64_char_value((char)*cursor);
        if (value < 0)
        {
            return false;
        }

        accumulator = (accumulator << 6) | value;
        bits_collected += 6;

        if (bits_collected >= 0)
        {
            if (out_len >= decoded_size)
            {
                return false;
            }

            decoded[out_len++] = (uint8_t)((accumulator >> bits_collected) & 0xFF);
            bits_collected -= 8;
        }
    }

    if (decoded_len != NULL)
    {
        *decoded_len = out_len;
    }

    return true;
}

static bool admin_has_valid_password_header(const char *auth_header)
{
    static const char basic_prefix[] = "Basic ";

    if (auth_header == NULL || strncasecmp(auth_header, basic_prefix, sizeof(basic_prefix) - 1) != 0)
    {
        return false;
    }

    const char *encoded = auth_header + (sizeof(basic_prefix) - 1);
    uint8_t decoded[ADMIN_BASIC_DECODED_MAX];
    size_t decoded_len = 0;
    if (!admin_decode_base64(encoded, decoded, sizeof(decoded) - 1, &decoded_len))
    {
        return false;
    }

    decoded[decoded_len] = '\0';
    char *separator = strchr((char *)decoded, ':');
    if (separator == NULL)
    {
        return strcmp((const char *)decoded, ADMIN_PASSWORD) == 0;
    }

    return strcmp(separator + 1, ADMIN_PASSWORD) == 0;
}

static bool admin_is_request_authorized(httpd_req_t *req, bool *has_auth_header)
{
    if (has_auth_header != NULL)
    {
        *has_auth_header = false;
    }

    size_t auth_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (auth_len == 0)
    {
        return false;
    }

    if (has_auth_header != NULL)
    {
        *has_auth_header = true;
    }

    if (auth_len >= ADMIN_AUTH_MAX_LEN)
    {
        return false;
    }

    char auth_header[ADMIN_AUTH_MAX_LEN];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK)
    {
        return false;
    }

    return admin_has_valid_password_header(auth_header);
}

static esp_err_t admin_redirect_to_public_root(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t admin_send_auth_prompt(httpd_req_t *req)
{
    char authenticate_header[96];
    snprintf(authenticate_header, sizeof(authenticate_header), "Basic realm=\"%s\"", ADMIN_AUTH_REALM);

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", authenticate_header);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    return httpd_resp_send(req, "Senha administrativa obrigatoria.", HTTPD_RESP_USE_STRLEN);
}

static bool admin_authorize_or_respond(httpd_req_t *req)
{
    bool has_auth_header = false;
    if (admin_is_request_authorized(req, &has_auth_header))
    {
        return true;
    }

    if (has_auth_header)
    {
        admin_redirect_to_public_root(req);
    }
    else
    {
        admin_send_auth_prompt(req);
    }
    return false;
}

static void admin_discard_request_body(httpd_req_t *req)
{
    int remaining = req->content_len;
    char scratch[64];

    while (remaining > 0)
    {
        int chunk_len = remaining < (int)sizeof(scratch) ? remaining : (int)sizeof(scratch);
        int received = httpd_req_recv(req, scratch, chunk_len);
        if (received > 0)
        {
            remaining -= received;
            continue;
        }

        if (received == HTTPD_SOCK_ERR_TIMEOUT)
        {
            continue;
        }

        break;
    }
}

static esp_err_t admin_read_post_body(httpd_req_t *req, char *body, size_t body_size)
{
    if (req == NULL || body == NULL || body_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (req->content_len <= 0)
    {
        body[0] = '\0';
        return ESP_OK;
    }

    if (req->content_len >= (int)body_size)
    {
        admin_discard_request_body(req);
        return ESP_ERR_INVALID_SIZE;
    }

    int total_received = 0;
    while (total_received < req->content_len)
    {
        int received = httpd_req_recv(req, body + total_received, req->content_len - total_received);
        if (received > 0)
        {
            total_received += received;
            continue;
        }

        if (received == HTTPD_SOCK_ERR_TIMEOUT)
        {
            continue;
        }

        return ESP_FAIL;
    }

    body[total_received] = '\0';
    return ESP_OK;
}

static bool admin_parse_bool(const char *value, bool *parsed_value)
{
    if (value == NULL || parsed_value == NULL)
    {
        return false;
    }

    if (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 || strcasecmp(value, "on") == 0 || strcasecmp(value, "yes") == 0)
    {
        *parsed_value = true;
        return true;
    }

    if (strcmp(value, "0") == 0 || strcasecmp(value, "false") == 0 || strcasecmp(value, "off") == 0 || strcasecmp(value, "no") == 0)
    {
        *parsed_value = false;
        return true;
    }

    return false;
}

static bool admin_parse_uint16(const char *value, uint16_t *parsed_value)
{
    if (value == NULL || parsed_value == NULL || value[0] == '\0')
    {
        return false;
    }

    char *end_ptr = NULL;
    long parsed_long = strtol(value, &end_ptr, 10);
    if (end_ptr == value || *end_ptr != '\0' || parsed_long < 0 || parsed_long > 65535)
    {
        return false;
    }

    *parsed_value = (uint16_t)parsed_long;
    return true;
}

static esp_err_t admin_send_state_json(httpd_req_t *req, const char *status_line, bool ok, const char *message)
{
    admin_state_snapshot_t snapshot = {0};
    admin_copy_snapshot(&snapshot);

    const char *status = status_line ? status_line : "200 OK";
    const char *msg = message ? message : "";
    const char *current_user = (snapshot.queue_len > 0) ? snapshot.queue_users[0] : "";

    char json[ADMIN_JSON_MAX];
    size_t offset = 0;
    json[0] = '\0';

    if (!admin_append_json_raw(json, sizeof(json), &offset, "{") ||
        !admin_append_json_raw(json, sizeof(json), &offset, "\"ok\":") ||
        !admin_append_json_raw(json, sizeof(json), &offset, ok ? "true" : "false") ||
        !admin_append_json_raw(json, sizeof(json), &offset, ",\"message\":") ||
        !admin_append_json_quoted(json, sizeof(json), &offset, msg) ||
        !admin_append_json_raw(json, sizeof(json), &offset, ",\"queue_mode_enabled\":") ||
        !admin_append_json_raw(json, sizeof(json), &offset, snapshot.queue_mode_enabled ? "true" : "false") ||
        !admin_append_json_raw(json, sizeof(json), &offset, ",\"rotation_paused\":") ||
        !admin_append_json_raw(json, sizeof(json), &offset, snapshot.rotation_paused ? "true" : "false"))
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao montar JSON");
    }

    char number_buffer[192];
    int number_written = snprintf(number_buffer,
                                  sizeof(number_buffer),
                                  ",\"default_turn_sec\":%u,\"min_turn_sec\":%u,\"max_turn_sec\":%u,\"max_users\":%u,\"ap_max_connections\":%u,\"queue_count\":%u,\"current_user\":",
                                  (unsigned)snapshot.default_turn_sec,
                                  (unsigned)snapshot.min_turn_sec,
                                  (unsigned)snapshot.max_turn_sec,
                                  (unsigned)snapshot.max_users,
                                  (unsigned)WIFI_AP_MAX_CONN,
                                  (unsigned)snapshot.queue_len);
    if (number_written < 0 || (size_t)number_written >= sizeof(number_buffer))
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao montar JSON");
    }

    if (!admin_append_json_raw(json, sizeof(json), &offset, number_buffer) ||
        !admin_append_json_quoted(json, sizeof(json), &offset, current_user) ||
        !admin_append_json_raw(json, sizeof(json), &offset, ",\"queue\":["))
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao montar JSON");
    }

    for (size_t i = 0; i < snapshot.queue_len; i++)
    {
        if (i > 0 && !admin_append_json_raw(json, sizeof(json), &offset, ","))
        {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao montar JSON");
        }

        if (!admin_append_json_quoted(json, sizeof(json), &offset, snapshot.queue_users[i]))
        {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao montar JSON");
        }
    }

    if (!admin_append_json_raw(json, sizeof(json), &offset, "]}"))
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao montar JSON");
    }

    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t admin_handle_post(httpd_req_t *req)
{
    char body[ADMIN_POST_BODY_MAX];
    esp_err_t read_err = admin_read_post_body(req, body, sizeof(body));
    if (read_err == ESP_ERR_INVALID_SIZE)
    {
        return admin_send_state_json(req, "413 Payload Too Large", false, "Corpo da requisicao muito grande");
    }
    if (read_err != ESP_OK)
    {
        return admin_send_state_json(req, "400 Bad Request", false, "Nao foi possivel ler o corpo da requisicao");
    }

    char action[40];
    if (httpd_query_key_value(body, "action", action, sizeof(action)) != ESP_OK)
    {
        return admin_send_state_json(req, "400 Bad Request", false, "Campo action obrigatorio");
    }

    if (strcmp(action, "status") == 0)
    {
        return admin_send_state_json(req, "200 OK", true, "Estado atualizado");
    }

    if (strcmp(action, "set_queue_mode") == 0)
    {
        char enabled_value[16];
        if (httpd_query_key_value(body, "enabled", enabled_value, sizeof(enabled_value)) != ESP_OK)
        {
            return admin_send_state_json(req, "400 Bad Request", false, "Campo enabled obrigatorio");
        }

        bool enabled = false;
        if (!admin_parse_bool(enabled_value, &enabled))
        {
            return admin_send_state_json(req, "400 Bad Request", false, "Valor de enabled invalido");
        }

        uint64_t now_us = (uint64_t)esp_timer_get_time();
        taskENTER_CRITICAL(&s_admin_state_mux);
        s_admin_state.queue_mode_enabled = enabled;
        if (enabled && s_admin_state.queue_len > 0 && s_admin_state.turn_started_us == 0)
        {
            s_admin_state.turn_started_us = now_us;
        }
        admin_refresh_rotation_locked(now_us);
        taskEXIT_CRITICAL(&s_admin_state_mux);

        return admin_send_state_json(req, "200 OK", true, enabled ? "Modo fila ativado" : "Modo fila desativado");
    }

    if (strcmp(action, "clear_queue") == 0)
    {
        taskENTER_CRITICAL(&s_admin_state_mux);
        s_admin_state.queue_len = 0;
        s_admin_state.turn_started_us = 0;
        for (size_t i = 0; i < ADMIN_QUEUE_CAPACITY; i++)
        {
            s_admin_state.queue_users[i][0] = '\0';
        }
        taskEXIT_CRITICAL(&s_admin_state_mux);

        return admin_send_state_json(req, "200 OK", true, "Fila limpa");
    }

    if (strcmp(action, "set_rotation") == 0)
    {
        char paused_value[16];
        if (httpd_query_key_value(body, "paused", paused_value, sizeof(paused_value)) != ESP_OK)
        {
            return admin_send_state_json(req, "400 Bad Request", false, "Campo paused obrigatorio");
        }

        bool paused = false;
        if (!admin_parse_bool(paused_value, &paused))
        {
            return admin_send_state_json(req, "400 Bad Request", false, "Valor de paused invalido");
        }

        uint64_t now_us = (uint64_t)esp_timer_get_time();
        taskENTER_CRITICAL(&s_admin_state_mux);
        s_admin_state.rotation_paused = paused;
        if (!paused && s_admin_state.queue_len > 0 && s_admin_state.turn_started_us == 0)
        {
            s_admin_state.turn_started_us = now_us;
        }
        admin_refresh_rotation_locked(now_us);
        taskEXIT_CRITICAL(&s_admin_state_mux);

        return admin_send_state_json(req, "200 OK", true, paused ? "Rotacao pausada" : "Rotacao retomada");
    }

    if (strcmp(action, "apply_policies") == 0)
    {
        char default_turn_value[16];
        char min_turn_value[16];
        char max_turn_value[16];
        char max_users_value[16];

        if (httpd_query_key_value(body, "default_turn_sec", default_turn_value, sizeof(default_turn_value)) != ESP_OK ||
            httpd_query_key_value(body, "min_turn_sec", min_turn_value, sizeof(min_turn_value)) != ESP_OK ||
            httpd_query_key_value(body, "max_turn_sec", max_turn_value, sizeof(max_turn_value)) != ESP_OK ||
            httpd_query_key_value(body, "max_users", max_users_value, sizeof(max_users_value)) != ESP_OK)
        {
            return admin_send_state_json(req, "400 Bad Request", false, "Todos os campos de politica sao obrigatorios");
        }

        uint16_t default_turn_sec = 0;
        uint16_t min_turn_sec = 0;
        uint16_t max_turn_sec = 0;
        uint16_t requested_max_users = 0;
        if (!admin_parse_uint16(default_turn_value, &default_turn_sec) ||
            !admin_parse_uint16(min_turn_value, &min_turn_sec) ||
            !admin_parse_uint16(max_turn_value, &max_turn_sec) ||
            !admin_parse_uint16(max_users_value, &requested_max_users))
        {
            return admin_send_state_json(req, "400 Bad Request", false, "Valores de politica invalidos");
        }

        uint64_t now_us = (uint64_t)esp_timer_get_time();
        bool max_users_was_clamped = false;
        taskENTER_CRITICAL(&s_admin_state_mux);
        s_admin_state.default_turn_sec = default_turn_sec;
        s_admin_state.min_turn_sec = min_turn_sec;
        s_admin_state.max_turn_sec = max_turn_sec;
        s_admin_state.max_users = requested_max_users;
        admin_apply_policy_guardrails_locked();
        max_users_was_clamped = s_admin_state.max_users < requested_max_users;
        admin_refresh_rotation_locked(now_us);
        taskEXIT_CRITICAL(&s_admin_state_mux);

        return admin_send_state_json(req,
                                     "200 OK",
                                     true,
                                     max_users_was_clamped ? "Politicas aplicadas (max usuarios ajustado ao limite de conexoes)" : "Politicas aplicadas");
    }

    return admin_send_state_json(req, "400 Bad Request", false, "Acao administrativa desconhecida");
}

static esp_err_t admin_handler(httpd_req_t *req)
{
    if (!admin_authorize_or_respond(req))
    {
        return ESP_OK;
    }

    if (req->method == HTTP_GET)
    {
        return send_file(req, "/spiffs/admin.html", "text/html");
    }

    if (req->method == HTTP_POST)
    {
        return admin_handle_post(req);
    }

    httpd_resp_set_status(req, "405 Method Not Allowed");
    return httpd_resp_send(req, "Metodo nao permitido", HTTPD_RESP_USE_STRLEN);
}

void register_captive_uri(httpd_handle_t server, const char *uri)
{
    httpd_uri_t any_uri = {
        .uri = uri,
        .method = HTTP_ANY,
        .handler = captive_redirect_handler,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &any_uri));
}

httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.server_port = 80;
    config.max_uri_handlers = 40;
    config.max_open_sockets = webserver_safe_max_open_sockets();
    config.backlog_conn = (config.max_open_sockets > 8) ? 8 : config.max_open_sockets;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    config.enable_so_linger = true;
    config.enable_so_linger = false;
    config.stack_size = 8192;

    httpd_handle_t server = NULL;
    esp_err_t start_err = httpd_start(&server, &config);
    if (start_err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "Falha ao iniciar HTTP server: %s (max_open_sockets=%u, CONFIG_LWIP_MAX_SOCKETS=%d)",
                 esp_err_to_name(start_err),
                 (unsigned)config.max_open_sockets,
                 CONFIG_LWIP_MAX_SOCKETS);
        return NULL;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t index_html_uri = {
        .uri = "/index.html",
        .method = HTTP_GET,
        .handler = index_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t css_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = css_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = js_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t gifuct_uri = {
        .uri = "/gifuct-js.min.js",
        .method = HTTP_GET,
        .handler = gifuct_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t spfc_gif_uri = {
        .uri = "/Complemento/spfc.gif",
        .method = HTTP_GET,
        .handler = spfc_gif_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t matrix_uri = {
        .uri = "/matrix",
        .method = HTTP_GET,
        .handler = matrix_ws_handler,
        .is_websocket = true,
        .user_ctx = NULL,
    };

    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t admin_get_uri = {
        .uri = ADMIN_URI,
        .method = HTTP_GET,
        .handler = admin_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t admin_post_uri = {
        .uri = ADMIN_URI,
        .method = HTTP_POST,
        .handler = admin_handler,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &index_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &index_html_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &css_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &js_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &gifuct_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &spfc_gif_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &matrix_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &favicon_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &admin_get_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &admin_post_uri));

    // Mantém compatibilidade com iPhone/macOS e outros clientes
    register_captive_uri(server, "/generate_204");
    register_captive_uri(server, "/gen_204");
    register_captive_uri(server, "/hotspot-detect.html");
    register_captive_uri(server, "/connecttest.txt");
    register_captive_uri(server, "/ncsi.txt");
    register_captive_uri(server, "/fwlink");
    register_captive_uri(server, "/success.txt");
    register_captive_uri(server, "/library/test/success.html");

    // Catch-all por último
    register_captive_uri(server, "/*");

    ESP_LOGI(TAG,
             "Servidor HTTP iniciado na porta %d (max_open_sockets=%u, backlog=%u, lru_purge=%s)",
             config.server_port,
             (unsigned)config.max_open_sockets,
             (unsigned)config.backlog_conn,
             config.lru_purge_enable ? "on" : "off");
    return server;
}

esp_err_t matrix_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Cliente WebSocket conectado em /matrix");
        return ESP_OK;
    }

    char sender_id[ADMIN_USER_ID_MAX_LEN];
    if (!admin_get_client_identifier(req, sender_id, sizeof(sender_id)))
    {
        strlcpy(sender_id, "desconhecido", sizeof(sender_id));
    }

    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = NULL,
        .len = 0,
    };

    esp_err_t err = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao ler tamanho do frame WS: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t rgb_frame[MATRIX_RGB_FRAME_SIZE];

    if (ws_pkt.len == MATRIX_RGB_FRAME_SIZE)
    {
        ws_pkt.payload = rgb_frame;
        err = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Falha ao ler frame WS: %s", esp_err_to_name(err));
            return err;
        }

        if (ws_pkt.type != HTTPD_WS_TYPE_BINARY)
        {
            ESP_LOGW(TAG, "Frame de %u bytes recebido sem tipo binario (tipo=%d)", (unsigned)ws_pkt.len, ws_pkt.type);
            return ESP_ERR_INVALID_ARG;
        }
    }
    else
    {
        if (ws_pkt.len > MAX_MATRIX_BODY_SIZE)
        {
            ESP_LOGE(TAG, "Frame WS invalido: %u bytes (maximo %u)", (unsigned)ws_pkt.len, (unsigned)MAX_MATRIX_BODY_SIZE);
            return ESP_ERR_INVALID_SIZE;
        }

        uint8_t *raw_payload = (uint8_t *)malloc(ws_pkt.len + 1);
        if (raw_payload == NULL)
        {
            ESP_LOGE(TAG, "Sem memoria para frame WS de %u bytes", (unsigned)ws_pkt.len);
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = raw_payload;
        err = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (err != ESP_OK)
        {
            free(raw_payload);
            ESP_LOGE(TAG, "Falha ao ler frame WS legado: %s", esp_err_to_name(err));
            return err;
        }

        if (ws_pkt.type != HTTPD_WS_TYPE_TEXT)
        {
            ESP_LOGW(TAG, "Frame WS nao suportado: tipo=%d len=%u", ws_pkt.type, (unsigned)ws_pkt.len);
            free(raw_payload);
            return ESP_ERR_INVALID_ARG;
        }

        err = parse_legacy_text_matrix_to_rgb(raw_payload, ws_pkt.len, rgb_frame);
        free(raw_payload);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Frame WS texto invalido: %u bytes (esperado binario de %u ou texto legado parseavel)",
                     (unsigned)ws_pkt.len,
                     (unsigned)MATRIX_RGB_FRAME_SIZE);
            return err;
        }

        ESP_LOGW(TAG, "Cliente legado detectado: frame texto %u bytes convertido para RGB binario", (unsigned)ws_pkt.len);
    }

    if (!admin_record_sender_and_check_turn(sender_id))
    {
        ESP_LOGW(TAG, "Frame descartado: usuario '%s' fora do turno atual", sender_id);
        return ESP_OK;
    }

    err = matrix_queue_push(rgb_frame, MATRIX_RGB_FRAME_SIZE);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Falha ao enfileirar frame RGB: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}
