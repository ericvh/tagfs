</$objtype/mkfile

TARG=\
	rdtrie\
	qhash\
	tagfiles\
	tagfs\

SCRIPTS=\
	mktags\
	looktags\

OFILES=\
	util.$O\

HFILES=\
	trie.h\
	query.h\
	util.h\


BIN=/$objtype/bin
SBIN=/rc/bin

</sys/src/cmd/mkmany

install:V:
	for (i in $TARG)
		mk $MKFLAGS $i.install
	cp $SCRIPTS $SBIN
	chmod +x $SBIN/^($SCRIPTS)
	echo contrib/push tags

export:V:
	mk clean
	contrib/push tags
	
$O.rdtrie: rdtrie.$O trie.$O query.$O

$O.qhash: qhash.$O

$O.tagfiles: tagfiles.$O trie.$O

$O.tagfs: tagfs.$O trie.$O query.$O
