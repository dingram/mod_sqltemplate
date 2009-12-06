sqltemplate_la_SOURCES = sqltemplate.c
sqltemplate_la_CFLAGS = -Wall ${MODULE_CFLAGS}
sqltemplate_la_LDFLAGS = -rpath ${AP_LIBEXECDIR} -module -avoid-version ${MODULE_LDFLAGS}

mod_LTLIBRARIES = sqltemplate.la
moddir=${AP_LIBEXECDIR}

install: install-am
	rm -f $(DESTDIR)${AP_LIBEXECDIR}/sqltemplate.a
	rm -f $(DESTDIR)${AP_LIBEXECDIR}/sqltemplate.la
