# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgstrubberband.la

# sources used to compile this plug-in
libgstrubberband_la_SOURCES = gstrubberband.c gstrubberband.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstrubberband_la_CFLAGS = $(GST_CFLAGS) $(RUBBERBAND_CFLAGS)
libgstrubberband_la_LIBADD = $(GST_LIBS) $(RUBBERBAND_LIBS)
libgstrubberband_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrubberband_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstrubberband.h
