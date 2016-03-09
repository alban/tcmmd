/*
 * tcdemo - demo gstreamer application using tcmmd
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

#include <libsoup/soup.h>
#include <clutter-gst/clutter-gst.h>
#include <gst/gst.h>

#include "tcmmd-generated.h"

#define GETTEXT_PACKAGE "tcdemo"

typedef struct
{
  TcmmdManagedConnections *proxy;
  GstElement *source;

  gboolean disable_traffic_control;
  gboolean looping;

  SoupSocket *socket;
  guint audio_bitrate;
  guint video_bitrate;
  gdouble buffer_fill;

  /* true if the buffer was 100% at least once */
  gboolean buffer_ever_full;
  /* true if the buffer is under 15% */
  gboolean buffer_critically_low;

  /* how many times did we lose sync? */
  guint buffer_critically_low_count;

  gboolean setup_queue_done;
} DemoData;

static void
update_daemon (DemoData *self)
{
  SoupAddress *local_address;
  SoupAddress *remote_address;
  guint bitrate;

  if (self->proxy == NULL)
    return;

  bitrate = self->audio_bitrate + self->video_bitrate;
  /* If the bitrate is zero, it is considered unknown. We still want to set up
   * a policy. */

  if (self->socket == NULL)
    {
      g_print ("No socket. Unset policy.\n");
      tcmmd_managed_connections_call_unset_policy (self->proxy,
          NULL, NULL, NULL);
      return;
    }

  local_address = soup_socket_get_local_address (self->socket);
  remote_address = soup_socket_get_remote_address (self->socket);

  g_print ("%s(%s:%d, %s:%d, %u, %f)\n",
           self->disable_traffic_control
             ? "# Traffic control disabled " : "Call SetPolicy",
           soup_address_get_physical (local_address),
           soup_address_get_port (local_address),
           soup_address_get_physical (remote_address),
           soup_address_get_port (remote_address),
           bitrate,
           self->buffer_fill);

  if (self->disable_traffic_control)
    return;

  tcmmd_managed_connections_call_set_policy (self->proxy,
      soup_address_get_physical (local_address),
      soup_address_get_port (local_address),
      soup_address_get_physical (remote_address),
      soup_address_get_port (remote_address),
      bitrate,
      self->buffer_fill,
      NULL, NULL, NULL);
}

static void
update_soup_socket (DemoData *self)
{
  SoupSocket *socket;

  g_object_get (self->source, "soup-socket", &socket, NULL);
  if (socket == self->socket)
    {
      g_clear_object (&socket);
      return;
    }

  g_clear_object (&self->socket);
  self->socket = socket;
  update_daemon (self);
}

static void
source_setup_cb (GstElement *playbin,
    GstElement *source,
    DemoData *self)
{
  if (source == self->source)
    return;

  g_clear_object (&self->source);
  g_clear_object (&self->socket);

  if (source != NULL)
    {
      self->source = g_object_ref (source);

      /* Watch the SoupSocket on which current trafic is done */
      g_signal_connect_swapped (self->source, "notify::soup-socket",
          G_CALLBACK (update_soup_socket), self);
      update_soup_socket (self);
    }

  /* If self->socket != NULL it means that update_soup_socket() already called
   * update_daemon(). */
  if (self->socket == NULL)
    update_daemon (self);
}

static void
audio_tags_changed_cb (GstElement *playbin,
    gint stream,
    DemoData *self)
{
  GstTagList *tags = NULL;
  guint bitrate;

  g_signal_emit_by_name (playbin, "get-audio-tags", stream, &tags);
  if (tags == NULL)
    return;

  if (gst_tag_list_get_uint (tags, GST_TAG_BITRATE, &bitrate))
    {
      if (bitrate != self->audio_bitrate)
        {
          /* FIXME: We could have multiple audio streams? */
          self->audio_bitrate = bitrate;
          update_daemon (self);
        }
    }
}

static void
video_tags_changed_cb (GstElement *playbin,
    gint stream,
    DemoData *self)
{
  GstTagList *tags = NULL;
  guint bitrate;

  g_signal_emit_by_name (playbin, "get-video-tags", stream, &tags);
  if (tags == NULL)
    return;

  if (gst_tag_list_get_uint (tags, GST_TAG_BITRATE, &bitrate))
    {
      if (bitrate != self->video_bitrate)
        {
          /* FIXME: We could have multiple video streams? */
          self->video_bitrate = bitrate;
          update_daemon (self);
        }
    }
}

static void
_playbin_set_low_percent (GstBin *playbin)
{
  GstIterator *it;
  gboolean done;
  GValue value = { 0, };
  GstElement *element;

  it = gst_bin_iterate_recurse (GST_BIN (playbin));

  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (it, &value)) {
      case GST_ITERATOR_OK:
        element = GST_ELEMENT (g_value_get_object (&value));
        /* the pipeline has a GstMultiQueue but it seems GstQueue2 is the one
         * used here. */
        if (element &&
            g_type_from_name ("GstQueue2") == G_TYPE_FROM_INSTANCE (element))
          {
            g_print ("- set low-percent on GstQueue2.\n");
            g_object_set (element,
                          "low-percent", 75,
                          "max-size-bytes", 4194304,
                          "max-size-buffers", 200,
                          "max-size-time", G_GUINT64_CONSTANT(4000000000),
                          NULL);
          }
        g_value_unset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }

  gst_iterator_free (it);
}

static void
buffer_fill_notify_cb (ClutterGstPlayer *player,
    GParamSpec *param_spec,
    DemoData *self)
{
  gdouble buffer_fill;

  g_object_get (player, "buffer-fill", &buffer_fill, NULL);

  /* Delay queue setup later: the queue element is not added synchronously */
  if (buffer_fill > 0.0 && !self->setup_queue_done)
    {
      GstElement *playbin;

      playbin = clutter_gst_player_get_pipeline (CLUTTER_GST_PLAYER (player));
      _playbin_set_low_percent (GST_BIN (playbin));

      self->setup_queue_done = TRUE;
    }

  if (buffer_fill == 1.0)
    {
      self->buffer_ever_full = TRUE;
      self->buffer_critically_low = FALSE;
    }

  if (self->buffer_ever_full && buffer_fill < 0.15 && !self->buffer_critically_low)
    {
      self->buffer_critically_low = TRUE;
      self->buffer_critically_low_count++;
    }

  /* Don't bother with a change less than 5% */
  if (ABS (self->buffer_fill - buffer_fill) > 0.05 ||
      (self->buffer_fill == 1.0) != (buffer_fill == 1.0))
    {
      self->buffer_fill = buffer_fill;
      update_daemon (self);
    }
}

static void
eos_cb (ClutterGstPlayer *player,
    DemoData *self)
{
  if (self->looping)
    {
      clutter_gst_playback_set_progress (CLUTTER_GST_PLAYBACK (player), 0.0);
      clutter_gst_player_set_playing (player, TRUE);
    }
  else
    {
      clutter_main_quit ();
    }
}

static void
got_proxy_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  DemoData *self = user_data;
  GError *error = NULL;

  self->proxy = tcmmd_managed_connections_proxy_new_for_bus_finish (result,
      &error);
  if (self->proxy == NULL)
    {
      g_critical ("Failed to get proxy: %s\n", error->message);
      g_clear_error (&error);
      return;
    }

  update_daemon (self);
}

gint
main (gint argc,
    gchar *argv[])
{
  DemoData self = { NULL, };
  ClutterColor stage_color = { 0x00, 0x00, 0x00, 0xff };
  ClutterActor *stage;
  ClutterActor *vactor;
  ClutterGstPlayback *player;
  GstElement *playbin;
  GOptionContext *context;
  GError *error = NULL;

  GOptionEntry option_entries[] =
  {
    { "disable-tc", 'd', 0, G_OPTION_ARG_NONE, &self.disable_traffic_control, "Disable traffic control", NULL },
    { "looping",       'l', 0, G_OPTION_ARG_NONE, &self.looping, "Start again at the end of the stream", NULL },
    { NULL }
  };

  clutter_gst_init (&argc, &argv);

  context = g_option_context_new ("- traffic control demo");
  g_option_context_add_main_entries (context, option_entries, GETTEXT_PACKAGE);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_print ("option parsing failed: %s\n", error->message);
      return 1;
    }

  if (argc != 2)
    {
      g_print ("Usage: %s <URL>\n", argv[0]);
      return 1;
    }

  if (!g_str_has_prefix (argv[1], "http://") &&
      !g_str_has_prefix (argv[1], "https://"))
    {
      g_print ("Only http(s):// URLs are accepted\n");
      return 1;
    }

  stage = clutter_stage_new ();
  clutter_actor_set_background_color (stage, &stage_color);
  clutter_actor_set_size (stage, 768, 576);
  clutter_stage_set_minimum_size (CLUTTER_STAGE (stage), 640, 480);
  g_signal_connect (stage, "destroy",
      G_CALLBACK (clutter_main_quit), NULL);

  player = clutter_gst_playback_new ();
  vactor = g_object_new (CLUTTER_TYPE_ACTOR,
      "width", clutter_actor_get_width (stage),
      "height", clutter_actor_get_height (stage),
      "content", g_object_new (CLUTTER_GST_TYPE_ASPECTRATIO,
                     "player", player,
                     NULL),
      NULL);
  clutter_actor_add_child (stage, vactor);

  g_signal_connect (player, "eos",
      G_CALLBACK (eos_cb), &self);

  clutter_gst_playback_set_buffer_size (player, GST_SECOND);
  clutter_gst_playback_set_buffering_mode (player,
      CLUTTER_GST_BUFFERING_MODE_STREAM);

  g_signal_connect (player, "notify::buffer-fill",
      G_CALLBACK (buffer_fill_notify_cb), &self);

  playbin = clutter_gst_player_get_pipeline (CLUTTER_GST_PLAYER (player));
  g_signal_connect (playbin, "source-setup",
      G_CALLBACK (source_setup_cb), &self);
  g_signal_connect (playbin, "audio-tags-changed",
      G_CALLBACK (audio_tags_changed_cb), &self);
  g_signal_connect (playbin, "video-tags-changed",
      G_CALLBACK (video_tags_changed_cb), &self);

  clutter_gst_playback_set_uri (player, argv[1]);
  clutter_gst_player_set_playing (CLUTTER_GST_PLAYER (player), TRUE);
  clutter_actor_show (stage);

  tcmmd_managed_connections_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
      G_DBUS_PROXY_FLAGS_NONE,
      "org.tcmmd",
      "/org/tcmmd/ManagedConnections",
      NULL, got_proxy_cb, &self);

  clutter_main ();

  g_print ("buffer_critically_low_count=%u\n", self.buffer_critically_low_count);

  g_clear_object (&self.source);
  g_clear_object (&self.socket);
  g_clear_object (&self.proxy);

  return EXIT_SUCCESS;
}
