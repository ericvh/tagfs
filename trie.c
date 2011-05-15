#include <u.h>
#include <libc.h>
#include <bio.h>
#include "util.h"
#include "trie.h"

/* Trie supporting search from word to
 * list of uvlong (Qid.paths for files)
 * values (uvlongs) can be removed, but keys
 * are never removed.
 */

long ntries;
long maxvals;
long nvaltries;

int	warntries = 1;

Trie	roott;	// profiling. Used entries at the root node.

Trie*	
alloctrie(void)
{
	Trie*	t;

	t = emallocz(sizeof(*t), 1);
	ntries++;
	if(warntries && (ntries%50000) == 0)
		fprint(2, "alloctrie: > %ld prefixes\n", ntries);
	return t;
}

void	
freetrie(Trie* t)
{
	int	i;

	if(t != nil){
		ntries--;
		free(t->vals);
		free(t->svals);
		for(i = 0; i < t->nents; i++)
			freetrie(t->ents[i].t);
		free(t->ents);
		free(t);
	}
}

static int
getkey(Trie* t, Rune k)
{
	int	h,l,m;

	h = t->nents;
	l = 0;
	k = tolowerrune(k);
	for(;;){
		if(l >= h)
			return -1;
		m = l + (h - l)/2;
		assert(m >= l && m < h);
		if(t->ents[m].r == k)
			return m;
		if(t->ents[m].r < k)
			l = m + 1;
		else
			h = m;
	}
}

static int
ecmp(const void* r1, const void* r2)
{
	const Tent* e1 = r1;
	const Tent* e2 = r2;

	return e1->r - e2->r;
}

/* called with root as t
 * to profile used entries at the root node.
 */
static int
putkey(Trie* t, Rune k)
{
	k = tolowerrune(k);
	if(t->nents == t->aents){
		t->aents += Incr;
		t->ents = erealloc(t->ents, t->aents*sizeof(Tent));
	}
	t->ents[t->nents].r = k;
	if(t != &roott)
		t->ents[t->nents].t = alloctrie();
	t->nents++;
	qsort(t->ents, t->nents, sizeof(Tent), ecmp);
	return getkey(t, k);
}

static int
putsval(Trie* t, long v)
{
	int	i;

	for(i = 0; i < t->nsvals; i++)
		if(t->svals[i] == v)
			return 0;
	if((t->nsvals%(2*Incr)) == 0){
		t->svals = erealloc(t->svals, (t->nsvals+2*Incr)*sizeof(ulong));
	}
	t->svals[t->nsvals++] = v;
	if(t->nsvals + t->nvals > maxvals)
		maxvals = t->nsvals + t->nvals;
	return 1;
}

static int
putval(Trie* t, vlong v)
{
	int	i;
	long	sv;

	if(t->nvals == 0)
		nvaltries++;
	sv = v;
	if(v == (vlong)sv)		// many qids fit in a long
		return putsval(t, sv);	// use long for them.
	for(i = 0; i < t->nvals; i++)
		if(t->vals[i] == v)
			return 0;
	if((t->nvals%(2*Incr)) == 0)
		t->vals = erealloc(t->vals, (t->nvals+2*Incr)*sizeof(uvlong));
	t->vals[t->nvals++] = v;
	if(t->nsvals + t->nvals > maxvals)
		maxvals = t->nsvals + t->nvals;
	return 1;
}

void	
trieput(Trie* t, char* k, vlong v)
{
	int	ti;
	Rune	r;
	int	nc;
	char*	uk;
	int	newv;

	uk = k;
	if(*k != 0){
		chartorune(&r, k);
		if(getkey(&roott, r) < 0)
			putkey(&roott, r);
	}
	for(;;){
		if(*k == 0)
			break;
		nc = chartorune(&r, k);
		ti = getkey(t, r);
		if(ti < 0)
			ti = putkey(t, r);
		t = t->ents[ti].t;
		k += nc;
	}
	newv = putval(t, v);
	if(newv && ((t->nvals+t->nsvals)%1000) == 0)
		fprint(2, "trieput: tag %s: > %d files\n", uk, t->nvals+t->nsvals);
}

Trie*	
trieget(Trie* t, char* k)
{
	int	ti;
	Rune	r;
	int	nc;

	if(*k != 0){
		chartorune(&r, k);
		if(getkey(&roott, r) < 0)
			putkey(&roott, r);
	}
	for(;;){
		if(*k == 0)
			return t;
		nc = chartorune(&r, k);
		ti = getkey(t, r);
		if(ti < 0)
			return nil;
		t = t->ents[ti].t;
		k += nc;
	}
}

static char*
rdline(Biobuf* b, int* lno, char* tag)
{
	char*	s;

	if(*lno == 0)
		*lno = 1;
	s = Brdstr(b, '\n', 1);
	if(s == nil)
		werrstr("rdtrie: off %lld line %d: %s expected",
			Boffset(b), *lno, tag);
	else
		(*lno)++;
	return s;
}

static Trie*	
_rdtrie(Biobuf* b, int* lno)
{
	char*	ln;
	Trie*	t;
	uvlong	v;
	char*	s;
	char*	n;
	int	nents;
	int	i;

	t = alloctrie();
	ln = rdline(b, lno, "values");
	if(ln == nil)
		goto fail;
	for(s = ln; *s != 0; s = n){
		v = strtoull(s, &n, 16);
		if (v == 0LL)
			break;
		putval(t, v);
	}
	free(ln);
	ln = rdline(b, lno, "nents");
	if(ln == nil)
		goto fail;
	nents = strtol(ln, nil, 10);
	free(ln);
	if(nents < 0 || nents > 100000){
		fprint(2, "rdtree: bad entry: %d ents: aborting\n", nents);
		goto fail;
	}
	t->ents = emallocz(nents*sizeof(Tent), 1);
	t->aents = nents;
	for(i = 0; i < nents; i++){
		ln = rdline(b, lno, "rune");
		if(ln == nil)
			goto fail;
		chartorune(&(t->ents[i].r), ln);
		free(ln);
		t->ents[i].t = _rdtrie(b, lno);
		if(t->ents[i].t == nil)
			goto fail;
		t->nents++;
	}
	return t;
fail:
	freetrie(t);
	return nil;
}

Trie*
rdtrie(Biobuf* b)
{
	int	lno;
	Trie*	t;
	lno = 0;
	// do not warn about so many tries created while reading
	// a database. Its creator already noticed.
	warntries = 0;
	t = _rdtrie(b, &lno);
	warntries = 1;
	return t;
}

int
wrtrie(Biobuf* b, Trie* t)
{
	int	i;

	if(t->nvals > 0 && Bprint(b, "%llux", t->vals[0]) < 0)
		return -1;
	for(i = 1; i < t->nsvals; i++)
		if(Bprint(b, " %lux", t->svals[i]) < 0)
			return -1;
	for(i = 1; i < t->nvals; i++)
		if(Bprint(b, " %llux", t->vals[i]) < 0)
			return -1;
	if(Bprint(b, "\n%d\n", t->nents) < 0)
		return -1;
	for(i = 0; i < t->nents; i++){
		if(Bprint(b, "%C\n", t->ents[i].r) < 0)
			return -1;
		if(wrtrie(b, t->ents[i].t) < 0)
			return -1;
	}
	return 0;
}

static void	
_printtrie(Biobuf* b, Trie* t, char* pref)
{
	int	i;
	char*	s;
	int	nc;
	int	l;

	Bprint(b, "prefix '%s':", pref);
	for(i = 0; i < t->nsvals; i++)
		Bprint(b, " %lux", t->svals[i]);
	for(i = 0; i < t->nvals; i++)
		Bprint(b, " %llux", t->vals[i]);
	Bprint(b, " (%d ents)\n", t->nents);
	l = strlen(pref);
	s = emallocz(l+UTFmax+1, 0);
	strcpy(s, pref);
	for(i = 0; i < t->nents; i++){
		nc = runetochar(s+l, &t->ents[i].r);
		s[l+nc] = 0;
		_printtrie(b, t->ents[i].t, s);
	}
	free(s);
}

void
printtrie(Biobuf* b, Trie* t)
{
	_printtrie(b, t, "");
}
