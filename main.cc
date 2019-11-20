#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>

#include <curl/curl.h>

void socket_closer(FILE * f)
{
    if ( f != nullptr )
    {
        fclose(f);
    }
}

void curl_closer(CURL * c)
{
    if ( c != nullptr )
    {
        curl_easy_cleanup(c);
    }
}

volatile sig_atomic_t abort_flag = 0;

void abort(int signal)
{
    abort_flag = 1;
}

int get_message(int s)
{
    std::array<char, 1u << 12u> buffer;
    iovec iov{buffer.data(), buffer.size() * sizeof(char)};

    msghdr msg;
    memset(&msg, 0x00, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    auto ret = recvmsg(s, &msg, 0);
    if ( ret == -1 )
    {
        perror("Failed to receive message from socket");
        return EXIT_FAILURE;
    }

    for ( auto nh = reinterpret_cast<nlmsghdr *>(buffer.data()); NLMSG_OK(nh, ret); nh = NLMSG_NEXT(nh, ret) )
    {
        switch (nh->nlmsg_type)
        {
        case NLMSG_ERROR:
            puts("error message");
            return EXIT_SUCCESS;
        case RTM_NEWADDR:
        case RTM_DELADDR:
        {
            auto len = NLMSG_PAYLOAD(nh, sizeof(ifaddrmsg));
            auto msg = reinterpret_cast<ifaddrmsg *>(NLMSG_DATA(nh));

            for ( auto rth = IFA_RTA(msg); RTA_OK(rth, len); rth = RTA_NEXT(rth, len) )
            {
                switch (rth->rta_type)
                {
                case IFA_ADDRESS:
                {
                    std::array<char, 16> buffer;
                    puts(inet_ntop(msg->ifa_family, RTA_DATA(rth), buffer.data(), buffer.size()));
                    break;
                }
                default:
                    puts("other");
                }
            }
            continue;
        }
        default:
            break;
        }
    }
    return EXIT_SUCCESS;
}

int main(int, char * [])
{
    // signals
    struct sigaction sact;
    memset(&sact, 0x00, sizeof sact);
    sact.sa_handler = abort;

    int ret = 0;
    ret += sigaction(SIGINT, &sact, nullptr);
    ret += sigaction(SIGTERM, &sact, nullptr);

    if ( ret != 0 )
    {
        perror("Failed to set signal handler");
    }

    // curl
    std::shared_ptr<CURL> curl(curl_easy_init(), curl_closer);
    curl_easy_setopt(curl.get(), CURLOPT_URL, "http://hldns.ru/update/HXQ2XC6APHW9I8RJMRH9S9A06KB4WE");
    curl_easy_setopt(curl.get(), CURLOPT_VERBOSE, 1l);

    // netlink socket
    std::shared_ptr<FILE> s(fdopen(socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE), "r"), socket_closer);
    if ( s == nullptr )
    {
        perror("Failed to open NETLINK_ROUTE socket");
        return EXIT_FAILURE;
    }

    sockaddr_nl sa;
    memset(&sa, 0x00, sizeof sa);
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = RTMGRP_IPV4_IFADDR;

    if ( bind(fileno(s.get()), reinterpret_cast<sockaddr *>(&sa), sizeof sa) == -1 )
    {
        perror("Failed to bind to RTMGRP_IPV4_IFADDR group");
        return EXIT_FAILURE;
    }

    while ( abort_flag == 0 )
    {
        if ( get_message(fileno(s.get())) == EXIT_SUCCESS )
        {
            curl_easy_perform(curl.get());
        }
    }

    puts("Exiting");

    return EXIT_SUCCESS;
}
