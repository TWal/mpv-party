#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <strings.h>
#include <string.h>
#include <assert.h>

#include <map>

const int MAX_EVENTS = 5;
const int BUF_SIZE = 256;

#include "config.h"

void error(const char* msg) {
    perror(msg);
    exit(1);
}


struct Client {
    int fd;
    size_t validated_passphrase_bytes;
    Client(int fd) : fd(fd), validated_passphrase_bytes(0) {
    }
    bool is_trusted() const {
        return validated_passphrase_bytes == PASSPHRASE_LEN;
    }
};

struct Server {
    int sock_fd;
    int epoll_fd;
    std::map<int, Client*> clients;
    Server() {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(sock_fd < 0) error("socket");

        sockaddr_in serv_addr;
        bzero(&serv_addr, sizeof(serv_addr));

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT);
        serv_addr.sin_addr.s_addr = INADDR_ANY;

        if(bind(sock_fd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            error("bind");
        }

        listen(sock_fd, 5);

        epoll_fd = epoll_create1(0);
        if(epoll_fd < 0) {
            error("epoll_create1");
        }


        epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = sock_fd;
        if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event)) {
            error("epoll_ctl");
        }
    }

    void broadcast(const void* buf, size_t nbytes) {
        for(std::pair<int, Client*> c : clients) {
            write(c.second->fd, buf, nbytes);
        }
    }

    void close_client(Client* client) {
        int fd = client->fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        delete client;
        clients.erase(fd);
        close(fd);
    }

    void wait_and_process_events() {
        epoll_event events[MAX_EVENTS];

        printf("Polling...\n");
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        printf("%d events\n", event_count);
        for(int i = 0; i < event_count; ++i) {
            int fd = events[i].data.fd;
            if(fd == sock_fd) {
                printf("Got new client!\n");
                sockaddr_in cli_addr;
                bzero(&cli_addr, sizeof(cli_addr));
                socklen_t clilen = sizeof(cli_addr);
                int newsockfd = accept(sock_fd, (sockaddr*)&cli_addr, &clilen);
                if(newsockfd < 0) {
                    error("accept");
                }
                clients[newsockfd] = new Client(newsockfd);;

                epoll_event event;
                event.events = EPOLLIN;
                event.data.fd = newsockfd;
                if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, newsockfd, &event)) {
                    error("epoll_ctl");
                }
            } else if(events[i].events & EPOLLIN) {
                printf("Getting data from client\n");
                Client* client = clients[fd];
                char buf[BUF_SIZE];
                if(!client->is_trusted()) {
                    int sz = read(fd, buf, std::min(size_t(BUF_SIZE), PASSPHRASE_LEN - client->validated_passphrase_bytes));
                    printf("%d\n", sz);
                    printf("%lu\n", std::min(size_t(BUF_SIZE), PASSPHRASE_LEN - client->validated_passphrase_bytes));
                    printf("%lu\n", PASSPHRASE_LEN - client->validated_passphrase_bytes);
                    assert(client->validated_passphrase_bytes + sz <= PASSPHRASE_LEN);
                    if(sz == 0) {
                        printf("Client closed\n");
                        close_client(client);
                    } else {
                        for(int i = 0; i < sz; ++i) {
                            if(PASSPHRASE[client->validated_passphrase_bytes + i] != buf[i]) {
                                printf("Client has wrong passphrase %lu %d '%c' '%c'\n", client->validated_passphrase_bytes, i, PASSPHRASE[client->validated_passphrase_bytes+i], buf[i]);
                                close_client(client);
                                break;
                            }
                        }
                        client->validated_passphrase_bytes += sz;
                    }
                } else {
                    int avail_sz;
                    ioctl(fd, FIONREAD, &avail_sz);
                    if(avail_sz == 0) {
                        printf("Client closed\n");
                        close_client(client);
                    }
                    if(avail_sz >= 12) {
                        assert(BUF_SIZE >= 12);
                        int sz = read(fd, buf, 12);
                        assert(sz == 12);
                        broadcast(buf, 12);
                    }
                }
            }
        }
    }
};

int main() {
    Server server;
    while(true) {
        server.wait_and_process_events();
    }
    return 0;
}

