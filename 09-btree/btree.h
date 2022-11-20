typedef struct node
{
	unsigned int n; // between L-1 and 2L-1
	int *keys; // of size n
	struct node **ptr; // of size n+1
} node;

struct btree
{
	unsigned int L;
	node *root;
};

typedef struct btree_iter
{
	node* cur_node;
	struct btree_iter* parent_iter;
	unsigned int pos;
} btree_iter;