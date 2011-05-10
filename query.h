enum {
	/* operators for tag expressions
	 */
	Ttag,
	Tand,
	Tor,
};

typedef struct Vals Vals;
typedef struct Texpr Texpr;

struct Vals {
	uvlong*	v;
	int	nv;
	int	av;	// allocated vs
};

struct Texpr {
	int	op;
	int	arity;
	Vals*	rval;	// result value
	union {
		char*	tag;	// Ttag 
		Texpr**	tagls;	// Tand, Tor
	};
};

void		printexpr(Texpr* e);
void		printexprval(Texpr* e);
char*		smprintexprval(Texpr* e);
void		evalexpr(Trie* t, Texpr* e);
void		freeexpr(Texpr* e);
Texpr*		parseexpr(int ntoks, char* toks[], int* pos);

