#include <u.h>
#include <libc.h>
#include <bio.h>
#include "util.h"


enum {
	Incr = 8,		// we grow arrays with this incr.
	Nhash = 512 * 1024,	// # of buckets
};

typedef struct Ent Ent;

struct Ent {
	uvlong	qid;
	char*	path;
	Ent*	next;	// in hash
};

Ent*	hash[Nhash];
int	debug;

int
qhash(uvlong q)
{
	uvlong max;

	max = Nhash;
	return (int) (q%max);
}

/*
 * Disk format:
 *	Nhash lines with "<qid.path> <file name>\n"
 */
void
rdhash(char* hfname, int mkit)
{
	Biobuf*	b;
	char*	ln;
	Ent*	e;
	char*	s;
	int	h;

	b = Bopen(hfname, OREAD);
	if(b == nil){
		if(!mkit)
			sysfatal("%s: %r", hfname);
		return;
	}
	while(ln = Brdstr(b, '\n', 1)){
		e = emallocz(sizeof(Ent), 0);
		e->qid = strtoull(ln, &s, 16);
		e->path = estrdup(s+1);
		h = qhash(e->qid);
		e->next = hash[h];
		hash[h] = e;
		free(ln);
	}
	Bterm(b);
}

void
wrhash(char* hfname)
{
	Biobuf*	b;
	int	i;
	Ent*	e;

	b = Bopen(hfname, OWRITE);
	if(b == nil)
		sysfatal("%s: %r", hfname);
	for(i = 0; i < Nhash; i++)
		for(e = hash[i]; e != nil; e = e->next)
			Bprint(b, "%llx %s\n", e->qid, e->path);
	Bterm(b);
}

char*
lookup(uvlong qid)
{
	int	h;
	Ent*	e;

	h = qhash(qid);
	for(e = hash[h]; e != nil; e = e->next)
		if(e->qid == qid)
			return e->path;
	return nil;
}

void
insert(uvlong qid, char* path)
{
	int	h;
	Ent*	e;

	h = qhash(qid);
	for(e = hash[h]; e != nil; e = e->next)
		if(e->qid == qid){
			free(e->path);
			e->path = estrdup(path);
			if(debug>1)
				fprint(2, "insert 0x%llx\t%s\n", qid, path);
			return;
		}
	if(debug)
		fprint(2, "insert 0x%llx\t%s\n", qid, path);
	e = emallocz(sizeof(Ent), 0);
	e->qid = qid;
	e->path = estrdup(path);
	e->next = hash[h];
	hash[h] = e;
}

void
addtree(char* fname, Dir* d)
{
	Dir*	ad;
	Dir*	dd;
	int	nd;
	int	fd;
	int	i;
	char*	nfname;

	ad = nil;
	if(d == nil){
		ad = d = dirstat(fname);
		if(d == nil)
			goto fail;
	}
	insert(d->qid.path, fname);
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
			addtree(nfname, &dd[i]);
			free(nfname);
		}
		free(dd);
	}
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
	fprint(2, "usage:\n\t%s [-dv] hash [qid...]\n", argv0);
	fprint(2, "\t%s [-dv] -a hash [qid path...]\n", argv0);
	fprint(2, "\t%s [-dv] -c hash file...\n", argv0);
	exits("usage");
}

void
main(int argc, char* argv[])
{
	int	verbose;
	char*	hfname;
	uvlong	qid;
	int	flaga, flagc;
	char*	path;

	/* Experiment: we do not del file entries
 	 * Qids are assumed unique and disk space is cheap
	 * flagd is known to be false.
 	 */
	flaga =  flagc = verbose = 0;
	ARGBEGIN{
	case 'd':
		debug++; // twice
	case 'v':
		verbose++;
		break;
	case 'a':
		flaga++;
		break;
	case 'c':
		flagc++;
		break;
	default:
		usage();
	}ARGEND;
	if(argc < 1)
		usage();
	hfname = argv[0];
	argv++; argc--;

	if(!flaga && !flagc){
		// search qids, print paths
		if(argc == 0)
			sysfatal("qid args expected");
		rdhash(hfname, 0);
		do{
			qid = strtoull(argv[0], nil, 16);
			path = lookup(qid);
			if(path && access(path, AEXIST) == 0)
				print("%s\n", path);
			else if(verbose)
				print("%s: not found\n", argv[0]);
			argc--; argv++;
		}while(argc > 0);
		exits(nil);
	}
	if(flaga){
		// add qid/paths from args
		if(argc == 0 || (argc%2) != 0)
			sysfatal("qid path arg pairs expected");
		rdhash(hfname, 1);
		do{
			qid = strtoull(argv[0], nil, 16);
			insert(qid, argv[1]);
			argc--; argv++;
			argc--; argv++;
		}while(argc > 0);
		wrhash(hfname);
		exits(nil);
	}
	if(flagc){
		// add entries for files in args
		if(argc == 0)
			sysfatal("file names expected");
		rdhash(hfname, 1);
		do{
			path = cleanpath(argv[0]);
			addtree(path, nil);
			free(path);
			argc--; argv++;
		}while(argc > 0);
		wrhash(hfname);
		exits(nil);
	}
	sysfatal("bug: invocation syntax too convoluted");
}

