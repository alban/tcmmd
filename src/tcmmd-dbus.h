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

#ifndef __TCMMD_DBUS_H__
#define __TCMMD_DBUS_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define TCMMD_TYPE_DBUS \
    (tcmmd_dbus_get_type ())
#define TCMMD_DBUS(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), TCMMD_TYPE_DBUS, \
        TcmmdDbus))
#define TCMMD_DBUS_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), TCMMD_TYPE_DBUS, \
        TcmmdDbusClass))
#define TCMMD_IS_DBUS(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TCMMD_TYPE_DBUS))
#define TCMMD_IS_DBUS_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), TCMMD_TYPE_DBUS))
#define TCMMD_DBUS_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), TCMMD_TYPE_DBUS, \
        TcmmdDbusClass))

typedef struct _TcmmdDbus TcmmdDbus;
typedef struct _TcmmdDbusClass TcmmdDbusClass;
typedef struct _TcmmdDbusPrivate TcmmdDbusPrivate;

struct _TcmmdDbus {
  GObject parent;

  TcmmdDbusPrivate *priv;
};

struct _TcmmdDbusClass {
  GObjectClass parent_class;
};

GType tcmmd_dbus_get_type (void) G_GNUC_CONST;
TcmmdDbus *tcmmd_dbus_new (void);

G_END_DECLS

#endif /* __TCMMD_DBUS_H__ */
