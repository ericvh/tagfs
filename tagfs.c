#include <u.h>
#include <libc.h>
#include <bio.h>
#include <fcall.h>
#include <thread.h>
#include <auth.h>
#include <9p.h>
#include <ctype.h>
#include "trie.h"
#include "util.h"
#include "query.h"

typedef struct Query Query;

struct Query{
	char*	text;
	Texpr*	expr;	// non-nil after query write completed
};

Trie*	trie;
File*	ctlf;
char*	tfname;
char*	ttfname;

static void
fscreate(Req* r)
{
	File*	file;
	Query*	q;
	char*	name;
	char*	uid;
	int	mode;
	File*	f;

	file = r->fid->file;
	name = r->ifcall.name;
	uid = r->fid->uid;
	mode = r->fid->file->dir.mode & 0x777 & r->ifcall.perm;
	mode |= (r->ifcall.perm & ~0x777);
	if(mode&DMDIR){
		respond(r, "queries cannot be directories");
		return;
	}
	if(f = createfile(file, name, uid, mode, nil)){
		q = emalloc9p(sizeof *q);
		q->text = estrdup9p("");
		q->expr = nil;
		f->aux = q;
		closefile(r->fid->file);
		r->fid->file = f;
		r->ofcall.qid = f->dir.qid;
		respond(r, nil);
	} else
		respond(r, "problem creating file");
}

void
tag(Trie* t, char* s, uvlong qid)
{
	int	l;
	char*	q;
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
	trieput(t, s, qid);
}

static void
ctlwrite(Req* r)
{
	char	buf[1024];
	char*	toks[512];
	int	fd;
	Biobuf	bout;
	int	ntoks;
	long	count;
	uvlong	qid;
	int	i;

	count = r->ifcall.count;
	if(count > sizeof(buf)-1)
		count=sizeof(buf)-1;
	r->ofcall.count = count;
	memmove(buf, r->ifcall.data, count);
	buf[count] = 0;
	ntoks = tokenize(buf, toks, nelem(toks));
	if(ntoks < 1){
		respond(r, "null ctl");
		return;
	}
	if(strcmp(toks[0], "tag") == 0){
		if(ntoks < 3){
			respond(r, "ctl usage: tag qid tag...");
			return;
		}
		qid = strtoull(toks[1], nil, 16);
		for(i = 2; i < ntoks; i++)
			tag(trie, toks[i], qid);
	} else if(strcmp(toks[0], "sync") == 0){
		fd = create(ttfname, OWRITE, 0664);
		if(fd < 0){
			respond(r, "bad fid");
			return;
		}
		Binit(&bout, fd, OWRITE);
		if(wrtrie(&bout, trie) < 0){
			close(fd);
			remove(ttfname);
			respond(r, "wrtrie failure");
			return;
		}
		Bterm(&bout);
		close(fd);
		if(rename(tfname, ttfname) < 0){
			respond(r, "rename failure");
			remove(ttfname);
			return;
		}
	} else {
		respond(r, "bad ctl request");
		return;
	}
	respond(r, nil);
}

static void
fswrite(Req* r)
{
	Query*	q;
	File*	f;
	long	count;
	char*	ntext;
	int	l;

	if(r->fid->qid.type&QTDIR){
		respond(r, "bug: write on dir");
		return;
	}
	f = r->fid->file;
	if(f == ctlf){
		ctlwrite(r);
		return;
	}
	count = r->ifcall.count;
	r->ofcall.count = count;
	q = f->aux;
	if(q->expr != nil){
		// a previous query was made. start another.
		freeexpr(q->expr);
		free(q->text);
		q->expr = nil;
		q->text = nil;
	}
	/* append text to query text, ignore offset.
	 */
	l = strlen(q->text);
	ntext = emalloc9p(l + count + 1);
	strcpy(ntext, q->text);
	memmove(ntext+l, r->ifcall.data, count);
	ntext[l+count]=0;
	free(q->text);
	q->text = ntext;
	respond(r, nil);
}

static void
ctlread(Req* r)
{
	char	buf[4096];
	char*	s;
	int	i;

	s = seprint(buf, buf+sizeof(buf), "trie %s\n", tfname);
	s = seprint(s, buf+sizeof(buf), "prefixes %ld\n", ntries);
	s = seprint(s, buf+sizeof(buf), "tags %ld\n", nvaltries);
	s = seprint(s, buf+sizeof(buf), "max entry %ld\n", maxvals);
	if(trie->nents > 0){
		s = seprint(s, buf+sizeof(buf), "%d root runes [", trie->nents);
		for(i = 0; i < trie->nents; i++)
			s = seprint(s, buf+sizeof(buf), "%C", trie->ents[i].r);
		s = seprint(s, buf+sizeof(buf), "]\n");
	}
	if(roott.nents > 0){
		s = seprint(s, buf+sizeof(buf), "%d used runes [", roott.nents);
		for(i = 0; i < roott.nents; i++)
			s = seprint(s, buf+sizeof(buf), "%C", roott.ents[i].r);
		seprint(s, buf+sizeof(buf), "]\n");
	}
	readstr(r, buf);
	respond(r, nil);
}

static void
fsread(Req* r)
{
	Query*	q;
	char**	toks;
	int	ntoks;
	int	atoks;
	char*	s;
	int	pos;
	File*	f;

	if(r->fid->qid.type&QTDIR){
		respond(r, "bug: write on dir");
		return;
	}
	f = r->fid->file;
	if(f == ctlf){
		ctlread(r);
		return;
	}
	q = f->aux;

	/* The first read process the query.
	 * Further reads just retrieve more data,
	 * if any.
	 */
	if(q->expr == nil){
		atoks = 512;
		toks = emalloc9p(atoks*sizeof(char*));
		do {
			s = estrdup9p(q->text);
			ntoks = tokenize(s, toks, atoks);
			if(ntoks == atoks){
				atoks += 512;
				toks = erealloc9p(toks, atoks*sizeof(char*));
				free(s);
			}
		}while(ntoks == atoks);
		pos = 0;
		if(chatty9p)
			fprint(2, "compiling %s (%d toks)\n", q->text, ntoks);
		q->expr = parseexpr(ntoks, toks, &pos);
		if(q->expr == nil){
			free(s);
			free(toks);
			respond(r, "syntax error");
			return;
		}
		if(chatty9p)
			fprint(2, "evaluating %s (%d toks)\n", q->text, ntoks);
		evalexpr(trie, q->expr);
		free(s);
		free(toks);
		free(q->text);
		/* smprintexprval limits the total output to
		 * at most 64K of text
		 */
		q->text = smprintexprval(q->expr);
		if(chatty9p)
			fprint(2, "result is [%s]\n", q->text);
	}
	/* After the query is process,
	 * q->text holds the reply.
	 */
	readstr(r,  q->text);
	respond(r, nil);
}

static void
fsclunk(Fid* fid)
{
	Query*	q;
	File*	f;

	f = fid->file;
	if(f == nil)
		return;
	q = f->aux;
	if(q == nil)
		return;
	if(q->expr != nil){
		/* the query was already made, destroy the file.
		 *
		 * We must incref the file because
		 * removefile assumes that we hold the
		 * reference given to it, and we do not.
		 * We just want the file removed from the tree.
		 */
		free(q->text);
		freeexpr(q->expr);
		free(q);
		f->aux = nil;
		incref(&f->ref);
		removefile(f);
	}
}

static Srv sfs=
{
	.create	=	fscreate,
	.read	=	fsread,
	.write	=	fswrite,
	.destroyfid = 	fsclunk,
};

char*
srvname(char* triefname)
{
	char*	s;
	char*	r;

	s = strrchr(triefname, '/');
	if(s != nil)
		s++;
	else
		s = triefname;
	s = estrdup(s);
	r = strstr(s, ".trie.db");
	if(r != nil)
		strcpy(r, ".tagfs");
	else
		s = smprint("%s.tagfs", s);
	return s;
}

void
usage(void)
{
	fprint(2, "usage: %s [-abcD] [-s srv] [-m mnt] trie\n", argv0);
	threadexits("usage");
}

void
threadmain(int argc, char* argv[])
{
	char*	mnt;
	char*	srv;
	int	mflag;
	Biobuf*	b;
	char*	user;

	srv = nil;
	mnt = nil;
	mflag = MREPL|MCREATE;
	ARGBEGIN{
	case 'a':
		mflag = MAFTER|MCREATE;
		break;
	case 'b':
		mflag = MBEFORE|MCREATE;
		break;
	case 'c':
		mflag = MREPL|MCREATE;
		break;
	case 's':
		srv = EARGF(usage());
		break;
	case 'm':
		mnt = EARGF(usage());
		break;
	case 'D':
		debug = 1;
		chatty9p++;
		break;
	default:
		usage();
	}ARGEND;
	if(argc != 1)
		usage();

	tfname = argv[0];
	ttfname = smprint("%s.new", tfname);
	b = Bopen(tfname, OREAD);
	if(b == nil)
		sysfatal("%s: %r", tfname);
	trie = rdtrie(b);
	Bterm(b);
	if(trie == nil)
		sysfatal("%s: %r", tfname);

	if(srv == nil && mnt == nil){
		mnt = "/mnt/tags";
		srv = srvname(tfname);
	}
	if(!chatty9p)
		rfork(RFNOTEG);
	sfs.tree =  alloctree(nil, nil, DMDIR|0777, nil);
	user = getuser();
	ctlf = createfile(sfs.tree->root, "ctl", user, 0666, nil);
	threadpostmountsrv(&sfs, srv, mnt, mflag);
	threadexits(nil);
}
