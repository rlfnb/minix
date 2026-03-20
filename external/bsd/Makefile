#	$NetBSD: Makefile,v 1.54 2015/09/28 17:12:46 christos Exp $

.include <bsd.own.mk>

#MINIX:
# bind and dhcp (ISC) use OpenSSL 1.0 internal APIs incompatible with 3.0.
# dhcpcd (the DHCP client MINIX actually uses) does not depend on bind.
SUBDIR=	byacc dhcpcd \
	fetch file flex less \
	libarchive libevent libpcap mdocml \
	openresolv tcpdump tmux top

.if (${MKATF} != "no")
SUBDIR+= atf
.endif
.if (${MKCRYPTO} != "no")
SUBDIR+= pkg_install
.endif
# IP Filter
.if (${MKIPFILTER} != "no")
SUBDIR+=ipf
.endif
.if (${MKISCSI} != "no")
SUBDIR+= iscsi 
.endif
.if (${MKKYUA} != "no")
SUBDIR+= lutok kyua-testers kyua-cli kyua-atf-compat
.endif
.if (${MKLDAP} != "no")
SUBDIR+= openldap
.endif
.if (${MKLIBCXX} != "no")
SUBDIR+= libc++
.endif
.if (${MKLLVM} != "no")
.if (${MKLLVMCMDS} != "no") && defined(__MINIX)
SUBDIR+= llvm
.endif # (${MKLLVMCMDS} != "no") && defined(__MINIX)
.endif
.if (${MKPCC} != "no")
SUBDIR+= pcc
.endif
.if (${MKDTRACE} != "no")
SUBDIR+= libproc
SUBDIR+= librtld_db
.endif

#MINIX:
SUBDIR+= blacklist nvi
# dhcp (ISC DHCP server) disabled - requires bind/OpenSSL 1.0 APIs

.include <bsd.subdir.mk>
