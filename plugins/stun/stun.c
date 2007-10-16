/***************************************************************************
 *   Copyright (C) 2007 by Saritha Kalyanam                                *
 *   kalyanamsaritha@gmail.com                                             * 
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA            *
 ***************************************************************************/

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
extern int h_errno;
#include <time.h>

#include "stun.h"
#include "crypto.h"
#include "debug.h"
#include "pkt.h"

#define MAX_STUN_TIMEOUT        7900 /* milliseconds */

static const char *stun_server_list[] = {
    "stun.sipnet.ru",
    "stun.vtnoc.net",
    "stunserver.org",
    "stun.fwdnet.net",
    "stun01.sipphone.com",
    "stun.voipbuster.com",
    "stun.voxgratia.org",
    "stun.xten.com",
    "stun.ekiga.net",
    "stun.fwd.org",
    "stun.voxalot.com.au",
    "stun1.noc.ams-ix.net",
    "stun.sipgate.net",
    "stun.voip.eutelia.it"
};

static const size_t n_stun_servers = sizeof(stun_server_list)/sizeof(char *);

const char *
stun_pick_rnd_server(void)
{
    u16 index = 0;
    int ret;

    ret = crypto_get_rnd_short(&index);
    if (ret != SUCCESS) {
        return NULL;
    }

    return stun_server_list[index % n_stun_servers];
}

int
stun_send_and_receive(int sock, struct sockaddr_in *dst, 
                        u8 *in, size_t inlen, struct sockaddr_in *src, 
                        u8 *out, size_t *outlen)
{
    int ret;
    struct sockaddr_in from;
    socklen_t fromlen;
    u8 buf[512];
    struct timespec ts;
    u64 t = 0;

    ASSERT(sock && dst && in && inlen && src && out && outlen);

    ret = sendto(sock, in, inlen, 0, (struct sockaddr *)dst, 
                    sizeof(struct sockaddr_in));
    if (ret < 0) {
        ERROR("sendto() - %s\n", strerror(errno));
        return FAILURE;
    }

    bzero(&ts, sizeof(ts));
    t = 0;

    while (t < MAX_STUN_TIMEOUT) {
        errno = 0;

        ret = recvfrom(sock, buf, sizeof(buf), MSG_DONTWAIT, 
                (struct sockaddr *)&from, &fromlen);
        if (ret < 0) {
            if (errno != EAGAIN) {
                break;
            }
            t = t * 2 + 100; /* millisecs */
            if (t > MAX_STUN_TIMEOUT) {
                break;
            }
            ts.tv_sec = t / 1000;
            ts.tv_nsec = (t % 1000) * 1000000;
            nanosleep(&ts, NULL);
            continue;
        } else if (ret == 0) {
            break;
        } else {
            if (memcmp(&dst->sin_addr, &from.sin_addr, 
                        sizeof(struct in_addr)) != 0) {
                ERROR("received STUN reply from invalid source\n");
                return FAILURE;
            }
            *outlen = ret;
            memcpy(out, buf, *outlen);
            memcpy(src, &from, sizeof(struct sockaddr_in));

            return SUCCESS;
        }
    } 

    return FAILURE;
}

int
stun_get_nat_info(struct stun_nat_info *info)
{
    int sock;
    int ret;
    const char *server = NULL;
    struct hostent *he = NULL;
    struct sockaddr_in dst;

    ASSERT(info);

    /* support only IPv4 */
    if (info->internal.ss_family != AF_INET) {
        return FAILURE;
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ERROR("socket() - %s\n", strerror(errno));
        return FAILURE;
    }

    ret = bind(sock, (struct sockaddr *)&info->internal, 
                sizeof(struct sockaddr_in));
    if (ret < 0) {
        ERROR("bind() - %s\n", strerror(errno));
        return FAILURE;
    }

    while (TRUE) {
        server = stun_pick_rnd_server();
        if (!server) {
            continue;
        }
        DEBUG("picked stun %s\n", server);

        he = gethostbyname(server);
        if (!he) {
            ERROR("gethostbyname() - %s\n", gai_strerror(h_errno));
            continue;
        }

        /* FIXME: do the stun tests here */
        bzero(&dst, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_port = htons(STUN_SERVICE);
        memcpy(&dst.sin_addr.s_addr, he->h_addr, sizeof(struct in_addr));

        ret = stun_test_one(sock, &dst);
        if (ret != SUCCESS) {
            continue;
        } else {
            break;
        }
    }

    return SUCCESS;
}

int
stun_test_one(int sock, struct sockaddr_in *dst)
{
    u8 req[512], rsp[512];
    struct stun_msg_hdr *hdr = NULL;
    size_t req_len, rsp_len;
    int ret;
    struct sockaddr_in from;
    struct stun_msg msg;

    ASSERT(sock && dst);

    bzero(req, sizeof(req));

    hdr = (struct stun_msg_hdr *)req;
    hdr->type = ntohs(BINDING_REQUEST);
    ret = crypto_get_rnd_bytes(&hdr->trans_id, sizeof(hdr->trans_id));
    if (ret != SUCCESS) {
        return FAILURE;
    }
    req_len = 0;
    hdr->len = ntohs(req_len);

    pkt_dump_data(req, sizeof(struct stun_msg_hdr));

    ret = stun_send_and_receive(sock, dst, req, sizeof(struct stun_msg_hdr), &from, rsp, &rsp_len);
    if (ret != SUCCESS) {
        return ret;
    }

    pkt_dump_data(rsp, rsp_len);

    ret = stun_read_msg(rsp, rsp_len, &msg);
    if (ret != SUCCESS) {
        return ret;
    }

    if (msg.hdr.type != BINDING_RESPONSE) {
        return FAILURE;
    }

    return SUCCESS;
}

int
stun_test_two(int sock, struct sockaddr_in *dst)
{
    ASSERT(sock && dst);

    return SUCCESS;
}

int
stun_test_three(int sock, struct sockaddr_in *dst)
{
    ASSERT(sock && dst);

    return SUCCESS;
}

int
stun_read_msg(u8 *data, size_t len, struct stun_msg *msg)
{
    struct stun_msg_hdr *hdr = NULL;
    int ret;

    ASSERT(data && len && msg);

    bzero(msg, sizeof(struct stun_msg));

    hdr = (struct stun_msg_hdr *)data;
    msg->hdr.type = ntohs(hdr->type);
    msg->hdr.len = ntohs(hdr->len);
    memcpy(msg->hdr.trans_id, hdr->trans_id, 16);

    ret = stun_read_attrs(hdr + 1, msg->hdr.len, msg);
    if (ret != SUCCESS) {
        return ret;
    }

    return SUCCESS;
}

int
stun_read_attrs(u8 *data, size_t len, struct stun_msg *msg)
{
    struct stun_tlv *tlv = NULL;
    u16 tlen = 0;
    int ret;

    ASSERT(data && len && msg);

    tlv = (struct stun_tlv *)data;

    while (len > 0) {
        switch (ntohs(tlv->type)) {
            case MAPPED_ADDRESS:
                /* parse the inet address */
                if (ntohs(tlv->len) != 8) {
                    ASSERT(0);
                }
                ret = stun_read_inetaddr((struct stun_inetaddr_attr *)tlv->val,
                                            &msg->map_addr);
                if (ret != SUCCESS) {
                    break;
                }
                break;
            case RESPONSE_ADDRESS:
            case SOURCE_ADDRESS:
            case CHANGED_ADDRESS:
            case REFLECTED_FROM:
            case XOR_MAPPED_ADDRESS:
            case SERVER:
            case ALTERNATE_SERVER:
                /* parse the inet address */
                if (ntohs(tlv->len) != 8) {
                    break;
                }
                ret = stun_read_inetaddr((struct stun_inetaddr_attr *)tlv->val,
                                            &msg->alt_server);
                if (ret != SUCCESS) {
                    break;
                }
                break;
            default:
                /* we don't handle this, just skip over */
                break;
        }

        tlen = ntohs(tlv->len) + sizeof(struct stun_tlv);
        len -= tlen; 

        tlv = (void *)tlv + tlen;
    }

    return SUCCESS;
}

int
stun_read_inetaddr(struct stun_inetaddr_attr *attr, struct sockaddr_in *sin)
{
    ASSERT(attr && sin);

    bzero(sin, sizeof(struct sockaddr_in));

    if (ntohs(attr->family) != STUN_INETADDR4_TYPE) {
        return FAILURE;
    }

    sin->sin_family = AF_INET;
    sin->sin_port = ntohs(attr->port);
    memcpy(&sin->sin_addr.s_addr, attr->addr, sizeof(u32));

    return SUCCESS;
}

/*

The flow makes use of three tests.  In test I, the client sends a
   STUN Binding Request to a server, without any flags set in the
   CHANGE-REQUEST attribute, and without the RESPONSE-ADDRESS attribute.
   This causes the server to send the response back to the address and
   port that the request came from.  In test II, the client sends a
   Binding Request with both the "change IP" and "change port" flags
   from the CHANGE-REQUEST attribute set.  In test III, the client sends
   a Binding Request with only the "change port" flag set.

   The client begins by initiating test I.  If this test yields no
   response, the client knows right away that it is not capable of UDP
   connectivity.  If the test produces a response, the client examines
   the MAPPED-ADDRESS attribute.  If this address and port are the same
   as the local IP address and port of the socket used to send the
   request, the client knows that it is not natted.  It executes test
   II.

   If a response is received, the client knows that it has open access
   to the Internet (or, at least, its behind a firewall that behaves
   like a full-cone NAT, but without the translation).  If no response
   is received, the client knows its behind a symmetric UDP firewall.

   In the event that the IP address and port of the socket did not match
   the MAPPED-ADDRESS attribute in the response to test I, the client
   knows that it is behind a NAT.  It performs test II.  If a response
   is received, the client knows that it is behind a full-cone NAT.  If
   no response is received, it performs test I again, but this time,
   does so to the address and port from the CHANGED-ADDRESS attribute
   from the response to test I.  If the IP address and port returned in
   the MAPPED-ADDRESS attribute are not the same as the ones from the
   first test I, the client knows its behind a symmetric NAT.  If the
   address and port are the same, the client is either behind a
   restricted or port restricted NAT.  To make a determination about
   which one it is behind, the client initiates test III.  If a response
   is received, its behind a restricted NAT, and if no response is
   received, its behind a port restricted NAT.

   This procedure yields substantial information about the operating
   condition of the client application.  In the event of multiple NATs
   between the client and the Internet, the type that is discovered will
   be the type of the most restrictive NAT between the client and the
   Internet.  The types of NAT, in order of restrictiveness, from most
   to least, are symmetric, port restricted cone, restricted cone, and
   full cone.

   */
