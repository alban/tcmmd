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

#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "tcmmd_rtnl.h"
#include "tcmmd-dbus.h"

static gchar *iface_name;
static gchar *filename_stats;
static FILE *file_stats = NULL;

static GOptionEntry option_entries[] =
{
  { "interface", 'i', 0, G_OPTION_ARG_STRING, &iface_name, "Network interface (usually eth0)", "IFACE" },
  { "save-stats", 's', 0, G_OPTION_ARG_STRING, &filename_stats, "Save traffic control stats in a file", "FILE" },
  { NULL }
};

#define GETTEXT_PACKAGE "tcmmd"

#define DEFAULT_IFACE "eth0"

/* Keep some bandwidth for SSH :) */
#define MINIMUM_BANDWIDTH 5000 /* 5 kB/s */

#define INFINITE_BANDWIDTH 0xffffffffULL

/* This cache is what the application told us. So it is from the point of view
 * of the application: tcp_dport_cache is likely to be http=80 and
 * tcp_sport_cache is likely to be a random port.
 *
 * When calling tcmmdrtnl_add_rules, we are adding rules for ingress packets,
 * so it is from the point of view of the remote sender: tcp_sport_cache is
 * likely to be http=80 and tcp_dport_cache is likely to be a random port.
 *
 * Yes, it is confusing.
 */
static in_addr_t ip_src_cache = 0;
static in_addr_t ip_dst_cache = 0;
static uint16_t tcp_sport_cache = 0;
static uint16_t tcp_dport_cache = 0;

static guint64 bandwidth = 0;
static int percentage = 0;
static gboolean in_panic = FALSE;
static guint timeout_id = 0;

static gboolean
stats_cb (gpointer data)
{
  struct timeval tv = {0,};
  guint64 qdisc_root_bytes = 0;
  guint64 qdisc_stream_bytes = 0;
  guint64 qdisc_background_bytes = 0;

  if (!file_stats)
    return;

  gettimeofday (&tv, NULL);

  tcmmdrtnl_get_stats (&qdisc_root_bytes, &qdisc_stream_bytes,
                       &qdisc_background_bytes);

  fprintf (file_stats, "%ld.%06ld %"G_GUINT64_FORMAT
           " %"G_GUINT64_FORMAT" %"G_GUINT64_FORMAT
           " %"G_GUINT64_FORMAT" %d\n",
           tv.tv_sec, tv.tv_usec,
           qdisc_root_bytes, qdisc_stream_bytes, qdisc_background_bytes,
           bandwidth, percentage);
  fflush (file_stats);

  return TRUE;
}

static gboolean
update_bandwidth_cb (gpointer data)
{
  guint64 new_bandwidth = 0;

  g_print ("Update callback. Current values: "
           "percentage=%d in_panic=%d tcp_sport_cache=%d bandwidth=%"G_GUINT64_FORMAT"\n",
           percentage, in_panic, tcp_sport_cache, bandwidth);

  if (in_panic)
    {
      new_bandwidth = MINIMUM_BANDWIDTH;
    }
  else
    {
      new_bandwidth = bandwidth * 1.5;
      if (new_bandwidth > INFINITE_BANDWIDTH)
        new_bandwidth = bandwidth;
    }

  if (new_bandwidth != bandwidth)
    {
      bandwidth = new_bandwidth;
      tcmmdrtnl_add_rules (&ip_dst_cache, &ip_src_cache,
                           tcp_dport_cache, tcp_sport_cache,
                           INFINITE_BANDWIDTH, bandwidth);
    }

  return TRUE;
}

static void
on_set_fixed_policy (TcmmdDbus *dbus,
                     const gchar *src_ip_str, guint src_port,
                     const gchar *dst_ip_str, guint dst_port,
                     guint stream_rate,
                     guint background_rate,
                     gpointer user_data)
{
  in_addr_t ip_src_b = 0;
  in_addr_t ip_dst_b = 0;

  if (src_ip_str[0] != '\0')
    ip_src_b = inet_network (src_ip_str);
  if (dst_ip_str[0] != '\0')
    ip_dst_b = inet_network (dst_ip_str);

  if (timeout_id != 0)
    {
      g_source_remove (timeout_id);
      timeout_id = 0;
    }

  tcmmdrtnl_add_rules (&ip_dst_b, &ip_src_b, dst_port, src_port, stream_rate, background_rate);
}

static void
on_set_policy (TcmmdDbus *dbus,
    const gchar *src_ip_str, guint src_port,
    const gchar *dst_ip_str, guint dst_port,
    guint bitrate,
    gdouble buffer_fill,
    gpointer user_data)
{
  gboolean new_panic = FALSE;

  in_addr_t ip_src_b = inet_network (src_ip_str);
  in_addr_t ip_dst_b = inet_network (dst_ip_str);

  percentage = buffer_fill * 100.0;
  if (!in_panic && percentage < 70)
    {
      new_panic = TRUE;
      in_panic = TRUE;
    }
  else if (percentage == 100)
    {
      in_panic = FALSE;
    }

  if (new_panic || src_port != tcp_sport_cache)
    {
      if (timeout_id != 0)
        {
          g_source_remove (timeout_id);
          timeout_id = 0;
        }

      bandwidth = MINIMUM_BANDWIDTH;
      tcp_sport_cache = src_port;
      tcp_dport_cache = dst_port;
      ip_src_cache = ip_src_b;
      ip_dst_cache = ip_dst_b;
      tcmmdrtnl_add_rules (&ip_dst_cache, &ip_src_cache,
                           tcp_dport_cache, tcp_sport_cache,
                           INFINITE_BANDWIDTH, bandwidth);
    }
  else
    {
      if (timeout_id == 0)
        {
          /* schedule change */
          g_print ("Add timeout.\n");
          timeout_id = g_timeout_add (2000, update_bandwidth_cb, NULL);
        }
    }
}

static void
on_unset_policy (TcmmdDbus *dbus,
    gpointer user_data)
{
  if (timeout_id != 0)
    {
      g_source_remove (timeout_id);
      timeout_id = 0;
    }

  tcp_sport_cache = 0;

  tcmmdrtnl_del_rules ();
}

static void signal_handler (int sig)
{
  if (sig == SIGINT || sig == SIGTERM)
    {
      /* By exiting properly instead of being terminated by the signal,
       * atexit will run _uninit to remove the TC rules.
       */
      exit (0);
    }
}

static void
init_signals (void)
{
  struct sigaction sigact;

  sigact.sa_handler = signal_handler;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = 0;
  sigaction (SIGINT, &sigact, (struct sigaction *)NULL);
  sigaction (SIGTERM, &sigact, (struct sigaction *)NULL);

  atexit (tcmmdrtnl_uninit);
}

int
main (int argc, char **argv)
{
  GError *error = NULL;
  GOptionContext *context;
  TcmmdDbus *dbus;
  GMainLoop *loop;

  context = g_option_context_new ("- traffic control multimedia daemon");
  g_option_context_add_main_entries (context, option_entries, GETTEXT_PACKAGE);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_print ("option parsing failed: %s\n", error->message);
      exit (1);
    }

  init_signals ();
  tcmmdrtnl_init (iface_name);
  tcmmdrtnl_init_ifb ();

  g_print ("Init done.\n");

  dbus = tcmmd_dbus_new ();
  g_signal_connect (dbus, "set-policy",
      G_CALLBACK (on_set_policy), NULL);
  g_signal_connect (dbus, "set-fixed-policy",
      G_CALLBACK (on_set_fixed_policy), NULL);
  g_signal_connect (dbus, "unset-policy",
      G_CALLBACK (on_unset_policy), NULL);

  if (filename_stats)
    {
      file_stats = fopen (filename_stats, "w");
      if (!file_stats)
        {
          g_print ("Cannot write to '%s': %s\n",  filename_stats,
                   strerror (errno));
          exit (1);
        }
      fprintf (file_stats, "time qdisc_root_bytes qdisc_stream_bytes qdisc_background_bytes background_bandwidth_requested gst_buffer_percent\n");
      g_timeout_add (1000, stats_cb, NULL);
    }

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  g_object_unref (dbus);

  return 0;
}

