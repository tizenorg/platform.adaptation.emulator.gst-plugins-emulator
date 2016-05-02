# Note: plugindir is set in configure

##############################################################################
# TODO: change libgstemul.la to something else, e.g. libmysomething.la     #
##############################################################################
plugin_LTLIBRARIES = libgstemul.la

##############################################################################
# TODO: for the next set of variables, name the prefix if you named the .la, #
#  e.g. libmysomething.la => libmysomething_la_SOURCES                       #
#                            libmysomething_la_CFLAGS                        #
#                            libmysomething_la_LIBADD                        #
#                            libmysomething_la_LDFLAGS                       #
##############################################################################

# sources used to compile this plug-in
libgstemul_la_SOURCES = gstmaru.c \
	gstmaruutils.c \
	gstmaruviddec.c \
	gstmaruauddec.c \
	gstmaruvidenc.c \
	gstmaruaudenc.c \
	gstmaruinterface.c \
	gstmarudevice.c \
	gstmarumem.c

	gstv4l2allocator.c \
	gstv4l2bufferpool.c \
	gstv4l2object.c \
	gstv4l2_calls.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstemul_la_CFLAGS = $(GST_CFLAGS) -g
libgstemul_la_LIBADD = $(GST_LIBS) -lgstaudio-1.0 -lgstpbutils-1.0 -lgstallocators-1.0 -L$(libdir)/gstreamer-1.0 -lgstvideo4linux2
libgstemul_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstemul_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
#noinst_HEADERS = gstmaru.h
