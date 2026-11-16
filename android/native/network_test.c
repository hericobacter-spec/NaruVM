/* Authored Ethernet client; no disk, guest OS, account or user traffic is read.
 * Exercises the production NAT/DNS backend using DHCP, ARP and a temporary
 * loopback HTTP server. Only the explicit DNS fixture names leave the app. */
#include "net_slirp.h"
#include "dns_proxy.h"
#include <arpa/inet.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static HvMachine machine;
static HvVirtioNet net;
static const uint8_t mac[6] = {2, 0x48, 0x56, 0x4d, 0, 1};
static uint8_t gateway_mac[6] = {0x52, 0x55, 10, 0, 2, 2};
static int offer, ack, arp, dns_a, dns_aaaa, dns_negative, bad_packet, syn_ack, http_ok;
static uint32_t peer_seq, next_ack;
static uint16_t server_port;
static int failures, checks;
static uint16_t rd16(const uint8_t *p) { return p[0] << 8 | p[1]; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) << 16 | rd16(p + 2); }
static void wr16(uint8_t *p, uint32_t v) { p[0] = v >> 8; p[1] = v; }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v >> 16); wr16(p + 2, v); }
static uint16_t checksum(const uint8_t *p, size_t n) {
    uint32_t s = 0;
    for (size_t i = 0; i < n; i += 2) s += (uint32_t)p[i] << 8 | (i + 1 < n ? p[i + 1] : 0);
    while (s >> 16) s = (s & 65535) + (s >> 16);
    return ~s;
}
static int64_t now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void check(const char *name, int ok) { checks++; failures += !ok; printf("%s %s\n", ok ? "PASS" : "FAIL", name); }
void hv_machine_wake(HvMachine *m) { (void)m; } /* fixture has no emulated CPU */

int hv_virtio_net_receive(HvVirtioNet *unused, const uint8_t *f, uint32_t len) {
    if (len < 42) return 1;
    if (rd16(f + 12) == 0x0806 && rd16(f + 20) == 2 && rd32(f + 28) == 0x0a000202) {
        memcpy(gateway_mac, f + 22, 6); arp = 1; return 1;
    }
    if (rd16(f + 12) != 0x0800) return 1;
    const uint8_t *ip = f + 14;
    uint32_t ihl = (ip[0] & 15) * 4, total = rd16(ip + 2);
    if (ihl < 20 || total < ihl || total > len - 14 || checksum(ip, ihl)) { bad_packet++; return 1; }
    const uint8_t *p = ip + ihl;
    if (ip[9] == 17 && total >= ihl + 8) {
        uint32_t n = rd16(p + 4);
        if (n < 8 || n > total - ihl) { bad_packet++; return 1; }
        if (rd16(p) == 67 && rd16(p + 2) == 68 && n >= 8 + 240) {
            const uint8_t *bootp = p + 8;
            if (rd32(bootp + 4) != 0x48454e47 || rd32(bootp + 16) != 0x0a00020f) return 1;
            int type = 0, dns = 0;
            for (uint32_t i = 240; i < n - 8;) {
                uint8_t tag = bootp[i++];
                if (tag == 255) break;
                if (!tag) continue;
                if (i >= n - 8) break;
                uint8_t size = bootp[i++];
                if (i + size > n - 8) break;
                if (tag == 53 && size == 1) type = bootp[i];
                if (tag == 6 && size >= 4 && rd32(bootp + i) == 0x0a000203) dns = 1;
                i += size;
            }
            if (type == 2 && dns) offer = 1;
            if (type == 5 && dns) ack = 1;
        } else if (rd16(p) == 53 && n >= 20) {
            const uint8_t *d = p + 8;
            if (!(d[2] & 0x80)) return 1;
            if (rd16(d) == 0x4841 && !(d[3] & 15) && rd16(d + 6)) dns_a = 1;
            if (rd16(d) == 0x4842 && !(d[3] & 15)) dns_aaaa = 1;
            if (rd16(d) == 0x4843 && (d[3] & 15) == 3) dns_negative = 1;
        }
    } else if (ip[9] == 6 && total >= ihl + 20 && rd16(p) == server_port && rd16(p + 2) == 41000) {
        uint32_t th = (p[12] >> 4) * 4, n = total - ihl;
        if (th < 20 || th > n) { bad_packet++; return 1; }
        if ((p[13] & 0x12) == 0x12 && rd32(p + 8) == 1001) { peer_seq = rd32(p + 4); syn_ack = 1; }
        if (n > th && memmem(p + th, n - th, "HENGE_NAT_OK", 12)) http_ok = 1;
        if (n > th || (p[13] & 1)) next_ack = rd32(p + 4) + n - th + !!(p[13] & 1);
    }
    return 1;
}
static void wait_flag(int *flag, unsigned ms) {
    int64_t end = now_ms() + ms;
    do { hv_net_poll(); if (*flag) return; usleep(10000); } while (now_ms() < end);
}
static void ethernet(uint8_t *f, int broadcast) {
    if (broadcast) memset(f, 255, 6); else memcpy(f, gateway_mac, 6);
    memcpy(f + 6, mac, 6); wr16(f + 12, 0x0800);
}
static void udp_send(uint16_t sp, uint16_t dp, const uint8_t *data, uint32_t n, int dhcp) {
    uint8_t f[1600] = {0}; ethernet(f, dhcp);
    uint8_t *ip = f + 14, *udp = ip + 20;
    ip[0] = 0x45; wr16(ip + 2, 28 + n); ip[8] = 64; ip[9] = 17;
    wr32(ip + 12, dhcp ? 0 : 0x0a00020f); wr32(ip + 16, dhcp ? 0xffffffff : 0x0a000203);
    wr16(ip + 10, checksum(ip, 20));
    wr16(udp, sp); wr16(udp + 2, dp); wr16(udp + 4, 8 + n); memcpy(udp + 8, data, n);
    net.transmit(NULL, f, 42 + n);
}
static void dhcp_send(int request) {
    uint8_t b[300] = {1, 1, 6, 0};
    wr32(b + 4, 0x48454e47); wr16(b + 10, 0x8000); memcpy(b + 28, mac, 6);
    wr32(b + 236, 0x63825363);
    uint32_t n = 240;
    b[n++] = 53; b[n++] = 1; b[n++] = request ? 3 : 1;
    if (request) {
        b[n++] = 50; b[n++] = 4; wr32(b + n, 0x0a00020f); n += 4;
        b[n++] = 54; b[n++] = 4; wr32(b + n, 0x0a000202); n += 4;
    }
    b[n++] = 55; b[n++] = 3; b[n++] = 1; b[n++] = 3; b[n++] = 6; b[n++] = 255;
    udp_send(68, 67, b, sizeof b, 1);
}
static void arp_send(void) {
    uint8_t f[42] = {0}; memset(f, 255, 6); memcpy(f + 6, mac, 6); wr16(f + 12, 0x0806);
    wr16(f + 14, 1); wr16(f + 16, 0x0800); f[18] = 6; f[19] = 4; wr16(f + 20, 1);
    memcpy(f + 22, mac, 6); wr32(f + 28, 0x0a00020f); wr32(f + 38, 0x0a000202);
    net.transmit(NULL, f, sizeof f);
}
static void dns_send(uint16_t id, const char *name, uint16_t type) {
    uint8_t d[512] = {0}; wr16(d, id); d[2] = 1; wr16(d + 4, 1);
    size_t n = 12;
    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.'); size_t size = dot ? (size_t)(dot - p) : strlen(p);
        d[n++] = size; memcpy(d + n, p, size); n += size; p += size; if (*p) p++;
    }
    d[n++] = 0; wr16(d + n, type); n += 2; wr16(d + n, 1); n += 2;
    udp_send(42000 + (id & 3), 53, d, n, 0);
}
static void tcp_send(uint8_t flags, uint32_t seq, uint32_t ackno, const char *body) {
    uint8_t f[1600] = {0}, pseudo[1600] = {0}; ethernet(f, 0);
    uint8_t *ip = f + 14, *tcp = ip + 20; size_t n = body ? strlen(body) : 0;
    ip[0] = 0x45; wr16(ip + 2, 40 + n); ip[8] = 64; ip[9] = 6;
    wr32(ip + 12, 0x0a00020f); wr32(ip + 16, 0x0a000202); wr16(ip + 10, checksum(ip, 20));
    wr16(tcp, 41000); wr16(tcp + 2, server_port); wr32(tcp + 4, seq); wr32(tcp + 8, ackno);
    tcp[12] = 0x50; tcp[13] = flags; wr16(tcp + 14, 32768); if (n) memcpy(tcp + 20, body, n);
    memcpy(pseudo, ip + 12, 8); pseudo[9] = 6; wr16(pseudo + 10, 20 + n); memcpy(pseudo + 12, tcp, 20 + n);
    wr16(tcp + 16, checksum(pseudo, 32 + n)); net.transmit(NULL, f, 54 + n);
}
static void *http_server(void *opaque) {
    int listener = *(int *)opaque;
    struct pollfd p = {listener, POLLIN, 0};
    if (poll(&p, 1, 6000) <= 0) return NULL;
    int c = accept(listener, NULL, NULL);
    if (c >= 0) {
        p = (struct pollfd){c, POLLIN, 0};
        if (poll(&p, 1, 3000) > 0) {
            char b[512];
            if (read(c, b, sizeof b) > 0) {
                const char response[] = "HTTP/1.0 200 OK\r\nContent-Length: 12\r\n\r\nHENGE_NAT_OK";
                send(c, response, sizeof response - 1, MSG_NOSIGNAL);
            }
        }
        close(c);
    }
    return NULL;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); alarm(40);
    printf("GUEST_NETWORK_FIXTURE authored=1 disk_opened=0 inbound_forwarding=0\n");
    int attached = hv_net_attach(&machine, &net) == 0;
    check("production backend attach", attached);
    if (!attached) return 1;
    check("reject duplicate attach", hv_net_attach(&machine, &net) != 0);
    dhcp_send(0); wait_flag(&offer, 3000); check("DHCP offer with Android DNS gateway", offer);
    dhcp_send(1); wait_flag(&ack, 3000); check("DHCP ACK 10.0.2.15", ack);
    arp_send(); wait_flag(&arp, 3000); check("gateway ARP", arp);
    dns_send(0x4841, "gs.apple.com", 1); wait_flag(&dns_a, 6000); check("Android resolver A via Ethernet", dns_a);
    dns_send(0x4842, "gs.apple.com", 28); wait_flag(&dns_aaaa, 6000); check("Android resolver AAAA response", dns_aaaa);
    dns_send(0x4843, "henge-fixture-nonexistent.invalid", 1); wait_flag(&dns_negative, 6000); check("DNS NXDOMAIN retained", dns_negative);
    int listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t address_len = sizeof address;
    int server_ok = listener >= 0 && !bind(listener, (struct sockaddr *)&address, sizeof address) &&
        !getsockname(listener, (struct sockaddr *)&address, &address_len) && !listen(listener, 1);
    pthread_t server;
    int server_started = server_ok && !pthread_create(&server, NULL, http_server, &listener);
    if (server_started) {
        server_port = ntohs(address.sin_port);
        tcp_send(2, 1000, 0, NULL); wait_flag(&syn_ack, 2000);
        if (syn_ack) {
            tcp_send(0x10, 1001, peer_seq + 1, NULL);
            const char request[] = "GET /fixture HTTP/1.0\r\nHost: fixture\r\n\r\n";
            tcp_send(0x18, 1001, peer_seq + 1, request); wait_flag(&http_ok, 3000);
            if (next_ack) tcp_send(0x10, 1001 + strlen(request), next_ack, NULL);
        }
        pthread_join(server, NULL);
    }
    if (listener >= 0) close(listener);
    check("outbound TCP handshake through NAT", syn_ack);
    check("HTTP payload through NAT", http_ok);
    check("received IPv4 framing/checksums", !bad_packet);
    hv_net_detach();
    check("teardown clears transmit callback", !net.transmit);
    int reattached = hv_net_attach(&machine, &net) == 0;
    check("fresh network session after detach", reattached);
    if (reattached) { dns_send(0x4841, "gs.apple.com", 1); hv_net_detach(); }
    check("stop joins in-flight DNS callbacks", !net.transmit);
    printf("GUEST_NETWORK_FIXTURE_RESULT checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
