ssi_include_memcached_la_SOURCES = ssi_include_memcached.c
ssi_include_memcached_la_CFLAGS = -Wall ${MODULE_CFLAGS}
ssi_include_memcached_la_LDFLAGS = -rpath ${AP_LIBEXECDIR} -module -avoid-version ${MODULE_LDFLAGS}

mod_LTLIBRARIES = ssi_include_memcached.la
moddir=${AP_LIBEXECDIR}

install: install-am
	rm -f $(DESTDIR)${AP_LIBEXECDIR}/ssi_include_memcached.a
	rm -f $(DESTDIR)${AP_LIBEXECDIR}/ssi_include_memcached.la