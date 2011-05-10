
enum {
	Incr = 8	// we grow arrays in k*Incr items
};

typedef struct Trie Trie;
typedef struct Tent Tent;

struct Tent {
	Rune	r;
	Trie*	t;
};

struct Trie {
	Tent*	ents;	// ents[i].r are runes for childs
	int	nents;	// ents[i].t are children
	int	aents;	// # of ents allocated
	uvlong*	vals;	// values for this node prefix
	int	nvals;	// # of values in use
	ulong*	svals;	// small values (fit in a long)
	int	nsvals;	// # of small values in use
};

Trie*	alloctrie(void);
void	trieput(Trie* t, char* k, vlong v);
Trie*	trieget(Trie* t, char* k);
void	freetrie(Trie* t);
Trie*	rdtrie(Biobuf* b);
int	wrtrie(Biobuf* b, Trie* t);
void	printtrie(Biobuf* b, Trie* t);

extern long ntries;
extern long maxvals;
extern long nvaltries;
extern Trie roott;	// profiling. Entries used at the root node.
