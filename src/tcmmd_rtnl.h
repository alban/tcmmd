/*
 * tcmmd - traffic control multimedia daemon
 * Copyright (C) 2014 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __TCMMD_RTNL_H
#define __TCMMD_RTNL_H

void tcmmdrtnl_init (const char *link_name);
void tcmmdrtnl_init_ifb (void);
void tcmmdrtnl_uninit (void);

void tcmmdrtnl_del_rules (void);

void tcmmdrtnl_add_rules (in_addr_t *ip_src,
                          in_addr_t *ip_dst,
                          uint16_t tcp_sport,
                          uint16_t tcp_dport,
                          guint64 stream_rate,
                          guint64 background_rate);

void tcmmdrtnl_print_stats (guint64 *qdisc_root_bytes,
                            guint64 *qdisc_stream_bytes,
                            guint64 *qdisc_background_bytes);

#endif
