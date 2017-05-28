#!/usr/local/bin/zsh

# index kernel sources with Emacs tags

# http://www.emacswiki.org/emacs/EmacsTags#tags
# M-. : search
# M-, : next
# M-TAB complete

setopt EXTENDED_GLOB

SYS=/usr/src/sys
rm -f TAGS

for D in $SYS/{netinet6,net,netinet} $SYS/*~$SYS/net~$SYS/netinet~$SYS/netinet6~$SYS/netinet6
do
    find $D -type f -name '*.s' | xargs etags --no-members -a
    find $D -type f -name '*.h' | xargs etags --no-members -a
done

for D in $SYS/{netinet6,net,netinet} $SYS/*~$SYS/net~$SYS/netinet~$SYS/netinet6~$SYS/netinet6
do
    find $D -type f -name '*.c' | xargs etags --no-members -a
done
