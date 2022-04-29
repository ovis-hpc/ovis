SUBDIRS =

if ENABLE_RDMA
SUBDIRS += rdma
endif

if ENABLE_FABRIC
SUBDIRS += fabric
endif

if ENABLE_SOCK
SUBDIRS += sock
endif

if ENABLE_UGNI
SUBDIRS += ugni
endif

SUBDIRS += .

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_CFLAGS = -DPLUGINDIR='"$(pkglibdir)"'
libzap_la_SOURCES = zap.c zap_priv.h
libzap_la_LIBADD = -ldl -lpthread

if ENABLE_STANDALONE
libzap_la_LIBADD += $(builddir)/sock/libzap_sock.la
endif

libzapincludedir = $(includedir)/zap
libzapinclude_HEADERS = zap.h

lib_LTLIBRARIES = libzap.la

if ENABLE_ZAPTEST
SUBDIRS += test
endif
