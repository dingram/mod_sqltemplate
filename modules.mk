mod_sqltemplate.la: mod_sqltemplate.slo
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version  mod_sqltemplate.lo
DISTCLEAN_TARGETS = modules.mk
shared =  mod_sqltemplate.la
