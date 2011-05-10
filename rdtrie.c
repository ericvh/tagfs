#include <u.h>
#include <libc.h>
#include <bio.h>
#include "trie.h"
#include "util.h"
#include "query.h"

void
usage(void)
{
	fprint(2, "usage: %s trie [tag...]\n", argv0);
	exits("usage");
}

void
main(int argc, char* argv[])
{
	Trie*	t;
	Biobuf*	b;
	Biobuf  bout;
	int	pos;
	Texpr*	e;

	ARGBEGIN{
	default:
		usage();
	}ARGEND;
	if(argc < 1)
		usage();
	b = Bopen(argv[0], OREAD);
	if(b == nil)
		sysfatal("%s: %r", argv[0]);
	t = rdtrie(b);
	if(t == nil)
		sysfatal("%s: %r", argv[0]);
	Bterm(b);
	Binit(&bout, 1, OWRITE);
	if(argc == 1)
		printtrie(&bout, t);
	else {
		pos = 0;
		e = parseexpr(argc-1, argv+1, &pos);
		evalexpr(t, e);
		printexprval(e);
		// freeexpr(e);		leak it
	}
	// freetrie(t);		leak it
	exits(nil);
}
