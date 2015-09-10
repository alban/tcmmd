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

#include "tcmmd-dbus.h"
#include "tcmmd-generated.h"

G_DEFINE_TYPE (TcmmdDbus, tcmmd_dbus, G_TYPE_OBJECT)

struct _TcmmdDbusPrivate
{
  GDBusConnection *connection;
  TcmmdManagedConnections *iface;
  guint own_name_id;
  guint watch_id;
};

enum
{
  SET_POLICY,
  SET_FIXED_POLICY,
  UNSET_POLICY,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
name_vanished_cb (GDBusConnection *connection,
    const gchar *name,
    gpointer user_data)
{
  TcmmdDbus *self = user_data;

  g_bus_unwatch_name (self->priv->watch_id);
  self->priv->watch_id = 0;

  g_signal_emit (self, signals[UNSET_POLICY], 0);
}

static void
watch_name (TcmmdDbus *self,
    const gchar *name)
{
  if (self->priv->watch_id != 0)
    g_bus_unwatch_name (self->priv->watch_id);

  self->priv->watch_id = g_bus_watch_name_on_connection (self->priv->connection,
      name, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL,
      name_vanished_cb, self, NULL);
}

static gboolean
handle_set_policy_cb (TcmmdManagedConnections *iface,
    GDBusMethodInvocation *invocation,
    const gchar *src_ip,
    guint src_port,
    const gchar *dest_ip,
    guint dest_port,
    guint bitrate,
    gdouble buffer_fill,
    gpointer user_data)
{
  TcmmdDbus *self = user_data;

  g_print ("SetPolicy: src=%s:%d, dest=%s:%d, bitrate=%d, buffer=%d%%\n",
      src_ip, src_port, dest_ip, dest_port, bitrate, (gint) (buffer_fill * 100.0));

  watch_name (self, g_dbus_method_invocation_get_sender (invocation));

  tcmmd_managed_connections_set_bitrate (self->priv->iface, bitrate);
  tcmmd_managed_connections_set_buffer_fill (self->priv->iface, buffer_fill);

  g_signal_emit (self, signals[SET_POLICY], 0,
      src_ip, src_port, dest_ip, dest_port, bitrate, buffer_fill);

  return TRUE;
}

static gboolean
handle_set_fixed_policy_cb (TcmmdManagedConnections *iface,
    GDBusMethodInvocation *invocation,
    const gchar *src_ip,
    guint src_port,
    const gchar *dest_ip,
    guint dest_port,
    guint stream_rate,
    guint background_rate,
    gpointer user_data)
{
  TcmmdDbus *self = user_data;

  g_print ("SetFixedPolicy: %s:%d -> %s:%d stream_rate=%d, background_rate=%d\n",
      src_ip, src_port, dest_ip, dest_port, stream_rate, background_rate);

  watch_name (self, g_dbus_method_invocation_get_sender (invocation));

  g_signal_emit (self, signals[SET_FIXED_POLICY], 0,
      src_ip, src_port, dest_ip, dest_port, stream_rate, background_rate);

  return TRUE;
}

static gboolean
handle_unset_policy_cb (TcmmdManagedConnections *iface,
    GDBusMethodInvocation *invocation,
    gpointer user_data)
{
  TcmmdDbus *self = user_data;

  g_print ("UnsetPolicy\n");

  if (self->priv->watch_id != 0)
    {
      g_bus_unwatch_name (self->priv->watch_id);
      self->priv->watch_id = 0;
    }

  g_signal_emit (self, signals[UNSET_POLICY], 0);

  return TRUE;
}

static void
on_bus_acquired (GDBusConnection *connection,
    const gchar *name,
    gpointer user_data)
{
  TcmmdDbus *self = user_data;
  GError *error = NULL;

  self->priv->connection = g_object_ref (connection);

  self->priv->iface = tcmmd_managed_connections_skeleton_new ();
  g_signal_connect (self->priv->iface, "handle-set-policy",
                    G_CALLBACK (handle_set_policy_cb), self);
  g_signal_connect (self->priv->iface, "handle-set-fixed-policy",
                    G_CALLBACK (handle_set_fixed_policy_cb), self);
  g_signal_connect (self->priv->iface, "handle-unset-policy",
                    G_CALLBACK (handle_unset_policy_cb), self);

  if (!g_dbus_interface_skeleton_export (
          G_DBUS_INTERFACE_SKELETON (self->priv->iface),
          self->priv->connection,
          "/org/tcmmd/ManagedConnections", &error))
    {
      g_critical ("Failed to export iface: %s", error->message);
      g_clear_error (&error);
    }
}

static void
on_name_lost (GDBusConnection *connection,
    const gchar *name,
    gpointer user_data)
{
  g_critical ("Name lost!");
}

static void
tcmmd_dbus_init (TcmmdDbus *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      TCMMD_TYPE_DBUS, TcmmdDbusPrivate);

  self->priv->own_name_id = g_bus_own_name (G_BUS_TYPE_SYSTEM, "org.tcmmd",
      G_BUS_NAME_OWNER_FLAGS_NONE,
      on_bus_acquired, NULL, on_name_lost, self, NULL);
}

static void
tcmmd_dbus_dispose (GObject *object)
{
  TcmmdDbus *self = (TcmmdDbus *) object;

  if (self->priv->own_name_id != 0)
    {
      g_bus_unown_name (self->priv->own_name_id);
      self->priv->own_name_id = 0;
    }

  if (self->priv->watch_id != 0)
    {
      g_bus_unwatch_name (self->priv->watch_id);
      self->priv->watch_id = 0;
    }

  g_clear_object (&self->priv->connection);
  g_clear_object (&self->priv->iface);

  G_OBJECT_CLASS (tcmmd_dbus_parent_class)->dispose (object);
}

static void
tcmmd_dbus_class_init (TcmmdDbusClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = tcmmd_dbus_dispose;

  g_type_class_add_private (object_class, sizeof (TcmmdDbusPrivate));

  signals[SET_POLICY] =
      g_signal_new ("set-policy",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL, NULL,
          G_TYPE_NONE,
          6, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT,
          G_TYPE_UINT, G_TYPE_DOUBLE);

  signals[SET_FIXED_POLICY] =
      g_signal_new ("set-fixed-policy",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL, NULL,
          G_TYPE_NONE,
          6, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT,
          G_TYPE_UINT, G_TYPE_UINT);

  signals[UNSET_POLICY] =
      g_signal_new ("unset-policy",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL, NULL,
          G_TYPE_NONE,
          0);
}

TcmmdDbus *
tcmmd_dbus_new (void)
{
  return g_object_new (TCMMD_TYPE_DBUS, NULL);
}
