#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include "trie.h"
#include "util.h"

enum {
	Ntoks = 1024,
};

typedef struct Prog Prog;
typedef struct Ext Ext;

struct Prog {
	char*	str;
	char*	prog;
	int	textok;
};

struct Ext {
	char*	str;
	int	len;
	char*	prog;
	int	textok;
};

/*
 * These tables dictate which program is
 * used to generate tags for
 *	1. files with particular extenssions
 *	2. files with particular file(1) output
 * "tagtext" is a builtin that generates as tags
 * the words found.
 *
 * WARNING: running tagtext on non text files
 * is likely to burn all the memory available.
 * Be careful when setting .textok to true.
 *
 * As a safety measure, tagfiles refuses to
 * add as tags words of length < 2 or > 50.
 */

Prog progs[] = {
	{"c program", 	"tagc",	1},
//	{"limbo program", "taglimbo", 1},
	{"ascii", 	"tagtext",	1},
	{"english",	"tagtext",	1},
	{"latin",	"tagtext",	1},
	{"troff input",	"tagtext",	1},
	{"email file",	"tagtext",	1},
	{"troff -ms input", "tagtext",	1},
//	{"HTML file",	"taghtml",	0},
//	{"manual page",	"tagman",	1},
//	{"rc executable", "tagrc",	1},
//	{"rich text",	"tagdoc",	0},
};

Ext exts[] = {
	{".c",	2, "tagc",	1},
	{".h",	2, "tagc",	1},
//	{".b",	2, "taglimbo",	1},
//	{".m",	2, "taglimbo",	1},
//	{".ms",	3, "tagtroff",	1},
//	{".mf",	3, "tagtroff",	1},
//	{".ps",	2, "tagps",	1},
	{".db", 3, "DONTTAG",		0},
	{".log", 4, "DONTTAG",		0},
//	{".doc", 4, "tagdoc",	0},
//	{".xls", 4, "tagdoc",	0},
//	{".ppt", 4, "tagdoc",	0},
//	{".rtf", 4, "tagdoc",	0},
//	{".pdf", 4, "tagpdf",	0},
//	{".eps", 4, "tageps",	0},
//	{".css", 4, "taghtml",	0},
};

void
checkprogs(void)
{
	int	i;

	for(i = 0; i < nelem(progs); i++)
		if(access(progs[i].prog, AEXEC) < 0)
			progs[i].prog = "tagtext";
	for(i = 0; i < nelem(exts); i++)
		if(access(exts[i].prog, AEXEC) < 0)
			exts[i].prog = "tagtext";
}

char*
runfile(char* fname)
{
	int	fd[2];
	char	buf[512];
	int	n, nr;

	if(pipe(fd)<0)
		sysfatal("pipe: %r");
	switch(fork()){
	case -1:
		sysfatal("fork: %r");
	case 0:
		close(fd[0]);
		dup(fd[1], 1);
		close(fd[1]);
		execl("file", "file", fname, nil);
		sysfatal("runfile: %s %r", fname);
	default:
		close(fd[1]);
		n = 0;
		for(;;){
			nr = read(fd[0], buf+n, sizeof buf - n -1);
			if(nr <= 0)
				break;
			n += nr;
			if(n >= sizeof buf -1)
				break;
		}
		close(fd[0]);
		buf[n] = 0;
		waitpid();
		return strdup(buf);
	}
}

void
tag(Trie* t, int triefd, char* s, uvlong qid)
{
	int	l;
	char*	q;
	char	str[200];
	Rune	r;
	int	nc;

	l = strlen(s);
	// don't use and and two char words as tags
	// don't use uttlerly long words, probably not tags.
	if(l < 3 || l > 40)
		return;

	// don't use as tags things other than alphanumeric.
	for(q = s; *q != 0; ){
		nc = chartorune(&r, q);
		if(!isalpharune(r) && !isdigit(r))
			return;
		q += nc;
	}

	if(debug>1)
		fprint(2, "\t%s\n", s);
	if(triefd < 0)
		trieput(t, s, qid);
	else {
		seprint(str, str+sizeof(str), "tag %llux %s", qid, s);
		if (write(triefd, str, strlen(str)) != strlen(str))
			sysfatal("trie ctl: write: %r");
	}
}

void
runtagprog(Trie* t, int triefd, char* fname, Dir* d, char* prog)
{
	int	fd[2];
	char*	ln;
	Biobuf	bin;

	if(pipe(fd)<0)
		sysfatal("pipe: %r");
	switch(fork()){
	case -1:
		sysfatal("fork: %r");
	case 0:
		close(fd[0]);
		dup(fd[1], 1);
		close(fd[1]);
		execl(prog, prog, fname, nil);
		sysfatal("tagprog: %r");
	default:
		close(fd[1]);
		Binit(&bin, fd[0], OREAD);
		while(ln = Brdstr(&bin, '\n', 1)){
			tag(t, triefd, ln, d->qid.path);
			free(ln);
		}
		Bterm(&bin);
		close(fd[0]);
		waitpid();
	}
}

/*
 * BUG: should tokenize according to isalpharune()
 */
void
tagtext(Trie* t, int triefd, char* fname, Dir* d)
{
	Biobuf*	bin;
	char*	ln;
	char*	toks[512];
	char*	wtoks[512];
	char*	delims = " \t\n\r~!@#$%^&*()[]{}+=-/|\\?,.;:><'`\"";
	int	n, nw;
	int	i, j;

	bin = Bopen(fname, OREAD);
	if(bin == nil)
		return;
	while(ln = Brdstr(bin, '\n', 1)){
		n = tokenize(ln, toks, nelem(toks));
		for(i = 0; i < n; i++){
			/* ignore non-white runs of more than 40 chars
			 * they are probably uuencoded data or similar
			 * non human readable data.
			 * otherwise we get many prefixes some times.
			 */
			if(strlen(toks[i]) < 40){
				nw = getfields(toks[i], wtoks, nelem(wtoks), 1, delims);
				for(j = 0; j < nw; j++)
					tag(t, triefd, wtoks[j], d->qid.path);
			}
		}
		free(ln);
	}
	Bterm(bin);
}

void
mktags(Trie* t, int triefd, char* fname, Dir *d)
{
	char*	f;
	char*	kind;
	char*	toks[512];
	int	n;
	int	l;
	char*	s;
	char*	prog;
	int	i;
	int	textok;

	if(debug || (d->qid.type&QTDIR))
		fprint(2, "tag %s\n", fname);
	f = estrdup(fname);
	n = gettokens(f, toks, nelem(toks), "/ \t");
	if(n <= 0){
		free(f);
		return;
	}
	for(i = 0; i < n; i++)
		tag(t, triefd, toks[i], d->qid.path);
	free(f);
	if(d->qid.type&QTDIR)
		return;
	if(d->qid.type&QTAPPEND)	// don't tag log files
		return;
	prog = nil;
	textok = 0;
	n = strlen(d->name);
	for(i = 0; i < nelem(exts); i++){
		l = exts[i].len;
		s = exts[i].str;
		if(n > l && cistrcmp(d->name+n-l, s) == 0){
			prog = exts[i].prog;
			textok = exts[i].textok;
			break;
		}
	}
	if(i == nelem(exts)){
		kind = runfile(fname);
		for(i = 0; i < nelem(progs); i++)
			if(cistrstr(kind, progs[i].str)){
				prog = progs[i].prog;
				textok = progs[i].textok;
				break;
			}
		free(kind);
	}

	if(prog != nil)
		if(strcmp(prog, "tagtext") != 0 && access(prog, AEXEC) == 0){
			if(debug)
				fprint(2, "using %s\n", prog);
			runtagprog(t, triefd, fname, d, prog);
		} else if(textok){
			if(debug)
				fprint(2, "using tagtext\n");
			tagtext(t, triefd, fname, d);
		}
}

void
tagfile(Trie* t, int triefd, char* fname, Dir* d)
{
	Dir*	ad;
	int	fd;
	int	nd;
	Dir*	dd;
	int	i;
	char*	nfname;

	ad = nil;
	if(d == nil){
		ad = d = dirstat(fname);
		if(d == nil)
			goto fail;
	}
	if(d->qid.type&QTDIR){
		fd = open(fname, OREAD);
		if(fd < 0)
			goto fail;
		nd = dirreadall(fd, &dd);
		close(fd);
		if(nd < 0)
			goto fail;
		for(i = 0; i < nd; i++){
			nfname = smprint("%s/%s", fname, dd[i].name);
			tagfile(t, triefd, nfname, &dd[i]);
			free(nfname);
		}
		free(dd);
	}
	mktags(t, triefd, fname, d);
	free(ad);
	return;
fail:
	fprint(2, "%s: %r\n", fname);
	free(ad);
	return;
}

void
usage(void)
{
	fprint(2, "usage: %s [-d] trie file...\n", argv0);
	exits("usage");
}

void
main(int argc, char* argv[])
{
	Trie*	t;
	int	fd;
	int	i;
	Biobuf*	b;
	Biobuf	bout;
	char*	tfname;
	char*	ttfname;
	char*	path;
	Dir*	d;
	int	triefd;

	ARGBEGIN{
	case 'd':
		debug++;
		break;
	default:
		usage();
	}ARGEND;
	if(argc < 2)
		usage();
	tfname = argv[0];
	triefd = -1;
	t = nil;
	checkprogs();

	if(access(tfname, AEXIST)<0)
		t = alloctrie();
	else {
		d = dirstat(tfname);
		if(d == nil)
			sysfatal("%s: %r", tfname);
		if(d->qid.type&QTDIR){
			ttfname = smprint("%s/ctl", tfname);
			triefd = open(ttfname, OWRITE);
			if(triefd < 0)
				sysfatal("%s: %r", ttfname);
			free(ttfname);
		} else {
			b = Bopen(tfname, OREAD);
			if(b == nil)
				sysfatal("%s: %r", tfname);
			t = rdtrie(b);
			if(t == nil)
				sysfatal("%s: %r", tfname);
			Bterm(b);
		}
		free(d);
	}

	for(i = 1; i< argc; i++){
		path = cleanpath(argv[i]);
		tagfile(t, triefd, path, nil);
		free(path);
	}
	if(triefd < 0){
		ttfname = smprint("%s.new", tfname);
		fd = create(ttfname, OWRITE, 0664);
		if(fd < 0)
			sysfatal("%s: %r", ttfname);
		Binit(&bout, fd, OWRITE);
		if(wrtrie(&bout, t) < 0)
			sysfatal("%s: %r", ttfname);
		Bterm(&bout);
		close(fd);
		if(myrename(tfname, ttfname) <0)
			sysfatal("can't rename %s to %s: %r", ttfname, tfname);
//		freetrie(t);
	} else{
		write(triefd, "sync", 4);
		close(triefd);
	}
	exits(nil);
}
