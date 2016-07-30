
# after having changed "#include" statements, call 'rm .depends; make depend' to create a new dependencies file

# use load & unload predefined targets to load & unload the module
# use install target to copy the module to /boot/kernel and generate hints for the kernel loader

# use all-man target to compress man page
# use maninstall target to install man page
# use manlint target to check manpage

# declare name of kernel module
KMOD    =  ndproxy

# enumerate source files for kernel module
SRCS    = ndproxy.c ndparse.c ndpacket.c ndconf.c
MAN    += ndproxy.4

# Include kernel module makefile
.include <bsd.kmod.mk>
.include <bsd.man.mk>

man: all-man maninstall
	man ndproxy

pdf: ndproxy.4
	groff -man ndproxy.4 > ndproxy.ps
	ps2pdf ndproxy.ps

mandoc: ndproxy.4
	mandoc -Ttree ndproxy.4

manhtml: ndproxy.4
	groff -Thtml -man ndproxy.4 > ndproxy.html

catman: ndproxy.4
	groff -Tascii -man ndproxy.4 | sed 's/.\[[012]*m//g' > INSTALLATION.TXT

lines:
	wc -l *.c *.h

etags:
	./etags.sh

propget:
	svn propget svn:keywords *.c *.h *.TXT Makefile

# call this target after adding a new text file to the repository
propset:
	svn propset svn:keywords Id *.c *.h *.TXT Makefile

ci:
	svn ci -m new

update:
	svn update

