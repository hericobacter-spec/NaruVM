/* Android's public asynchronous resolver supplies wire-format DNS answers.
 * No fixed public resolver, packet-content logging or inbound listener is used. */
#include "dns_proxy.h"
#include <android/multinetwork.h>
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_QUERY 4096
#define MAX_INFLIGHT 32
#define TIMEOUT_MS 5000
typedef struct {
    HvDnsSend send;
    uint8_t frame[14 + 60 + 8 + MAX_QUERY];
    uint32_t ip_off, udp_off, dns_off, dns_len, question_end, udp_max;
} Query;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int stopping = 1, inflight;
static uint64_t answered, handed_back, timed_out;
static int (*res_send)(net_handle_t, const uint8_t *, size_t, uint32_t);
static int (*res_result)(int, int *, uint8_t *, size_t);
static void (*res_cancel)(int);
static void *resolver_library;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static int64_t now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static uint16_t checksum(const uint8_t *p, uint32_t n) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; i += 2) sum += (uint32_t)p[i] << 8 | (i + 1 < n ? p[i + 1] : 0);
    while (sum >> 16) sum = (sum & 65535) + (sum >> 16);
    return (uint16_t)~sum;
}
static int is_stopping(void) {
    pthread_mutex_lock(&mu); int value = stopping; pthread_mutex_unlock(&mu); return value;
}

void hv_dns_proxy_start(void) {
    pthread_mutex_lock(&mu);
    /* dlsym retains the existing API28 loader compatibility; native DNS requires API29+. */
    if (!resolver_library) resolver_library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    res_send = resolver_library ? dlsym(resolver_library, "android_res_nsend") : NULL;
    res_result = resolver_library ? dlsym(resolver_library, "android_res_nresult") : NULL;
    res_cancel = resolver_library ? dlsym(resolver_library, "android_res_cancel") : NULL;
    answered = handed_back = timed_out = 0;
    stopping = 0;
    pthread_mutex_unlock(&mu);
    fprintf(stderr, "ANDROID_DNS available=%d resolver=system\n", !!(res_send && res_result && res_cancel));
}
void hv_dns_proxy_stop(void) {
    pthread_mutex_lock(&mu);
    stopping = 1;
    while (inflight) pthread_cond_wait(&cv, &mu);
    pthread_mutex_unlock(&mu);
}
int hv_dns_proxy_on(void) { return 1; }
void hv_dns_proxy_stats(uint64_t *a, uint64_t *h, uint64_t *t) {
    pthread_mutex_lock(&mu); *a = answered; *h = handed_back; *t = timed_out; pthread_mutex_unlock(&mu);
}

static void reply(Query *q, const uint8_t *answer, uint32_t n) {
    uint8_t f[14 + 20 + 8 + 1472] = {0};
    const uint8_t *original = q->frame + q->dns_off;
    uint8_t *ip = f + 14, *udp = ip + 20, *dns = udp + 8;
    if (!answer || n < 12 || !(answer[2] & 0x80) || n > q->udp_max) {
        memcpy(dns, original, q->question_end);
        dns[2] = (original[2] & 0x79) | 0x80 | (answer ? 2 : 0); /* QR, optionally TC */
        dns[3] = 0x80 | (answer ? answer[3] & 15 : 2); /* RA, SERVFAIL on failure */
        memset(dns + 6, 0, 6);
        n = q->question_end;
    } else memcpy(dns, answer, n);
    memcpy(dns, original, 2); /* retain the guest transaction ID */
    memcpy(f, q->frame + 6, 6); memcpy(f + 6, q->frame, 6); wr16(f + 12, 0x0800);
    ip[0] = 0x45; wr16(ip + 2, 20 + 8 + n); ip[8] = 64; ip[9] = 17;
    memcpy(ip + 12, q->frame + q->ip_off + 16, 4);
    memcpy(ip + 16, q->frame + q->ip_off + 12, 4);
    wr16(ip + 10, checksum(ip, 20));
    wr16(udp, 53); memcpy(udp + 2, q->frame + q->udp_off, 2); wr16(udp + 4, 8 + n);
    q->send(f, 14 + 20 + 8 + n, 1); /* IPv4 permits zero UDP checksum */
}
static void *lookup(void *opaque) {
    Query *q = opaque;
    uint8_t answer[65536];
    int n = -1, rcode = 0;
    int fd = !is_stopping() && res_send && res_result && res_cancel ?
        res_send(NETWORK_UNSPECIFIED, q->frame + q->dns_off, q->dns_len, 0) : -1;
    if (fd >= 0) {
        int64_t end = now_ms() + TIMEOUT_MS;
        while (!is_stopping() && now_ms() < end) {
            struct pollfd p = {fd, POLLIN, 0};
            int r = poll(&p, 1, 100);
            if (r > 0) { n = res_result(fd, &rcode, answer, sizeof answer); fd = -1; break; }
            if (r < 0 && errno != EINTR) break;
        }
        if (fd >= 0) res_cancel(fd);
    }
    if (!is_stopping()) {
        reply(q, n >= 12 ? answer : NULL, n >= 12 ? (uint32_t)n : 0);
        pthread_mutex_lock(&mu); answered++; if (n < 12) timed_out++; pthread_mutex_unlock(&mu);
    }
    free(q);
    pthread_mutex_lock(&mu); inflight--; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mu);
    return NULL;
}

int hv_dns_proxy_take(const uint8_t *p, uint32_t len, HvDnsSend send) {
    if (len < 54 || rd16(p + 12) != 0x0800) return 0;
    const uint8_t *ip = p + 14;
    uint32_t ihl = (ip[0] & 15) * 4u, iplen = rd16(ip + 2);
    if (ip[0] >> 4 != 4 || ihl < 20 || iplen > len - 14 || iplen < ihl + 20 ||
        ip[9] != 17 || rd16(ip + 6) & 0x3fff || memcmp(ip + 16, "\12\0\2\3", 4)) return 0;
    const uint8_t *udp = ip + ihl, *dns = udp + 8;
    uint32_t udplen = rd16(udp + 4);
    if (rd16(udp + 2) != 53 || udplen < 20 || udplen > iplen - ihl || udplen - 8 > MAX_QUERY) return 0;
    uint32_t dlen = udplen - 8, off = 12;
    if (dns[2] & 0xf8 || rd16(dns + 4) != 1 || rd16(dns + 6) || rd16(dns + 8) || rd16(dns + 10) > 1) return 0;
    while (off < dlen && dns[off]) {
        uint8_t size = dns[off++];
        if (size > 63 || off + size >= dlen) return 0;
        off += size;
    }
    if (off >= dlen || off + 5 > dlen || off > 267) return 0;
    uint32_t question_end = off + 5, udp_max = 512;
    if (rd16(dns + 10)) {
        if (question_end + 11 > dlen || dns[question_end] || rd16(dns + question_end + 1) != 41 ||
            question_end + 11u + rd16(dns + question_end + 9) != dlen) return 0;
        udp_max = rd16(dns + question_end + 3);
        if (udp_max < 512) udp_max = 512;
        if (udp_max > 1472) udp_max = 1472;
    } else if (question_end != dlen) return 0;
    Query *q = calloc(1, sizeof *q);
    if (!q) return 0;
    q->send = send; q->ip_off = 14; q->udp_off = 14 + ihl; q->dns_off = 14 + ihl + 8;
    q->dns_len = dlen; q->question_end = question_end; q->udp_max = udp_max;
    memcpy(q->frame, p, q->dns_off + dlen);
    pthread_mutex_lock(&mu);
    if (stopping || inflight >= MAX_INFLIGHT) { pthread_mutex_unlock(&mu); free(q); return 0; }
    inflight++;
    pthread_mutex_unlock(&mu);
    pthread_attr_t attr; pthread_t thread;
    pthread_attr_init(&attr); pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    int error = pthread_create(&thread, &attr, lookup, q);
    pthread_attr_destroy(&attr);
    if (error) {
        free(q); pthread_mutex_lock(&mu); inflight--; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mu);
        return 0;
    }
    return 1;
}
