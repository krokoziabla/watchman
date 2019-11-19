#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>

void socket_closer(FILE * f)
{
    if ( f != nullptr )
    {
        fclose(f);
    }
}

int main(int, char * [])
{
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

    msghdr msg;
    memset(&msg, 0x00, sizeof msg);
    std::array<char, 1u << 12u> buffer;
    iovec iov{buffer.data(), buffer.size() * sizeof(char)};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    auto ret = recvmsg(fileno(s.get()), &msg, 0);
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
