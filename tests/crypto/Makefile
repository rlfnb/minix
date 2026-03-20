# $NetBSD: Makefile,v 1.5 2014/01/15 10:19:55 martin Exp $

.include <bsd.own.mk>

# libcrypto tests reference OpenSSL 1.0 test files not available in 3.0
#TESTS_SUBDIRS=		libcrypto
.if !defined(__MINIX)
TESTS_SUBDIRS+=		opencrypto
.endif # !defined(__MINIX)

TESTSDIR=	${TESTSBASE}/crypto

.include <bsd.test.mk>
