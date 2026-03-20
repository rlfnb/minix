#	$NetBSD: Makefile,v 1.14 2021/05/30 01:57:00 joerg Exp $

LIB=		c++
WARNS=		4
NOLINT=		# defined

.include <bsd.init.mk>

.PATH: ${LIBCXX_SRCDIR}/src

SRCS=	algorithm.cpp bind.cpp charconv.cpp chrono.cpp \
	debug.cpp exception.cpp format.cpp \
	functional.cpp hash.cpp ios.cpp ios.instantiations.cpp \
	iostream.cpp locale.cpp memory.cpp \
	new.cpp optional.cpp random.cpp random_shuffle.cpp regex.cpp \
	stdexcept.cpp string.cpp strstream.cpp \
	system_error.cpp utility.cpp valarray.cpp variant.cpp \
	vector.cpp

# Threading support - MINIX has no threads, but mutex.cpp provides
# __call_once (with a no-threads fallback) needed by locale.cpp.
SRCS+=	mutex.cpp mutex_destructor.cpp
#SRCS+=	condition_variable.cpp condition_variable_destructor.cpp
#SRCS+=	future.cpp shared_mutex.cpp thread.cpp

.PATH: ${LIBCXX_SRCDIR}/src/filesystem
SRCS+=	directory_iterator.cpp operations.cpp

# typeinfo.cc: prefer libcxxrt's version

LIBCXXRT_SRCS+= \
	auxhelper.cc dynamic_cast.cc exception.cc guard.cc \
	stdexcept.cc terminate.cc typeinfo.cc libelftc_dem_gnu3.c
# cxa_atexit.c: Solaris-only
# memory.cc: already provided by libc++'s new.cpp

.for src in ${LIBCXXRT_SRCS}
rt_${src}: ${LIBCXXRT_SRCDIR}/src/${src}
	cp ${LIBCXXRT_SRCDIR}/src/${src} rt_${src}

SRCS+=		rt_${src}
DPSRCS+=	rt_${src}
CLEANFILES+=	rt_${src}
.endfor

.include <bsd.own.mk>

# Use source-tree headers only — not the (possibly stale) installed ones.
# The clang++ wrapper adds -cxx-isystem destdir/usr/include/c++ which we
# must override by placing the source-tree path first.
CPPFLAGS+=	-nostdinc++
CPPFLAGS+=	-cxx-isystem ${LIBCXX_SRCDIR}/include -cxx-isystem ${.CURDIR}/../include
CPPFLAGS+=	-I${LIBCXXRT_SRCDIR}/src
CPPFLAGS+=	-DLIBCXXRT -D_LIBCPP_BUILDING_LIBRARY
# MINIX has no threading support
CPPFLAGS+=	-D_LIBCPP_HAS_NO_THREADS
CXXFLAGS+=	-std=gnu++17

CWARNFLAGS.gcc+=	-Wno-error

CWARNFLAGS.clang+=	-Wno-error=unused-variable -Wno-error=missing-noreturn
CWARNFLAGS.clang+=	-Wno-error=missing-prototypes -Wno-error=sometimes-uninitialized
CWARNFLAGS.clang+=	-Wno-error=missing-field-initializers -Wno-error=switch
CWARNFLAGS.clang+=	-Wno-error=implicit-exception-spec-mismatch -Wno-error=format-nonliteral -Wno-error=format-security

LDFLAGS+=	-Wl,-z,defs

.include <bsd.lib.mk>
