<$PLAN9/src/mkhdr

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

<$PLAN9/src/mkmany

$O.rdtrie: rdtrie.$O trie.$O query.$O

$O.qhash: qhash.$O

$O.tagfiles: tagfiles.$O trie.$O

$O.tagfs: tagfs.$O trie.$O query.$O

