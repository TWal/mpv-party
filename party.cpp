#include <mpv/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <strings.h>
#include <string.h>
#include <netdb.h>
#include <assert.h>
#include <math.h>

#include "config.h"

void error(const char* msg) {
    perror(msg);
    exit(1);
}

double get_double(mpv_handle* mpv, const char* s) {
    double res;
    mpv_get_property(mpv, s, MPV_FORMAT_DOUBLE, &res);
    return res;
}

void set_double(mpv_handle* mpv, const char* s, double d) {
    mpv_set_property(mpv, s, MPV_FORMAT_DOUBLE, &d);
}

int get_flag(mpv_handle* mpv, const char* s) {
    int res;
    mpv_get_property(mpv, s, MPV_FORMAT_FLAG, &res);
    return res;
}

void set_flag(mpv_handle* mpv, const char* s, int f) {
    mpv_set_property(mpv, s, MPV_FORMAT_FLAG, &f);
}

double get_duration(mpv_handle* mpv) {
    return get_double(mpv, "duration");
}

double get_time(mpv_handle* mpv) {
    return get_double(mpv, "time-pos");
}

void set_time(mpv_handle* mpv, double t) {
    set_double(mpv, "time-pos", t);
}

int get_pause(mpv_handle* mpv) {
    return get_flag(mpv, "pause");
}

void set_pause(mpv_handle* mpv, int pause) {
    return set_flag(mpv, "pause", pause);
}

const char *addrtype(int addrtype) {
        switch(addrtype) {
                case AF_INET:
                        return "AF_INET";
                case AF_INET6:
                        return "AF_INET6";
        }
        return "Unknown";
}

#include <arpa/inet.h>

int open_connexion() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        perror("socket");
        return -1;
    }
    struct hostent* server = gethostbyname(SERVER);
    if(server == NULL) {
        perror("gethostbyname");
        return -1;
    }

    sockaddr_in serv_addr;

    bzero(&serv_addr, sizeof(serv_addr));
    bcopy(server->h_addr_list[0], &serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_family = AF_INET;

    if(connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        return -1;
    }
    if(write(sockfd, PASSPHRASE, PASSPHRASE_LEN) != (int)PASSPHRASE_LEN) {
        fprintf(stderr, "Wrong passphrase\n");
        return -1;
    }
    return sockfd;
}

struct playing_state {
    double t;
    int pause;
} __attribute__((packed));
static_assert(sizeof(playing_state) == 12);

playing_state get_playing_state(mpv_handle* mpv) {
    playing_state res;
    res.t = get_time(mpv);
    res.pause = get_pause(mpv);
    return res;
}

void set_playing_state(mpv_handle* mpv, playing_state st) {
    set_time(mpv, st.t);
    set_pause(mpv, st.pause);
}

bool playing_state_eq(playing_state s1, playing_state s2) {
    return fabs(s1.t - s2.t) < 0.01 && s1.pause == s2.pause;
}

void broadcast_state(mpv_handle* mpv, int sockfd) {
    playing_state st = get_playing_state(mpv);
    assert(write(sockfd, &st, 12) == 12);
}

playing_state read_playing_state(int sockfd) {
    playing_state st;
    assert(read(sockfd, &st, 12) == 12);
    return st;
}

int bytes_available(int sockfd) {
    int avail_sz;
    ioctl(sockfd, FIONREAD, &avail_sz);
    return avail_sz;
}

extern "C" {
int mpv_open_cplugin(mpv_handle* mpv) {
    int sockfd = open_connexion();
    if(sockfd < 0) {
        fprintf(stderr, "Couldn't connect to %s\n", SERVER);
        return 0;
    }
    mpv_observe_property(mpv, 0, "pause", MPV_FORMAT_FLAG);
    playing_state last_sent_state{-1, -1};
    while(1) {
        if(bytes_available(sockfd) >= 12) {
            playing_state newst = read_playing_state(sockfd);
            set_playing_state(mpv, newst);
            last_sent_state = newst;
        }
        mpv_event* e = mpv_wait_event(mpv, 0.1);
        if(e->event_id == MPV_EVENT_SEEK) {
            if(playing_state_eq(last_sent_state, get_playing_state(mpv))) {
                continue;
            }
            broadcast_state(mpv, sockfd);
        }
        if(e->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            mpv_event_property* ep = (mpv_event_property*)e->data;
            if(strcmp(ep->name, "pause") == 0) {
                broadcast_state(mpv, sockfd);
            }
        }
        if(e->event_id == MPV_EVENT_SHUTDOWN) {
            exit(0);
        }
    }
    return 0;
}
}
