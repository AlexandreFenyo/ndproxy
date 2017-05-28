# $FreeBSD$

PORTNAME=	ndproxy
PORTVERSION=	1.1
CATEGORIES=	net
MASTER_SITES=	https://www.vpnoverdns.com/${PORTNAME}/ \
		http://www.fenyo.net/newweb/${PORTNAME}/
WRKSRC=		${WRKDIR}/sources
USE_RC_SUBR=	ndproxy

NO_PACKAGE=	Should be in sync with the kernel to work correctly

USES=		kmod

MAINTAINER=	fbsd.bugzilla@fenyo.net
COMMENT=	Implementation of IPv6 Neighbor Discovery proxy

LICENSE=	BSD2CLAUSE
LICENSE_FILE=	${WRKSRC}/LICENSE

post-extract:
	${MV} -f work/ndproxy work/sources
	${RM} -f ${WRKSRC}/.depend

pre-everything::
	@if [ ! -f /usr/src/sys/Makefile ]; then \
		${ECHO_MSG} ">> The ndproxy port needs FreeBSD kernel source code to compile."; \
		${ECHO_MSG} ">> Please install FreeBSD kernel source code in /usr/src/sys."; \
		${FALSE}; \
	fi

pre-build:
	(cd ${BUILD_WRKSRC}; ${MAKE} depend)

do-install:
	${MKDIR} ${STAGEDIR}${PREFIX}/lib/${PORTNAME}
	${INSTALL_KLD} ${WRKSRC}/${PORTNAME}.ko ${STAGEDIR}${PREFIX}/lib/${PORTNAME}
	${INSTALL_MAN} ${WRKSRC}/${PORTNAME}.4 ${STAGEDIR}${PREFIX}/man/man4/

.include <bsd.port.mk>
