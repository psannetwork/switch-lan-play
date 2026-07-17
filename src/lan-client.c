#include "lan-play.h"
#include "sha1.h"

enum lan_client_type {
    LAN_CLIENT_TYPE_KEEPALIVE = 0x00,
    LAN_CLIENT_TYPE_IPV4 = 0x01,
    LAN_CLIENT_TYPE_PING = 0x02,
    LAN_CLIENT_TYPE_IPV4_FRAG = 0x03,
    LAN_CLIENT_TYPE_AUTH_ME = 0x04,
    LAN_CLIENT_TYPE_INFO = 0x10,
};

// Forward declarations
int lan_client_send(struct lan_play *lan_play, uint8_t type, const uint8_t *packet, uint16_t len);

static uint64_t last_ping_time = 0;

int lan_client_send_ping(struct lan_play *lan_play) {
    last_ping_time = uv_hrtime();
    return lan_client_send(lan_play, LAN_CLIENT_TYPE_PING, NULL, 0);
}

int lan_client_send_info(struct lan_play *lan_play) {
    return lan_client_send(lan_play, LAN_CLIENT_TYPE_INFO, NULL, 0);
}

void lan_client_ping_timer(uv_timer_t *handle) {
    struct lan_play *lan_play = (struct lan_play *)handle->data;
    lan_client_send_ping(lan_play);
}

void lan_client_info_timer(uv_timer_t *handle) {
    struct lan_play *lan_play = (struct lan_play *)handle->data;
    lan_client_send_info(lan_play);
}

struct lan_client_fragment_header {
    uint8_t src[4];
    uint8_t dst[4];
    uint16_t id;
    uint8_t part;
    uint8_t total_part;
    uint16_t len;
    uint16_t pmtu;
};
#define LC_FRAG_SRC 0
#define LC_FRAG_DST 4
#define LC_FRAG_ID 8
#define LC_FRAG_PART 10
#define LC_FRAG_TOTAL_PART 11
#define LC_FRAG_LEN 12
#define LC_FRAG_PMTU 14
#define LC_FRAG_HEADER_LEN 16

struct ipv4_req {
    uv_udp_send_t req;
    char *packet;
};
uint8_t BROADCAST_MAC[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

void lan_client_keepalive_timer(uv_timer_t *handle);
void lan_client_real_broadcast_timer(uv_timer_t *handle);
int lan_client_send_keepalive(struct lan_play *lan_play);
int lan_client_send_ipv4(struct lan_play *lan_play, void *dst_ip, const void *packet, uint16_t len);
int lan_client_send_auth_me(struct lan_play *lan_play, const void *packet, uint16_t len);
void lan_client_on_recv_internal(struct lan_play *lan_play, uint8_t *buffer, uint16_t recv_len);
void lan_client_on_recv_udp(uv_udp_t *handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags);
void lan_client_on_recv_stream(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);

static void lan_client_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    struct lan_play *lan_play = handle->data;
    buf->base = (char *)lan_play->client_buf;
    buf->len = sizeof(lan_play->client_buf);
}

int lan_client_init(struct lan_play *lan_play)
{
    int ret;
    uv_loop_t *loop = lan_play->loop;
    
    lan_play->is_tcp = !strcmp(options.protocol, "tcp");
    
    uv_timer_t *client_keepalive_timer = &lan_play->client_keepalive_timer;
    uv_timer_t *real_broadcast_timer = &lan_play->real_broadcast_timer;

    if (lan_play->pmtu) {
        if (lan_play->pmtu < MIN_FRAG_PAYLOAD_LEN) {
            LLOG(LLOG_DEBUG, "pmtu is too small: %d, must be greater than %d", lan_play->pmtu, MIN_FRAG_PAYLOAD_LEN);
            exit(1);
        }
        LLOG(LLOG_DEBUG, "pmtu is set to %d", lan_play->pmtu);
    }
    lan_play->frag_id = 0;
    lan_play->local_id = 0;
    lan_play->next_real_broadcast = true;
    memset(&lan_play->frags, 0, sizeof(lan_play->frags));

    if (lan_play->is_tcp) {
        lan_play->tcp_recv_buf = circular_buffer_create(65536);
        ret = uv_tcp_init(loop, &lan_play->client.tcp);
        if (ret != 0) LLOG(LLOG_ERROR, "uv_tcp_init %d", ret);
        lan_play->client.tcp.data = lan_play;
        
        uv_connect_t *connect = malloc(sizeof(uv_connect_t));
        ret = uv_tcp_connect(connect, &lan_play->client.tcp, (const struct sockaddr *)&lan_play->server_addr.u.addr, NULL);
        if (ret != 0) LLOG(LLOG_ERROR, "uv_tcp_connect %d", ret);
        
        ret = uv_read_start((uv_stream_t*)&lan_play->client.tcp, lan_client_alloc_cb, lan_client_on_recv_stream);
    } else {
        ret = uv_udp_init(loop, &lan_play->client.udp);
        if (ret != 0) LLOG(LLOG_ERROR, "uv_udp_init %d", ret);
        lan_play->client.udp.data = lan_play;

        if (lan_play->broadcast) {
            struct sockaddr_in temp;
            uv_ip4_addr("0.0.0.0", 11451, &temp);
            ret = uv_udp_bind(&lan_play->client.udp, (struct sockaddr *)&temp, 0);
            if (ret != 0) LLOG(LLOG_ERROR, "uv_udp_bind %d", ret);
        } else {
            if (lan_play->server_addr.sin_family == AF_INET6) {
                struct sockaddr_in6 temp;
                uv_ip6_addr("0.0.0.0", 0, &temp);
                ret = uv_udp_bind(&lan_play->client.udp, (struct sockaddr *)&temp, 0);
                if (ret != 0) LLOG(LLOG_ERROR, "uv_udp_bind v6 %d", ret);
            }
        }
        ret = uv_udp_recv_start(&lan_play->client.udp, lan_client_alloc_cb, lan_client_on_recv_udp);
    }

    ret = uv_timer_init(loop, client_keepalive_timer);
    if (ret != 0) LLOG(LLOG_ERROR, "uv_timer_init %d", ret);

    ret = uv_timer_init(loop, real_broadcast_timer);
    if (ret != 0) LLOG(LLOG_ERROR, "uv_timer_init %d", ret);

    client_keepalive_timer->data = lan_play;
    real_broadcast_timer->data = lan_play;

    printf("Server IP: %s\n", ip2str(&lan_play->server_addr));

    ret = uv_timer_start(client_keepalive_timer, lan_client_keepalive_timer, 0, 10 * 1000);
    if (ret != 0) {
        LLOG(LLOG_ERROR, "uv_timer_start %d", ret);
        return ret;
    }

    ret = uv_timer_start(real_broadcast_timer, lan_client_real_broadcast_timer, 0, 1000);
    if (ret != 0) {
        LLOG(LLOG_ERROR, "uv_timer_start %d", ret);
        return ret;
    }

    ret = uv_timer_init(loop, &lan_play->ping_timer);
    lan_play->ping_timer.data = lan_play;
    uv_timer_start(&lan_play->ping_timer, lan_client_ping_timer, 1000, 5000);

    ret = uv_timer_init(loop, &lan_play->info_timer);
    lan_play->info_timer.data = lan_play;
    uv_timer_start(&lan_play->info_timer, lan_client_info_timer, 2000, 10000);

    lan_play->upload_byte = 0;
    lan_play->download_byte = 0;
    lan_play->upload_packet = 0;
    lan_play->download_packet = 0;

    return ret;
}

int lan_client_close(struct lan_play *lan_play)
{
    int ret;

    if (lan_play->is_tcp) {
        ret = uv_read_stop((uv_stream_t*)&lan_play->client.tcp);
    } else {
        ret = uv_udp_recv_stop(&lan_play->client.udp);
    }
    if (ret != 0) {
        LLOG(LLOG_ERROR, "recv_stop %d", ret);
        return ret;
    }

    ret = uv_timer_stop(&lan_play->client_keepalive_timer);
    if (ret != 0) {
        LLOG(LLOG_ERROR, "uv_timer_stop %d", ret);
        return ret;
    }

    ret = uv_timer_stop(&lan_play->real_broadcast_timer);
    if (ret != 0) {
        LLOG(LLOG_ERROR, "real_broadcast uv_timer_stop %d", ret);
        return ret;
    }

    uv_close((uv_handle_t *)&lan_play->client, NULL);
    uv_close((uv_handle_t *)&lan_play->client_keepalive_timer, NULL);
    if (lan_play->is_tcp && lan_play->tcp_recv_buf) {
        circular_buffer_destroy(lan_play->tcp_recv_buf);
        lan_play->tcp_recv_buf = NULL;
    }

    return 0;
}

int lan_client_arp_for_each_cb(void *p, const struct arp_item *item)
{
    struct {
        struct lan_play *lan_play;
        const uint8_t *packet;
        uint16_t len;
    } *userdata = p;

    struct payload part;

    part.ptr = userdata->packet;
    part.len = userdata->len;
    part.next = NULL;
    int rc = send_ether(
        &userdata->lan_play->packet_ctx,
        item->mac,
        ETHER_TYPE_IPV4,
        &part
    );
    if (rc != 0) {
        LLOG(LLOG_ERROR, "Failed to call send_ether in lan_client_arp_for_each_cb");
    }

    return 0;
}

int lan_client_on_broadcast(struct lan_play *lan_play, const uint8_t *packet, uint16_t len)
{
    if (lan_play->next_real_broadcast) {
        lan_play->next_real_broadcast = false;

        struct payload part;

        part.ptr = packet;
        part.len = len;
        part.next = NULL;
        return send_ether(
            &lan_play->packet_ctx,
            BROADCAST_MAC,
            ETHER_TYPE_IPV4,
            &part
        );
    } else {
        struct {
            struct lan_play *lan_play;
            const uint8_t *packet;
            uint16_t len;
        } userdata;
        userdata.lan_play = lan_play;
        userdata.packet = packet;
        userdata.len = len;
        arp_for_each(&lan_play->packet_ctx, &userdata, lan_client_arp_for_each_cb);
    }
    return 0;
}

int lan_client_process(struct lan_play *lan_play, const uint8_t *packet, uint16_t len)
{
    if (len == 0) {
        return 0;
    }
    uint8_t dst_mac[6];
    const uint8_t *dst = packet + IPV4_OFF_DST;
    struct payload part;

    if (IS_BROADCAST(dst, lan_play->packet_ctx.subnet_net, lan_play->packet_ctx.subnet_mask)) {
        return lan_client_on_broadcast(lan_play, packet, len);
    } else if (!arp_get_mac_by_ip(&lan_play->packet_ctx, dst_mac, dst)) {
        return 0;
    }

    part.ptr = packet;
    part.len = len;
    part.next = NULL;
    return send_ether(
        &lan_play->packet_ctx,
        dst_mac,
        ETHER_TYPE_IPV4,
        &part
    );
}

int lan_client_process_frag(struct lan_play *lan_play, const uint8_t *packet, uint16_t len)
{
    struct lan_client_fragment *frags = lan_play->frags;
    struct lan_client_fragment_header header;
    CPY_IPV4(header.src, packet + LC_FRAG_SRC);
    CPY_IPV4(header.dst, packet + LC_FRAG_DST);
    header.id = READ_NET16(packet, LC_FRAG_ID);
    header.part = READ_NET8(packet, LC_FRAG_PART);
    header.total_part = READ_NET8(packet, LC_FRAG_TOTAL_PART);
    header.len = READ_NET16(packet, LC_FRAG_LEN);
    header.pmtu = READ_NET16(packet, LC_FRAG_PMTU);

    struct lan_client_fragment *frag = NULL;
    int i;
    for (i = 0; i < LC_FRAG_COUNT; i++) {
        if (frags[i].used
                && (frags[i].id == header.id)
                && CMP_IPV4(frags[i].src, header.src)) {
            frag = &frags[i];
            break;
        }
    }

    if (!frag) {
        for (i = 0; i < LC_FRAG_COUNT; i++) {
            if (!frags[i].used) {
                frag = &frags[i];
                frag->used = 1;
                frag->id = header.id;
                frag->local_id = lan_play->local_id++;
                CPY_IPV4(frag->src, header.src);
                frag->part = 0;
                break;
            }
        }
    }

    if (!frag) {
        int max_dif = 0;
        struct lan_client_fragment *to_delete = NULL;
        for (i = 0; i < LC_FRAG_COUNT; i++) {
            if (frags[i].used) {
                int dif = LABS(frags[i].local_id - lan_play->local_id);
                if (dif > max_dif) {
                    max_dif = dif;
                    to_delete = &frags[i];
                }
            }
        }
        if (max_dif > LC_FRAG_COUNT) {
            frag = to_delete;
            frag->used = 1;
            frag->id = header.id;
            frag->local_id = lan_play->local_id++;
            CPY_IPV4(frag->src, header.src);
            frag->part = 0;
        }
    }

    if (frag) {
        frag->part |= 1 << header.part;
        memcpy(&frag->buffer[header.pmtu * header.part], packet + LC_FRAG_HEADER_LEN, header.len);
        if (header.part == header.total_part - 1) {
            frag->total_len = (header.total_part - 1) * header.pmtu + header.len;
        }
        if (~(~0 << header.total_part) == frag->part) {
            frag->used = 0;
            return lan_client_process(lan_play, frag->buffer, frag->total_len);
        }
    } else {
        LLOG(LLOG_WARNING, "fragment buffer is full, ignore it");
    }

    return 0;
}

void lan_client_real_broadcast_timer(uv_timer_t *handle)
{
    struct lan_play *lan_play = (struct lan_play *)handle->data;
    lan_play->next_real_broadcast = true;
}

void lan_client_keepalive_timer(uv_timer_t *handle)
{
    struct lan_play *lan_play = (struct lan_play *)handle->data;
    lan_client_send_keepalive(lan_play);
}

void lan_client_process_auth_me(struct lan_play *lan_play, const uint8_t *packet, uint16_t len)
{
    if (lan_play->username) {
        uint8_t auth_type = packet[0];
        if (auth_type == 0) {
            const uint8_t *challenge = packet + 1;
            uint16_t challenge_len = len - 1;
            uint16_t username_len = strlen(lan_play->username);
            uint16_t response_len = 20 + username_len;
            uint8_t *response = malloc(response_len);
            SHA1_CTX hashctx;
            SHA1Init(&hashctx);
            SHA1Update(&hashctx, (const unsigned char *)lan_play->key, 20);
            SHA1Update(&hashctx, challenge, challenge_len);
            SHA1Final(response, &hashctx);
            memcpy(response + 20, lan_play->username, username_len);
            lan_client_send_auth_me(lan_play, response, response_len);
            free(response);
        } else {
            LLOG(LLOG_WARNING, "unknown auth type %d", auth_type);
        }
    } else {
        printf("The server ask us to login. Please re-run client with username and password\n");
    }
}

void lan_client_on_recv_udp(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags)
{
    if (nread <= 0) return;
    struct lan_play *lan_play = (struct lan_play *)handle->data;
    lan_client_on_recv_internal(lan_play, (uint8_t *)buf->base, (uint16_t)nread);
}

void lan_client_on_recv_stream(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    if (nread > 0) {
        struct lan_play *lan_play = (struct lan_play *)stream->data;
        circular_buffer_push(lan_play->tcp_recv_buf, (uint8_t*)buf->base, nread);

        while (circular_buffer_size(lan_play->tcp_recv_buf) >= 2) {
            uint8_t len_buf[2];
            circular_buffer_read(lan_play->tcp_recv_buf, len_buf, 2);
            uint16_t packet_len = (len_buf[0] << 8) | len_buf[1];

            if (circular_buffer_size(lan_play->tcp_recv_buf) < 2 + packet_len) {
                break;
            }

            circular_buffer_discard(lan_play->tcp_recv_buf, 2);

            uint8_t *packet = malloc(packet_len);
            if (!packet) {
                LLOG(LLOG_ERROR, "malloc failed for packet");
                break;
            }
            circular_buffer_read(lan_play->tcp_recv_buf, packet, packet_len);
            circular_buffer_discard(lan_play->tcp_recv_buf, packet_len);

            lan_client_on_recv_internal(lan_play, packet, packet_len);
            free(packet);
        }
    }
}


void lan_client_on_recv_internal(struct lan_play *lan_play, uint8_t *buffer, uint16_t recv_len)
{
    uint8_t type = buffer[0] & 0x7f;
    lan_play->download_packet++;
    lan_play->download_byte += recv_len;

    switch (type) {
    case LAN_CLIENT_TYPE_KEEPALIVE:
        break;
    case LAN_CLIENT_TYPE_IPV4:
        lan_client_process(lan_play, buffer + 1, recv_len - 1);
        break;
    case LAN_CLIENT_TYPE_PING: {
        uint64_t now = uv_hrtime();
        uint64_t latency = (now - last_ping_time) / 1000000; // ms
        FILE *f = fopen("latency.txt", "w");
        if (f) {
            fprintf(f, "%llu", (unsigned long long)latency);
            fclose(f);
        }
        break;
    }
    case LAN_CLIENT_TYPE_IPV4_FRAG:
        lan_client_process_frag(lan_play, buffer + 1, recv_len - 1);
        break;
    case LAN_CLIENT_TYPE_AUTH_ME:
        lan_client_process_auth_me(lan_play, buffer + 1, recv_len - 1);
        break;
    case LAN_CLIENT_TYPE_INFO: {
        FILE *f = fopen("server_info.txt", "w");
        if (f) {
            fprintf(f, "%.*s", recv_len - 1, buffer + 1);
            fclose(f);
        }
        printf("[Server]: %.*s\n", recv_len - 1, buffer + 1);
        break;
    }
    }
}

static int lan_client_send_raw(struct lan_play *lan_play, uv_buf_t *bufs, int bufs_len)
{
    int ret;
    int total_len = 0;
    for (int i = 0; i < bufs_len; i++) total_len += bufs[i].len;
    if (total_len == 0) return 0;

    if (lan_play->is_tcp) {
        // Add 2-byte length prefix (network byte order)
        uint16_t len_prefix = htons(total_len);
        
        uv_buf_t header_buf = uv_buf_init((char *)&len_prefix, 2);
        
        // Prepare scatter/gather array: [len_prefix, ...original bufs]
        uv_buf_t *all_bufs = malloc(sizeof(uv_buf_t) * (bufs_len + 1));
        all_bufs[0] = header_buf;
        for (int i = 0; i < bufs_len; i++) all_bufs[i + 1] = bufs[i];
        
        ret = uv_write(&lan_play->tcp_write_req, (uv_stream_t*)&lan_play->client.tcp, all_bufs, bufs_len + 1, NULL);
        free(all_bufs);
    } else {
        // UDP sends can just use the provided bufs directly
        uv_udp_send_t *req = malloc(sizeof(uv_udp_send_t));
        ret = uv_udp_send(req, &lan_play->client.udp, bufs, bufs_len, (const struct sockaddr *)&lan_play->server_addr.u.addr, NULL);
    }

    lan_play->upload_packet++;
    lan_play->upload_byte += total_len;

    return ret;
}

int lan_client_send(struct lan_play *lan_play, uint8_t type, const uint8_t *packet, uint16_t len)
{
    uv_buf_t bufs[3];
    bufs[0] = uv_buf_init((char *)&type, sizeof(type));

    int pmtu = lan_play->pmtu;
    if (type == LAN_CLIENT_TYPE_IPV4 && pmtu > 0) {
        int ret = 0, i, pos;
        int total_part = (len / pmtu) + ((pmtu * (len / pmtu) < len) ? 1 : 0);
        if (total_part > 1) {
            type = LAN_CLIENT_TYPE_IPV4_FRAG;
            int id = lan_play->frag_id++;
            uint8_t header[LC_FRAG_HEADER_LEN];
            CPY_IPV4(header + LC_FRAG_SRC, packet + IPV4_OFF_SRC);
            CPY_IPV4(header + LC_FRAG_DST, packet + IPV4_OFF_DST);
            WRITE_NET8(header, LC_FRAG_TOTAL_PART, total_part);
            WRITE_NET16(header, LC_FRAG_PMTU, pmtu);
            bufs[1] = uv_buf_init((char *)&header, sizeof(header));
            i = 0; pos = 0;
            while (pos < len) {
                int part_len = LMIN(pmtu, len - pos);
                WRITE_NET16(header, LC_FRAG_ID, id);
                WRITE_NET8(header, LC_FRAG_PART, i);
                WRITE_NET16(header, LC_FRAG_LEN, part_len);
                bufs[2] = uv_buf_init((char *)(packet + pos), part_len);
                ret = lan_client_send_raw(lan_play, bufs, 3);
                if (ret) return ret;
                i += 1; pos += part_len;
            }
            return 0;
        }
    }
    bufs[1] = uv_buf_init((char *)packet, len);
    return lan_client_send_raw(lan_play, bufs, 2);
}

int lan_client_send_keepalive(struct lan_play *lan_play) { return lan_client_send(lan_play, LAN_CLIENT_TYPE_KEEPALIVE, NULL, 0); }
int lan_client_send_ipv4(struct lan_play *lan_play, void *dst_ip, const void *packet, uint16_t len) { return lan_client_send(lan_play, LAN_CLIENT_TYPE_IPV4, packet, len); }
int lan_client_send_auth_me(struct lan_play *lan_play, const void *packet, uint16_t len) { return lan_client_send(lan_play, LAN_CLIENT_TYPE_AUTH_ME, packet, len); }
