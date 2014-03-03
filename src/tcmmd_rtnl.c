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

#include "tcmmd_rtnl.h"

#include <glib.h>

#include <netlink/version.h>
#include <netlink/cli/utils.h>
#include <netlink/cli/link.h>
#include <netlink/route/link.h>
#include <netlink/route/act/mirred.h>
#include <netlink/route/cls/u32.h>
#include <netlink/route/cls/basic.h>
#include <netlink/route/cls/ematch.h>
#include <netlink/route/qdisc/dsmark.h>
#include <netlink/route/qdisc/htb.h>
#include <netlink/route/qdisc/sfq.h>

#include <linux/if_arp.h>
#include <linux/tc_act/tc_mirred.h>

#define Q_ESTIMATOR "estimator 250ms 500ms"

static struct nl_sock *sock;

static struct nl_cache *link_cache;
static struct nl_cache *qdisc_cache;
static struct nl_cache *class_cache;

/* filter/classifier cache attached to qdisc 1:0 */
static struct nl_cache *cls1_cache = NULL;
/* filter/classifier cache attached to qdisc 2:0 */
static struct nl_cache *cls2_cache = NULL;

static struct rtnl_link *main_link = NULL;
static struct rtnl_link *ifb_link = NULL;

static void
link_cb (struct nl_object *obj, void *data)
{
  struct rtnl_link *link = nl_object_priv (obj);
  const char *link_name = (const char *) data;

  /* Ignore loopback and other non-ethernet interfaces
   * unless explicitely requested.
   * The filter does not check that.
   * Bug in libnl3/lib/route/link.c:link_compare() ?
   */
  if (!link_name && rtnl_link_get_arptype (link) != ARPHRD_ETHER)
    return;

  /* We want to find a real hardware interface, not ifb0 */
  if (g_str_has_prefix (rtnl_link_get_name (link), "ifb"))
    return;

  if (main_link)
    {
      g_printerr ("Error: several network interfaces. "
                  "Hint: use options such as -i %s or -i %s\n",
                  rtnl_link_get_name (main_link),
                  rtnl_link_get_name (link));
      exit (1);
    }
  nl_object_get (OBJ_CAST (link));
  main_link = link;
}

static void
qdisc_delete_cb (struct nl_object *obj, void *arg)
{
  struct rtnl_qdisc *qdisc = nl_object_priv(obj);
  struct rtnl_tc *tc = (struct rtnl_tc *) qdisc;
  struct rtnl_link *link;
  int err;
  char buf[32];

  /* Ignore default qdiscs, unable to delete */
  if (rtnl_tc_get_handle ((struct rtnl_tc *) qdisc) == 0)
    return;

  link = rtnl_link_get (link_cache, rtnl_tc_get_ifindex (tc));
  g_print ("delete qdisc dev %s handle %s %s\n",
           rtnl_link_get_name (link),
           rtnl_tc_handle2str (rtnl_tc_get_handle (tc), buf, sizeof(buf)),
           rtnl_tc_get_kind (tc));
  rtnl_link_put (link);

  if ((err = rtnl_qdisc_delete (sock, qdisc)) < 0)
    {
      g_printerr ("Unable to delete qdisc: %s\n", nl_geterror(err));
      exit (1);
    }
}


void
tcmmdrtnl_init (const char *link_name)
{
  struct rtnl_link *link_filter;
  int err;

  if (!(sock = nl_socket_alloc()))
    exit (1);

  if ((err = nl_connect(sock, NETLINK_ROUTE)) < 0)
    exit (1);

  /* init link cache */

  if ((err = rtnl_link_alloc_cache(sock, AF_UNSPEC, &link_cache)) < 0)
    {
      g_printerr ("Error: unable to allocate link cache: %s\n", nl_geterror(err));
      exit (1);
    }
  nl_cache_mngt_provide (link_cache);

  /* look for the main network interface (e.g. eth0) */

  link_filter = rtnl_link_alloc();
  if (!link_filter)
    exit (1);

  if (link_name)
    rtnl_link_set_name (link_filter, link_name);

  /* we are not interested in loopback or other non-ether interfaces, unless
   * it is explicitely requested by the user
   * FIXME: libnl does not check arptype in nl_cache_foreach_filter so it's a
   *        no-op and we have to double-check in the callback.
   */
  if (!link_name)
    rtnl_link_set_arptype (link_filter, ARPHRD_ETHER);

  nl_cache_foreach_filter (link_cache, OBJ_CAST (link_filter),
                           link_cb, (void *) link_name);

  if (main_link == NULL)
    {
      g_printerr ("Error: network interface not found\n");
      exit (1);
    }

  g_print ("Using iface %s\n", rtnl_link_get_name (main_link));

  /* init qdisc cache */

  if ((err = rtnl_qdisc_alloc_cache(sock, &qdisc_cache)) < 0)
    {
      g_printerr ("Error: unable to allocate qdisc cache: %s\n", nl_geterror(err));
      exit (1);
    }
  nl_cache_mngt_provide (qdisc_cache);
}

static void
tcmmrtnl_setup_ifb_redirection (void)
{
#if 1
  gchar *cmd;
  int err;

  tcmmdrtnl_del_rules ();

  cmd = g_strdup_printf ("tc qdisc del dev %s ingress > /dev/null 2>&1 || true",
                         rtnl_link_get_name (main_link));
  if ((err = system (cmd)))
    {
      g_printerr ("Error: command failed: '%s' (%d)\n", cmd, err);
      exit (1);
    }
  g_free (cmd);

  cmd = g_strdup_printf ("tc qdisc add dev %s "Q_ESTIMATOR" handle ffff: ingress",
                         rtnl_link_get_name (main_link));
  if ((err = system (cmd)))
    {
      g_printerr ("Error: command failed: '%s' (%d)\n", cmd, err);
      exit (1);
    }
  g_free (cmd);

  cmd = g_strdup_printf ("tc filter add dev %s parent ffff: protocol ip u32 match u32 0 0 action mirred egress redirect dev ifb0",
                         rtnl_link_get_name (main_link));
  if ((err = system (cmd)))
    {
      g_printerr ("Error: command failed: '%s' (%d)\n", cmd, err);
      exit (1);
    }
  g_free (cmd);
#else
  struct rtnl_qdisc *qdisc;
  struct rtnl_cls *cls;
  struct rtnl_tc *tc;
  struct rtnl_act *act;
  int err;

  qdisc = rtnl_qdisc_alloc ();
  if (!qdisc)
    {
      g_printerr ("Error: unable to allocate qdisc object\n");
      exit (1);
    }
  tc = (struct rtnl_tc *) qdisc;

  /* delete previous ingress qdisc on eth0, if any */
  rtnl_tc_set_link (tc, main_link);
  rtnl_tc_set_parent (tc, TC_H_INGRESS);
  nl_cache_foreach_filter (qdisc_cache, OBJ_CAST(qdisc), qdisc_delete_cb, NULL);

  /* tc qdisc add dev eth0 handle ffff: ingress */
  rtnl_tc_set_link (tc, main_link);
  rtnl_tc_set_handle (tc, TC_HANDLE (0xffff, 0));
  rtnl_tc_set_parent (tc, TC_H_INGRESS);
  /* "ingress" is both the parent and the name of the qdisq */
  rtnl_tc_set_kind (tc, "ingress");

  if ((err = rtnl_qdisc_add (sock, qdisc, NLM_F_CREATE)))
    {
      g_printerr ("Error: cannot add ingress qdisc: %s\n", nl_geterror(err));
      exit (1);
    }
  rtnl_qdisc_put (qdisc);

  /* tc filter add dev eth0 parent ffff: protocol ip u32 match u32 0 0 action mirred egress redirect dev ifb0 */
  cls = rtnl_cls_alloc ();
  if (!cls)
    {
      g_printerr ("Error: unable to allocate class object\n");
      exit (1);
    }
  tc = (struct rtnl_tc *) cls;

  rtnl_tc_set_link (tc, main_link);
  rtnl_tc_set_parent (tc, TC_HANDLE (0xffff, 0));
  rtnl_cls_set_protocol (cls, ETH_P_IP);
  rtnl_tc_set_kind (tc, "u32");

  rtnl_u32_add_key_uint32 (cls, 0, 0, 0, 0);

  act = rtnl_act_alloc();
  if (!act)
    {
      g_printerr ("Error: unable to allocate act object\n");
      exit (1);
    }
  rtnl_tc_set_kind (TC_CAST(act), "mirred");
  rtnl_mirred_set_action (act, TCA_EGRESS_REDIR);
  err = rtnl_mirred_set_policy (act, TC_ACT_STOLEN);
  if (err)
    {
      g_printerr ("Error: rtnl_mirred_set_policy does not accept TC_ACT_STOLEN. Hint: patch your libnl.\n");
      exit (1);
    }

  rtnl_mirred_set_ifindex (act, rtnl_link_get_ifindex (ifb_link));

  rtnl_u32_add_action (cls, act);

  if ((err = rtnl_cls_add (sock, cls, NLM_F_CREATE)))
    {
      g_printerr ("Error: cannot add ingress redirection: %s\n", nl_geterror(err));
      exit (1);
    }
  rtnl_cls_put (cls);
#endif
}

void
tcmmdrtnl_init_ifb (void)
{
  struct rtnl_link *change;
  int err;

  ifb_link = rtnl_link_get_by_name (link_cache, "ifb0");
  if (ifb_link == NULL)
    {
      g_printerr ("Error: network interface ifb0 unavailable. Hint: sudo modprobe ifb numifbs=1\n");
      exit (1);
    }

  if (!(rtnl_link_get_flags (ifb_link) & IFF_UP))
    {
      change = rtnl_link_alloc ();
      if (change == NULL)
        {
          g_printerr ("Error: unable to allocate link object\n");
          exit (1);
        }

      /* "ip link set dev ifb0 up" */
      rtnl_link_set_flags (change, IFF_UP);
      if ((err = rtnl_link_change (sock, ifb_link, change, 0)))
        {
          g_printerr ("Error: cannot set ifb0 up: %s\n", nl_geterror(err));
          exit (1);
        }
      rtnl_link_put (change);
  }

  tcmmrtnl_setup_ifb_redirection ();

  /* init class cache */

  if ((err = rtnl_class_alloc_cache (sock,
                                     rtnl_link_get_ifindex (ifb_link),
                                     &class_cache)) < 0)
    {
      g_printerr ("Error: unable to allocate class cache: %s\n", nl_geterror(err));
      exit (1);
    }
  nl_cache_mngt_provide (class_cache);
}

/* Last configured values */
static int previous_port = -1;
static guint64 previous_stream_rate = 0;
static guint64 previous_background_rate = 0;

static void
_del_rules (void)
{
  int err;

#if 1
  gchar *cmd;

  cmd = g_strdup_printf ("tc qdisc del dev %s root > /dev/null 2>&1 || true",
                         rtnl_link_get_name (ifb_link));
  if ((err = system (cmd)))
    {
      g_printerr ("Error: command failed: '%s' (%d)\n", cmd, err);
      exit (1);
    }
  g_free (cmd);

  cmd = g_strdup_printf ("tc qdisc del dev %s ingress > /dev/null 2>&1 || true",
                         rtnl_link_get_name (ifb_link));
  if ((err = system (cmd)))
    {
      g_printerr ("Error: command failed: '%s' (%d)\n", cmd, err);
      exit (1);
    }
  g_free (cmd);

#else
  struct rtnl_qdisc *qdisc;
  struct rtnl_tc *tc;

  if (!(qdisc = rtnl_qdisc_alloc ()))
    {
      g_printerr ("Error: unable to allocate qdisc\n");
      exit (1);
    }
  tc = (struct rtnl_tc *) qdisc;

  /* tc qdisc del dev ifb0 root */
  rtnl_tc_set_link (tc, ifb_link);
  rtnl_tc_set_parent (tc, TC_H_ROOT);
  nl_cache_foreach_filter (qdisc_cache, OBJ_CAST(qdisc), qdisc_delete_cb, NULL);

  /* tc qdisc del dev ifb0 ingress */
  rtnl_tc_set_link (tc, ifb_link);
  rtnl_tc_set_parent (tc, TC_H_INGRESS);
  nl_cache_foreach_filter (qdisc_cache, OBJ_CAST(qdisc), qdisc_delete_cb, NULL);

  rtnl_qdisc_put (qdisc);
#endif

}

void
tcmmdrtnl_del_rules (void)
{
  int err;

  _del_rules ();

  if (qdisc_cache)
    {
      if ((err = nl_cache_refill(sock, qdisc_cache)))
        {
          g_printerr ("Error: cannot sync cache: %s\n", nl_geterror(err));
          exit (1);
        }
    }

  if (class_cache)
    {
      if ((err = nl_cache_refill(sock, class_cache)))
        {
          g_printerr ("Error: cannot sync cache: %s\n", nl_geterror(err));
          exit (1);
        }
    }

  nl_cache_free (cls1_cache);
  cls1_cache = NULL;
  nl_cache_free (cls2_cache);
  cls2_cache = NULL;

  previous_port = -1;
  previous_stream_rate = 0;
  previous_background_rate = 0;
}

void
tcmmdrtnl_uninit (void)
{
  int err;
  gchar *cmd;

  if (!ifb_link || !main_link)
    return;

  g_print ("uninit\n");

  _del_rules ();

  cmd = g_strdup_printf ("tc qdisc del dev %s ingress > /dev/null 2>&1 || true",
                         rtnl_link_get_name (main_link));
  if ((err = system (cmd)))
    g_printerr ("Error: command failed: '%s' (%d)\n", cmd, err);
  g_free (cmd);
}

static void
_add_qdisc_dsmark_root (void)
{
  struct rtnl_qdisc *qdisc;
  struct rtnl_tc *tc;
  int err;

  qdisc = rtnl_qdisc_alloc ();
  if (!qdisc)
    {
      g_printerr ("Error: unable to allocate qdisc object\n");
      exit (1);
    }
  tc = (struct rtnl_tc *) qdisc;

  /* tc qdisc add dev ifb0 handle 1:0 root dsmark indices 4 default_index 0 */

  rtnl_tc_set_link (tc, ifb_link);
  rtnl_tc_set_handle (tc, TC_HANDLE (1, 0));
  rtnl_tc_set_parent (tc, TC_H_ROOT);
  rtnl_tc_set_kind (tc, "dsmark");

  rtnl_qdisc_dsmark_set_indices (qdisc, 4);
  rtnl_qdisc_dsmark_set_default_index (qdisc, 0);

  if ((err = rtnl_qdisc_add (sock, qdisc, NLM_F_CREATE)))
    {
      g_printerr ("Error: cannot add dsmark qdisc: %s\n", nl_geterror(err));
      exit (1);
    }

  rtnl_qdisc_put (qdisc);

  if ((err = nl_cache_refill(sock, qdisc_cache)))
    {
      g_printerr ("Error: cannot sync cache: %s\n", nl_geterror(err));
      exit (1);
    }
}

static void
_add_qdisc_htb_root (void)
{
  struct rtnl_qdisc *qdisc;
  struct rtnl_tc *tc;
  int err;

  qdisc = rtnl_qdisc_alloc ();
  if (!qdisc)
    {
      g_printerr ("Error: unable to allocate qdisc object\n");
      exit (1);
    }
  tc = (struct rtnl_tc *) qdisc;

  /* tc qdisc add dev ifb0 handle 2:0 parent 1:0 htb r2q 2 */

  rtnl_tc_set_link (tc, ifb_link);
  rtnl_tc_set_handle (tc, TC_HANDLE (2, 0));
  rtnl_tc_set_parent (tc, TC_HANDLE (1, 0));
  rtnl_tc_set_kind (tc, "htb");

  rtnl_htb_set_rate2quantum (qdisc, 2);

  if ((err = rtnl_qdisc_add (sock, qdisc, NLM_F_CREATE)))
    {
      g_printerr ("Error: cannot add htb qdisc: %s\n", nl_geterror(err));
      exit (1);
    }

  rtnl_qdisc_put (qdisc);

  if ((err = nl_cache_refill(sock, qdisc_cache)))
    {
      g_printerr ("Error: cannot sync cache: %s\n", nl_geterror(err));
      exit (1);
    }
}

static void
_add_class_htb (uint32_t parent, uint32_t classid, uint64_t rate, uint64_t ceil)
{
  struct rtnl_class *class;
  struct rtnl_tc *tc;
  int err;

  class = rtnl_class_alloc ();
  if (!class)
    {
      g_printerr ("Error: unable to allocate class object\n");
      exit (1);
    }
  tc = (struct rtnl_tc *) class;

  /* tc class add dev ifb0 parent 2:0 classid 2:1 htb rate 50000bps ceil 50000bps */

  rtnl_tc_set_link (tc, ifb_link);
  rtnl_tc_set_handle (tc, classid);
  rtnl_tc_set_parent (tc, parent);
  rtnl_tc_set_kind (tc, "htb");

  rtnl_htb_set_rate (class, rate);
  if (ceil > 0)
    rtnl_htb_set_ceil (class, ceil);

  if ((err = rtnl_class_add (sock, class, NLM_F_CREATE)))
    {
      g_printerr ("Error: cannot add htb class: %s\n", nl_geterror(err));
      exit (1);
    }

  rtnl_class_put (class);

  if ((err = nl_cache_refill(sock, class_cache)))
    {
      g_printerr ("Error: cannot sync cache: %s\n", nl_geterror(err));
      exit (1);
    }
}

static void
_add_qdisc_sfq (uint32_t handle, uint32_t parent)
{
  struct rtnl_qdisc *qdisc;
  struct rtnl_tc *tc;
  int err;

  qdisc = rtnl_qdisc_alloc ();
  if (!qdisc)
    {
      g_printerr ("Error: unable to allocate qdisc object\n");
      exit (1);
    }
  tc = (struct rtnl_tc *) qdisc;

  /* tc qdisc add dev ifb0 handle 3:0 parent 2:1 sfq */

  rtnl_tc_set_link (tc, ifb_link);
  rtnl_tc_set_handle (tc, handle);
  rtnl_tc_set_parent (tc, parent);
  rtnl_tc_set_kind (tc, "sfq");

  if ((err = rtnl_qdisc_add (sock, qdisc, NLM_F_CREATE)))
    {
      g_printerr ("Error: cannot add sfq qdisc: %s\n", nl_geterror(err));
      exit (1);
    }

  rtnl_qdisc_put (qdisc);

  if ((err = nl_cache_refill(sock, qdisc_cache)))
    {
      g_printerr ("Error: cannot sync cache: %s\n", nl_geterror(err));
      exit (1);
    }
}

static void
_add_filter_tcindex (uint32_t parent)
{
  struct rtnl_cls *filter;
  struct rtnl_tc *tc;
  int err;

  filter = rtnl_cls_alloc ();
  if (!filter)
    {
      g_printerr ("Error: unable to allocate filter object\n");
      exit (1);
    }
  tc = (struct rtnl_tc *) filter;

  /* tc filter add dev ifb0 parent 2:0 protocol all prio 1 tcindex mask 0x3 shift 0 */

  rtnl_tc_set_link (tc, ifb_link);
  rtnl_tc_set_parent (tc, parent);
  rtnl_tc_set_kind (tc, "tcindex");

  rtnl_cls_set_prio (filter, 1);

  if ((err = rtnl_cls_add (sock, filter, NLM_F_CREATE | NLM_F_EXCL)))
    {
      g_printerr ("Error: cannot add filter: %s\n", nl_geterror(err));
      exit (1);
    }

  rtnl_cls_put (filter);

  if ((err = nl_cache_refill(sock, cls2_cache)))
    {
      g_printerr ("Error: cannot sync cache: %s\n", nl_geterror(err));
      exit (1);
    }
}

void
tcmmdrtnl_add_rules (in_addr_t *ip_src,
                     in_addr_t *ip_dst,
                     uint16_t tcp_sport,
                     uint16_t tcp_dport,
                     guint64 stream_rate,
                     guint64 background_rate)
{
  char *cmd;
  int err;
  uint32_t ip_src_mask = 0xffffffff;
  uint32_t ip_dst_mask = 0xffffffff;
  uint16_t tcp_sport_mask = 0xffff;
  uint16_t tcp_dport_mask = 0xffff;

  /* zero means we don't filter on that */
  if (*ip_src == 0)
    ip_src_mask = 0;
  if (*ip_dst == 0)
    ip_dst_mask = 0;
  if (tcp_sport == 0)
    tcp_sport_mask = 0;
  if (tcp_dport == 0)
    tcp_dport_mask = 0;

  /* if the tuple didn't change, don't reinstall the rules, just modify them */
  if (previous_port == tcp_dport)
    {
      g_print ("Updating traffic control: tcp_dport=%d stream_rate=%"G_GUINT64_FORMAT" background_rate=%"G_GUINT64_FORMAT" ...\n", tcp_dport, stream_rate, background_rate);

      if (previous_stream_rate != stream_rate)
        {
          cmd = g_strdup_printf (
            "tc class change dev ifb0 parent 2:0 classid 2:2 htb rate %"G_GUINT64_FORMAT"bps",
            stream_rate);
          g_print ("%s\n", cmd);
          err = system (cmd);
          g_print ("cmd returned %d\n", err);
          g_free (cmd);
        }

      if (previous_background_rate != background_rate)
        {
          cmd = g_strdup_printf (
            "tc class change dev ifb0 parent 2:0 classid 2:3 htb rate %"G_GUINT64_FORMAT"bps ceil %"G_GUINT64_FORMAT"bps",
            background_rate, background_rate);
          g_print ("%s\n", cmd);
          err = system (cmd);
          g_print ("cmd returned %d\n", err);
          g_free (cmd);
        }

      previous_stream_rate = stream_rate;
      previous_background_rate = background_rate;

      g_print ("Updating traffic control: tcp_dport=%d stream_rate=%"G_GUINT64_FORMAT" background_rate=%"G_GUINT64_FORMAT" : done.\n", tcp_dport, stream_rate, background_rate);
      return;
    }

  tcmmdrtnl_del_rules ();

  g_print ("Adding traffic control: tcp_dport=%d stream_rate=%"G_GUINT64_FORMAT" background_rate=%"G_GUINT64_FORMAT" ...\n", tcp_dport, stream_rate, background_rate);

#if 0
  /* add qdisc and classes */
  _add_qdisc_dsmark_root ();
  _add_qdisc_htb_root ();
  _add_class_htb (TC_HANDLE (2, 0), TC_HANDLE (2, 1), 50000, 50000);
  _add_qdisc_sfq (TC_HANDLE (3, 0), TC_HANDLE (2, 1));
  _add_class_htb (TC_HANDLE (2, 0), TC_HANDLE (2, 2), stream_rate, 0);
  _add_qdisc_sfq (TC_HANDLE (4, 0), TC_HANDLE (2, 2));
  _add_class_htb (TC_HANDLE (2, 0), TC_HANDLE (2, 3), background_rate, background_rate);
  _add_qdisc_sfq (TC_HANDLE (5, 0), TC_HANDLE (2, 3));
#endif

  /* classifier caches are specific to the qdisc they are attached to */
  if ((err = rtnl_cls_alloc_cache (sock,
                                   rtnl_link_get_ifindex (ifb_link),
                                   TC_HANDLE (1,0),
                                   &cls1_cache)) < 0)
    {
      g_printerr ("Error: unable to allocate filter cache: %s\n", nl_geterror(err));
      exit (1);
    }
  nl_cache_mngt_provide (cls1_cache);

  if ((err = rtnl_cls_alloc_cache (sock,
                                   rtnl_link_get_ifindex (ifb_link),
                                   TC_HANDLE (1,0),
                                   &cls2_cache)) < 0)
    {
      g_printerr ("Error: unable to allocate filter cache: %s\n", nl_geterror(err));
      exit (1);
    }
  nl_cache_mngt_provide (cls2_cache);

  /* add classifiers */
  /* TODO: tcindex classifiers not supported by libnl?
   * This patch could help:
   * http://marc.info/?l=linux-netdev&m=120888202117595&w=2
   */
  /* _add_filter_tcindex (TC_HANDLE (2, 0)); */

  cmd = g_strdup_printf (
    "tc qdisc add dev ifb0 "Q_ESTIMATOR" handle 1:0 root dsmark indices 4 default_index 0 && "
    "tc qdisc add dev ifb0 "Q_ESTIMATOR" handle 2:0 parent 1:0 htb r2q 2 && "
    "tc class add dev ifb0 "Q_ESTIMATOR" parent 2:0 classid 2:1 htb rate 50000bps ceil 50000bps && " /* SSH */
    "tc qdisc add dev ifb0 "Q_ESTIMATOR" handle 3:0 parent 2:1 sfq && "
    "tc class add dev ifb0 "Q_ESTIMATOR" parent 2:0 classid 2:2 htb rate %"G_GUINT64_FORMAT"bps && "
    "tc qdisc add dev ifb0 "Q_ESTIMATOR" handle 4:0 parent 2:2 sfq && "
    "tc class add dev ifb0 "Q_ESTIMATOR" parent 2:0 classid 2:3 htb rate %"G_GUINT64_FORMAT"bps ceil %"G_GUINT64_FORMAT"bps && "
    "tc qdisc add dev ifb0 "Q_ESTIMATOR" handle 5:0 parent 2:3 sfq && "
    //
    "tc filter add dev ifb0 parent 2:0 protocol all prio 1 tcindex mask 0x3 shift 0 && "
    "tc filter add dev ifb0 parent 2:0 protocol all prio 1 handle 3 tcindex classid 2:3 && "
    "tc filter add dev ifb0 parent 2:0 protocol all prio 1 handle 2 tcindex classid 2:2 && "
    "tc filter add dev ifb0 parent 2:0 protocol all prio 1 handle 1 tcindex classid 2:1 && "

    "tc filter add dev ifb0 parent 1:0 protocol all prio 1 handle 1:0:0 u32 divisor 1 && "
    "tc filter add dev ifb0 parent 1:0 protocol all prio 1 u32 match u8 0x6 0xff at 9 offset at 0 mask 0f00 shift 6 eat link 1:0:0 && "
    "tc filter add dev ifb0 parent 1:0 protocol all prio 1 handle 1:0:1 u32 ht 1:0:0 match u16 0x16 0xffff at 2 classid 1:1 && "
    "tc filter add dev ifb0 parent 1:0 protocol all prio 1 handle 2:0:0 u32 divisor 1 && "
    "tc filter add dev ifb0 parent 1:0 protocol all prio 1 u32 match u8 0x6 0xff at 9 match u32 0x%x 0x%x at 12 match u32 0x%x 0x%x at 16 offset at 0 mask 0f00 shift 6 eat link 2:0:0 && "
    "tc filter add dev ifb0 parent 1:0 protocol all prio 1 handle 2:0:1 u32 ht 2:0:0 match u16 0x%x 0x%x at 2 match u16 0x%x 0x%x at 0 classid 1:2 && "
    "tc filter add dev ifb0 parent 1:0 protocol all prio 1 u32 match u32 0x0 0x0 at 0 classid 1:3",
    stream_rate, background_rate, background_rate,
    *ip_src, ip_src_mask,
    *ip_dst, ip_dst_mask,
    tcp_dport, tcp_dport_mask,
    tcp_sport, tcp_sport_mask
  );

  g_print ("%s\n", cmd);
  err = system (cmd);
  g_print ("cmd returned %d\n", err);
  g_free (cmd);

  previous_port = tcp_dport;

  g_print ("Adding traffic control: tcp_dport=%d stream_rate=%"G_GUINT64_FORMAT" background_rate=%"G_GUINT64_FORMAT" : done.\n", tcp_dport, stream_rate, background_rate);

}

struct tcmmd_stats {
  guint64 qdisc_root_bytes;
  guint64 qdisc_stream_bytes;
  guint64 qdisc_background_bytes;
};

static void
qdisc_stats_cb (struct nl_object *obj, void *arg)
{
  struct rtnl_qdisc *qdisc = nl_object_priv(obj);
  struct rtnl_tc *tc = (struct rtnl_tc *) qdisc;
  struct tcmmd_stats *stats = arg;
  char buf[32];

  g_print ("stats of qdisc handle %s %s: RTNL_TC_PACKETS=%"G_GUINT64_FORMAT" RTNL_TC_BYTES=%"G_GUINT64_FORMAT"\n",
           rtnl_tc_handle2str (rtnl_tc_get_handle (tc), buf, sizeof(buf)),
           rtnl_tc_get_kind (tc),
           rtnl_tc_get_stat (tc, RTNL_TC_PACKETS),
           rtnl_tc_get_stat (tc, RTNL_TC_BYTES));

  g_print ("  - RTNL_TC_PACKETS:    %"G_GUINT64_FORMAT"\n", rtnl_tc_get_stat (tc, RTNL_TC_PACKETS));
  g_print ("  - RTNL_TC_BYTES:      %"G_GUINT64_FORMAT"\n", rtnl_tc_get_stat (tc, RTNL_TC_BYTES));
  g_print ("  - RTNL_TC_RATE_BPS:   %"G_GUINT64_FORMAT"\n", rtnl_tc_get_stat (tc, RTNL_TC_RATE_BPS));
  g_print ("  - RTNL_TC_RATE_PPS:   %"G_GUINT64_FORMAT"\n", rtnl_tc_get_stat (tc, RTNL_TC_RATE_PPS));
  g_print ("  - RTNL_TC_QLEN:       %"G_GUINT64_FORMAT"\n", rtnl_tc_get_stat (tc, RTNL_TC_QLEN));
  g_print ("  - RTNL_TC_BACKLOG:    %"G_GUINT64_FORMAT"\n", rtnl_tc_get_stat (tc, RTNL_TC_BACKLOG));
  g_print ("  - RTNL_TC_DROPS:      %"G_GUINT64_FORMAT"\n", rtnl_tc_get_stat (tc, RTNL_TC_DROPS));
  g_print ("  - RTNL_TC_REQUEUES:   %"G_GUINT64_FORMAT"\n", rtnl_tc_get_stat (tc, RTNL_TC_REQUEUES));
  g_print ("  - RTNL_TC_OVERLIMITS: %"G_GUINT64_FORMAT"\n", rtnl_tc_get_stat (tc, RTNL_TC_OVERLIMITS));

  /* if tcmmd didn't install any rules, the default pfifo_fast is on 0:0 */
  if (rtnl_tc_get_handle (tc) == TC_HANDLE (0, 0) ||
      rtnl_tc_get_handle (tc) == TC_HANDLE (1, 0))
    stats->qdisc_root_bytes = rtnl_tc_get_stat (tc, RTNL_TC_BYTES);

  if (rtnl_tc_get_handle (tc) == TC_HANDLE (4, 0) &&
      g_strcmp0 (rtnl_tc_get_kind (tc), "sfq") == 0)
    stats->qdisc_stream_bytes = rtnl_tc_get_stat (tc, RTNL_TC_BYTES);

  if (rtnl_tc_get_handle (tc) == TC_HANDLE (5, 0) &&
      g_strcmp0 (rtnl_tc_get_kind (tc), "sfq") == 0)
    stats->qdisc_background_bytes = rtnl_tc_get_stat (tc, RTNL_TC_BYTES);
}

void
tcmmdrtnl_get_stats (guint64 *qdisc_root_bytes,
                     guint64 *qdisc_stream_bytes,
                     guint64 *qdisc_background_bytes)
{
  struct rtnl_qdisc *qdisc;
  struct rtnl_tc *tc;
  int err;
  struct tcmmd_stats stats = {0,};

  if ((err = nl_cache_refill(sock, qdisc_cache)))
    {
      g_printerr ("Error: cannot sync cache: %s\n", nl_geterror(err));
      exit (1);
    }

  if (!(qdisc = rtnl_qdisc_alloc ()))
    {
      g_printerr ("Error: unable to allocate qdisc\n");
      exit (1);
    }
  tc = (struct rtnl_tc *) qdisc;

  /* dev ifb0 root */
  rtnl_tc_set_link (tc, ifb_link);
  //rtnl_tc_set_kind (tc, "sfq");

  nl_cache_foreach_filter (qdisc_cache, OBJ_CAST(qdisc), qdisc_stats_cb, &stats);

  if (qdisc_root_bytes)
    *qdisc_root_bytes = stats.qdisc_root_bytes;
  if (qdisc_stream_bytes)
    *qdisc_stream_bytes = stats.qdisc_stream_bytes;
  if (qdisc_background_bytes)
    *qdisc_background_bytes = stats.qdisc_background_bytes;
}

