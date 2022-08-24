EXTRA_DIST=
lib_LTLIBRARIES=
sbin_PROGRAMS = ldmsd

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@

AM_CFLAGS = \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

LZAP = $(top_builddir)/lib/src/zap/libzap.la
LMMALLOC = $(top_builddir)/lib/src/mmalloc/libmmalloc.la
LOVIS_UTIL = $(top_builddir)/lib/src/ovis_util/libovis_util.la
LCOLL = $(top_builddir)/lib/src/coll/libcoll.la
LJSON_UTIL = $(top_builddir)/lib/src/ovis_json/libovis_json.la
LOVIS_EVENT = $(top_builddir)/lib/src/ovis_event/libovis_event.la
LOVIS_CTRL = $(top_builddir)/lib/src/ovis_ctrl/libovis_ctrl.la
LOVIS_EV = $(top_builddir)/lib/src/ovis_ev/libovis_ev.la
LLDMSD = ../ldmsd
LSAMPLER = $(top_builddir)/ldms/src/sampler

AM_CFLAGS += -DPLUGINDIR='"$(pkglibdir)"'

ldmsd_SOURCES = $(LLDMSD)/ldmsd.c $(LLDMSD)/ldmsd_config.c \
	$(LLDMSD)/ldmsd_request.c \
	$(LLDMSD)/ldmsd_request.h \
	$(LLDMSD)/ldmsd_cfgobj.c $(LLDMSD)/ldmsd_prdcr.c $(LLDMSD)/ldmsd_updtr.c $(LLDMSD)/ldmsd_strgp.c \
	$(LLDMSD)/ldmsd_failover.c $(LLDMSD)/ldmsd_group.c $(LLDMSD)/ldmsd_auth.c \
	$(LLDMSD)/ldmsd_event.c $(LLDMSD)/ldmsd_event.h \
	$(LLDMSD)/ldmsd_decomp.c
ldmsd_LDADD = ../core/libldms.la ../ldmsd/libldmsd_request.la ../ldmsd/libldmsd_stream.la \
	$(LZAP) $(LMMALLOC) $(LOVIS_UTIL) $(LCOLL) $(LJSON_UTIL) \
	$(LOVIS_EVENT) $(LOVIS_EV) -lpthread $(LOVIS_CTRL) -lm -ldl
ldmsd_LDADD += $(LSAMPLER)/meminfo/libmeminfo.la \
		$(LSAMPLER)/vmstat/libvmstat.la \
		$(LSAMPLER)/loadavg/libloadavg.la \
		$(LSAMPLER)/coretemp/libcoretemp.la
ldmsd_CFLAGS = $(AM_CFLAGS)
ldmsd_LDFLAGS = $(AM_LDFLAGS) -rdynamic -pthread -static
