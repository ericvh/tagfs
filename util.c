#include <u.h>
#include <libc.h>
#include "util.h"

int debug;

char*
estrdup(char* s)
{
	s = strdup(s);
	if(s == nil)
		sysfatal("estrdup: not enough memory");
	setmalloctag(s, getcallerpc(&s));
	return s;
}

void*
emallocz(int sz, int zero)
{
	void*	s;

	s = malloc(sz);
	if(s == nil)
		sysfatal("emalloc: not enough memory");
	setmalloctag(s, getcallerpc(&sz));
	if(zero)
		memset(s, 0, sz);
	return s;
}

void*
erealloc(void* p, int sz)
{

	p = realloc(p, sz);
	if(p == nil)
		sysfatal("erealloc: not enough memory");
	else
		setmalloctag(p, getcallerpc(&p));
	return p;
}

char*
cleanpath(char* file)
{
	char*	s;
	char*	t;
	char	cwd[512];

	assert(file && file[0]);
	if(file[1])
		file = strdup(file);
	else {
		s = file;
		file = malloc(3);
		file[0] = s[0];
		file[1] = 0;
		file[2] = 0;
	}
	s = cleanname(file);
	if(s[0] != '/'){
		getwd(cwd, sizeof(cwd));
		t = smprint("%s/%s", cwd, s);
		free(s);
		cleanname(t);
		return t;
	} else
		return s;
}

int
rename(char* to, char* frompath)
{
	Dir	d;
	char*	p;

	remove(to);
	p = strrchr(to, '/');
	if(p != nil)
		to = p + 1;
	nulldir(&d);
	d.name = to;
	if(dirwstat(frompath, &d) < 0)
		return -1;
	return 0;
}
