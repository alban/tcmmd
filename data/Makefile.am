systembusdir = $(sysconfdir)/dbus-1/system.d
systembus_DATA = org.tcmmd.conf

systembusactivationdir = $(datadir)/dbus-1/system-services
systembusactivation_DATA = org.tcmmd.service

modulesddir = $(sysconfdir)/modules-load.d
modulesd_DATA = tcmmd.conf

systemdsystemdir = $(prefix)/lib/systemd/system
systemdsystem_DATA = tcmmd.service

install-data-local: tcmmd.service
	install -d $(DESTDIR)$(systemdsystemdir)
	ln -s tcmmd.service $(DESTDIR)$(systemdsystemdir)/dbus-org.tcmmd.service

EXTRA_DIST = $(systembus_DATA) $(systembusactivation_DATA) $(modulesd_DATA) $(systemdsystem_DATA)
