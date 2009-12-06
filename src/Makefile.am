mod_sqltemplate_la_SOURCES = mod_sqltemplate.c
mod_sqltemplate_la_CFLAGS = -Wall ${MODULE_CFLAGS}
mod_sqltemplate_la_LDFLAGS = -rpath ${AP_LIBEXECDIR} -module -avoid-version ${MODULE_LDFLAGS}

mod_LTLIBRARIES = mod_sqltemplate.la
moddir=${AP_LIBEXECDIR}

install: install-am
	rm -f $(DESTDIR)${AP_LIBEXECDIR}/mod_sqltemplate.a
	rm -f $(DESTDIR)${AP_LIBEXECDIR}/mod_sqltemplate.la
