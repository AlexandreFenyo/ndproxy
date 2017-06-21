# $FreeBSD$

PORTNAME=	ndproxy
PORTVERSION=	1.12
CATEGORIES=	net

MAINTAINER=	fbsd.bugzilla@fenyo.net
COMMENT=	Implementation of IPv6 Neighbor Discovery proxy

LICENSE=	BSD2CLAUSE
LICENSE_FILE=	${WRKSRC}/LICENSE

SUB_FILES=	pkg-message

USE_RC_SUBR=	ndproxy
USE_GITHUB=	yes
GH_ACCOUNT=	AlexandreFenyo
GH_TAGNAME=	v${PORTVERSION}

PLIST_FILES=	lib/ndproxy/ndproxy.ko man/man4/ndproxy.4.gz

USES=		kmod

pre-build:
	(cd ${BUILD_WRKSRC}; ${MAKE} depend)

do-install:
	${MKDIR} ${STAGEDIR}${PREFIX}/lib/${PORTNAME}
	${INSTALL_KLD} ${WRKSRC}/${PORTNAME}.ko ${STAGEDIR}${PREFIX}/lib/${PORTNAME}
	${INSTALL_MAN} ${WRKSRC}/${PORTNAME}.4 ${STAGEDIR}${PREFIX}/man/man4/

.include <bsd.port.mk>
