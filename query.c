#include <u.h>
#include <libc.h>
#include <bio.h>
#include "util.h"
#include "trie.h"
#include "query.h"

void
printexpr(Texpr* e)
{
	int	i;

	switch(e->op){
	case Ttag:
		print("%s", e->tag);
		break;
	case Tand:
	case Tor:
		printexpr(e->tagls[0]);
		for(i = 1; i < e->arity; i++){
			if(e->op == Tand)
				print(" ");
			else
				print(" : ");
			printexpr(e->tagls[i]);
		}
		break;
	default:
		sysfatal("bad op");
	}
}

void
printexprval(Texpr* e)
{
	int	i;

	if(e->rval->nv != 0){
		print("%llx", e->rval->v[0]);
		for(i = 1; i < e->rval->nv; i++)
			print(" %llx", e->rval->v[i]);
		print("\n");
	}
}

char*
smprintexprval(Texpr* e)
{
	int	i;
	static	char buf[64*1024];
	char*	s;

	s = buf;
	*s = 0;
	if(e->rval->nv != 0){
		s = seprint(s, buf+sizeof(buf), "%llx", e->rval->v[0]);
		for(i = 1; i < e->rval->nv; i++)
			s = seprint(s, buf+sizeof(buf), " %llx", e->rval->v[i]);
		seprint(s, buf+sizeof(buf), "\n");
	}
	return estrdup(buf);
}

static void
addtagl(Texpr* e, Texpr* tl)
{
	assert(e->op == Tor || e->op == Tand);
	if((e->arity%Incr) == 0)
		e->tagls = erealloc(e->tagls, (e->arity+Incr)*sizeof(Texpr));
	e->tagls[e->arity++] = tl;
}

static Texpr*
parsetag(int ntoks, char* toks[], int* pos)
{
	Texpr*	e;

	if(*pos == ntoks)
		return nil;
	if(strcmp(toks[*pos], ":") == 0)
		sysfatal("tag expected");
	e = emallocz(sizeof(Texpr), 1);
	e->op = Ttag;
	e->tag = estrdup(toks[*pos]);
	(*pos)++;
	return e;
}

static Texpr*
parsetagl(int ntoks, char* toks[], int* pos)
{
	Texpr*	re;
	Texpr*	e;

	if(*pos == ntoks)
		return nil;
	re = emallocz(sizeof(Texpr), 1);
	re->op = Tand;
	if(strcmp(toks[*pos], ":") == 0)
		sysfatal("tag expected");
	do {
		e = parsetag(ntoks, toks, pos);
		addtagl(re, e);
	} while(*pos < ntoks && strcmp(toks[*pos],":") != 0);
	return re;
}

Texpr*
parseexpr(int ntoks, char* toks[], int* pos)
{
	Texpr*	e;
	Texpr*	re;

	e = parsetagl(ntoks, toks, pos);
	if(e == nil)
		return nil;
	if(*pos == ntoks)
		return e;
	re = emallocz(sizeof(Texpr), 1);
	re->op = Tor;
	addtagl(re, e);
	while(*pos < ntoks){
		if(strcmp(toks[*pos], ":") != 0)
			sysfatal("':' expected");
		(*pos)++;
		e = parsetagl(ntoks, toks, pos);
		if(e == nil)
			sysfatal("tag list expected");
		addtagl(re, e);
	}
	return re;
}

static Vals*
newvals(void)
{
	Vals*	vals;

	vals = emallocz(sizeof(*vals), 1);
	return vals;
}

static Vals*
dupvals(Vals* vals)
{
	Vals*	nv;

	nv = newvals();
	*nv = *vals;
	nv->v = emallocz(nv->av*sizeof(uvlong), 0);
	memmove(nv->v, vals->v, nv->nv*sizeof(uvlong));
	return nv;
}

static void
addvals(Vals* vals, Trie* t)
{
	int	i;

	free(vals->v);
	vals->av = t->nvals+t->nsvals+Incr;
	vals->nv = t->nvals+t->nsvals;
	vals->v = emallocz(vals->av*sizeof(uvlong), 0);
	if(t->nvals)
		memmove(vals->v, t->vals, t->nvals*sizeof(uvlong));
	for(i = 0; i < t->nsvals; i++)
		vals->v[t->nvals+i] = (uvlong)t->svals[i];
}

static void
freevals(Vals* vals)
{
	if(vals){
		free(vals->v);
		free(vals);
	}
}

static int
hasval(Vals* vals, uvlong v)
{
	int	i;

	for(i = 0; i < vals->nv; i++)
		if(vals->v[i] == v)
			break;
	return i < vals->nv;
}

static void
addval(Vals* vals, uvlong v)
{
	if(hasval(vals, v))
		return;
	if(vals->nv == vals->av){
		vals->av += Incr;
		vals->v = erealloc(vals->v, vals->av*sizeof(uvlong));
	}
	vals->v[vals->nv++] = v;
}

static void
delval(Vals* vals, uvlong v)
{
	int	i;
	int	nv;

	nv = vals->nv;
	for(i = 0; i < nv; i++)
		if(vals->v[i] == v){
			vals->v[i] = vals->v[nv-1];
			vals->nv--;
			break;
		}
}

void
evalexpr(Trie* t, Texpr* e)
{
	int	i, j;
	Trie*	tt;
	Texpr*	ie;

	for(i = 0; i < e->arity; i++)
		evalexpr(t, e->tagls[i]);
	switch(e->op){
	case Ttag:
		e->rval = newvals();
		tt = trieget(t, e->tag);
		if(tt != nil)
			addvals(e->rval, tt);
		break;
	case Tand:
		e->rval = dupvals(e->tagls[0]->rval);
		for(i = 1; i < e->arity; i++){
			ie = e->tagls[i];
			for(j = 0; j < e->rval->nv;)
				if(!hasval(ie->rval, e->rval->v[j]))
					delval(e->rval, e->rval->v[j]);
				else
					j++;
		}
		break;
	case Tor:
		e->rval = dupvals(e->tagls[0]->rval);
		for(i = 1; i < e->arity; i++){
			ie = e->tagls[i];
			for(j = 0; j < ie->rval->nv; j++)
				addval(e->rval, ie->rval->v[j]);
		}
		break;
	default:
		sysfatal("evalexpr: bad op %d", e->op);
	}
}

void
freeexpr(Texpr* e)
{
	int	i;

	if(e == nil)
		return;
	switch(e->op){
	case Ttag:
		free(e->tag);
		break;
	case Tand:
	case Tor:
		for(i = 1; i < e->arity; i++)
			freeexpr(e->tagls[i]);
		break;
	default:
		sysfatal("bad op");
	}
	freevals(e->rval);
	free(e);
}

