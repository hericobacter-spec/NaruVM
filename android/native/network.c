#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <curl/curl.h>

int lab_network_probe(void) {
    printf("NETWORK_LIBRARY %s\n", curl_version());
    struct addrinfo *resolved = NULL;
    struct addrinfo hints = {.ai_socktype = SOCK_STREAM};
    errno = 0;
    int gai = getaddrinfo("gs.apple.com", "443", &hints, &resolved);
    printf("NETWORK_DNS gai=%d errno=%d message=%s\n", gai, errno, gai_strerror(gai));
    if (resolved) freeaddrinfo(resolved);
    if (curl_global_init(CURL_GLOBAL_DEFAULT)) return 1;
    const char *urls[] = {"https://gs.apple.com/TSS/controller?action=2", "https://updates.cdn-apple.com/"};
    int failures = 0;
    for (unsigned i = 0; i < sizeof urls / sizeof urls[0]; i++) {
        CURL *c = curl_easy_init();
        if (!c) { failures++; continue; }
        char error[CURL_ERROR_SIZE] = {0};
        curl_easy_setopt(c, CURLOPT_URL, urls[i]);
        curl_easy_setopt(c, CURLOPT_ERRORBUFFER, error);
        curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(c, CURLOPT_VERBOSE, 1L);
        CURLcode result = curl_easy_perform(c);
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        printf("NETWORK_ENDPOINT url=%s curl=%d http=%ld error=%s\n", urls[i], result, status, error);
        failures += result != CURLE_OK || !status;
        curl_easy_cleanup(c);
    }
    curl_global_cleanup();
    printf("NETWORK_RESULT failures=%d (TLS certificate verification enabled)\n", failures);
    return failures ? 1 : 0;
}
