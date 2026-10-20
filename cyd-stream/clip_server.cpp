// Serves a clip.bin (made by the ESP32-035 CYD repo's examples/video-clip/tools/make_clip.py)
// to the CYD video-clip player over Wi-Fi, so the whole video lives on the NanoKVM and the CYD
// only fetches what it is about to show or play.
//
//   clip_server FILE [PORT]      default port 8765
//
// Protocol (TCP, little endian): the client sends u32 offset + u32 length and gets u32 n followed
// by n bytes of FILE (n < length only at the end of the file). Length 0 asks for the file size,
// returned in n with no data. One process per connection, so a stalled client never blocks the
// other; a connection idle for 60 s is closed (the player reconnects by itself).
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_REQUEST (256 * 1024)   // biggest single read: a frame, the index, or an audio chunk

static bool sendAll(int s, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    while (n > 0) {
        ssize_t w = send(s, b, n, MSG_NOSIGNAL);
        if (w <= 0) return false;
        b += w;
        n -= w;
    }
    return true;
}

static bool recvAll(int s, void* p, size_t n) {
    uint8_t* b = (uint8_t*)p;
    while (n > 0) {
        ssize_t r = recv(s, b, n, 0);
        if (r <= 0) return false;
        b += r;
        n -= r;
    }
    return true;
}

static void serve(int s, int fd, uint32_t size) {
    static uint8_t buf[MAX_REQUEST];
    uint32_t req[2];
    while (recvAll(s, req, sizeof(req))) {
        uint32_t off = req[0], len = req[1];
        if (len == 0) {
            if (!sendAll(s, &size, 4)) return;
            continue;
        }
        if (len > MAX_REQUEST) return;
        uint32_t n = off >= size ? 0 : (len < size - off ? len : size - off);
        if (n && pread(fd, buf, n, off) != (ssize_t)n) return;
        if (!sendAll(s, &n, 4) || !sendAll(s, buf, n)) return;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: clip_server FILE [PORT]\n"); return 1; }
    int port = argc > 2 ? atoi(argv[2]) : 8765;
    int fd = open(argv[1], O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) < 0) { perror(argv[1]); return 1; }
    uint32_t size = (uint32_t)st.st_size;

    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, (sockaddr*)&a, sizeof(a)) < 0 || listen(ls, 4) < 0) { perror("bind"); return 1; }
    signal(SIGCHLD, SIG_IGN);   // children reap themselves
    printf("serving %s (%u bytes) on port %d\n", argv[1], size, port);
    fflush(stdout);

    for (;;) {
        sockaddr_in peer;
        socklen_t pl = sizeof(peer);
        int c = accept(ls, (sockaddr*)&peer, &pl);
        if (c < 0) continue;
        if (fork() == 0) {
            close(ls);
            setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            timeval idle = {60, 0};
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &idle, sizeof(idle));
            printf("client %s connected\n", inet_ntoa(peer.sin_addr));
            fflush(stdout);
            serve(c, fd, size);
            close(c);
            _exit(0);
        }
        close(c);
    }
}
