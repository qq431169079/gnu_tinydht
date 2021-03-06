/***************************************************************************
 *  Copyright (C) 2007 by Saritha Kalyanam                                 *
 *  kalyanamsaritha@gmail.com                                              *
 *                                                                         *
 *  This program is free software: you can redistribute it and/or modify   *
 *  it under the terms of the GNU Affero General Public License as         *
 *  published by the Free Software Foundation, either version 3 of the     *
 *  License, or (at your option) any later version.                        *
 *                                                                         *
 *  This program is distributed in the hope that it will be useful,        *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *  GNU Affero General Public License for more details.                    *
 *                                                                         *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

#ifndef __DHT_H__
#define __DHT_H__

#include <net/if.h>

#include "types.h"
#include "pkt.h"
#include "kbucket.h"
#include "task.h"
#include "tinydht.h"

struct pkt;
struct task;
struct tinydht_msg;

struct dht_net_if {
    char                        ifname[IFNAMSIZ];
    struct sockaddr_storage     int_addr;
    struct sockaddr_storage     ext_addr;
    int                         sock;
};

struct dht {
    /* type */
    int                 type;
    /* network intf */
    struct dht_net_if   net_if;
    unsigned short      port;
    /* DHT parameters */
    int                 k;
    int                 b;
    struct kbucket      kbucket[160];
    /* DHT api */
    int (*get)(struct dht *dht, struct tinydht_msg *msg);
    int (*put)(struct dht *dht, struct tinydht_msg *msg);
    int (*rpc_rx)(struct dht *dht, struct sockaddr_storage *from, 
                        size_t fromlen, u8 *data, int len, u64 timestamp);
    int (*task_schedule)(struct dht *dht);
    void (*exit)(struct dht *dht);
};

int dht_net_if_new(struct dht_net_if *net_if, const char *ifname, 
                        struct sockaddr *addr, size_t addrlen);
int dht_new(struct dht *dht, unsigned int type, 
                        struct dht_net_if *net_if, short port);

u64 dht_get_current_time(void);
int dht_get_rnd_port(u16 *port);

#endif /* __DHT_H__ */
