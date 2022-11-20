#include <solution.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void display(node *print_node, unsigned int blanks) {
	if (print_node) {
		unsigned int i;
		for (i = 1; i <= blanks; i++)
			printf(" ");
		for (i = 0; i < print_node->n; i++)
			printf("%d ", print_node->keys[i]);
		printf("\n");
		for (i = 0; i <= print_node->n; i++)
			display(print_node->ptr[i], blanks + 10);
	}
}

int construct_node(node *new_node, unsigned int L) {
	new_node->n = 0;
	new_node->keys = (int*)malloc((2*L) * sizeof(int));
	new_node->ptr = (node**)calloc((2*L), sizeof(node*));
	//memset(new_node->ptr, (void*)NULL, (2*L-1) * sizeof(node*));

	return 0;
}

int delete_node(node *old_node) {
	free(old_node->keys);
	if( (old_node->n != 0) && (old_node->ptr[0] != 0) ) {
		for(unsigned int i = 0; i < old_node->n + 1; i ++) {
			delete_node(old_node->ptr[i]);
		}
	}
	free(old_node->ptr);
	free(old_node);
	return 0;
}

struct btree* btree_alloc(unsigned int L)
{
	struct btree *new_btree = (struct btree*)malloc(sizeof(struct btree));
	new_btree->root = (node*)malloc(sizeof(struct node));
	construct_node(new_btree->root, L);
	new_btree->L = L;

	return new_btree;
}

void btree_free(struct btree *t)
{
	delete_node(t->root);
	free(t);
}

bool btree_contains_node(node *some_node, int x)
{
	if ( some_node ) {
		if ( some_node->n == 0 ) return false; // empty node
		for( unsigned int i = 0; i < some_node->n; i++ ) {
			if( x < some_node->keys[i] ) {
				if ( btree_contains_node(some_node->ptr[i], x) ) return true;
			} else if ( x == some_node->keys[i] ) return true;
		}
		if( btree_contains_node(some_node->ptr[some_node->n], x) ) return true;
	}

	return false;
}

bool btree_contains(struct btree *t, int x)
{
	return btree_contains_node(t->root, x);
}

void insert_value_to_not_exceeded_node(node *some_node, int key, node *ptr) {
	unsigned int pos = some_node->n;
	for( unsigned int i = 0; i < some_node->n; i++ ) {
		//if(key == (int)some_node->keys[i]) return;
		if(key < (int)some_node->keys[i]) {
			pos = i;
			break;
		}
	}
	for ( unsigned int i = some_node->n; i > pos; i--) {
		some_node->keys[i] = some_node->keys[i - 1];
		some_node->ptr[i + 1] = some_node->ptr[i];
	}
	some_node->keys[pos] = key;
	//printf("inserting to pos %d == %d \n", pos, some_node->n);
	some_node->ptr[/*pos == some_node->n ?*/ pos + 1 /*: pos*/ ] = ptr;
	++some_node->n;
}

void insert_value_to_exceeded_node(const unsigned int L, node *some_node, int key, int *parent_key, node **new_brother_node)
{
	// allocate, write and return new_brother
	insert_value_to_not_exceeded_node(some_node, key, *new_brother_node); // now size is 2*L
	//some_node->n--;

	unsigned int middle = L - 1;
	*parent_key = some_node->keys[middle]; some_node->n--;

	*new_brother_node = (node*)malloc(sizeof(node)); construct_node(*new_brother_node, L);

	for( unsigned int i = 0; i < L; i ++ ) {
		(*new_brother_node)->keys[i] = some_node->keys[i + middle + 1];
		(*new_brother_node)->ptr[i] = some_node->ptr[i + middle + 1];
		some_node->n--;
		(*new_brother_node)->n++;
	}
	//printf("Left in some_node: %d, in new_brother_node: %d\n", some_node->n, (*new_brother_node)->n);
	//printf("Some_node: %d\n", some_node->keys[middle]);
	(*new_brother_node)->ptr[L - 1] = some_node->ptr[L - 1 + middle + 1];
}

int insert_one_level(const unsigned int L, node *some_node, int key, int *parent_key, node **new_brother_node, bool can_insert_not_to_leaf)
{
	if( some_node->ptr[0] == 0 || can_insert_not_to_leaf ) {
		//printf("inserting %d to a leaf\n", key);
		if( some_node->n < 2 * L - 1 ) { // just insert, new_brother_node = NULL
			insert_value_to_not_exceeded_node(some_node, key, *new_brother_node);
			return 0;
		} else { // size exceeded, new_brother_node = NULL
			insert_value_to_exceeded_node(L, some_node, key, parent_key, new_brother_node);
			return 1; // insert and continue inserting on a higher level
		}
	} else {
		for( unsigned int i = 0; i < some_node->n; i++ ) {
			if( key < some_node->keys[i] ) {
				//printf("key %d is less then %d\n", key, some_node->keys[i]);
				switch( insert_one_level(L, some_node->ptr[i], key, parent_key, new_brother_node, false) ) {
					case 0:
						return 0;
					case 1: // value was inserted lower, continue now, new_brother_node != NULL
						return insert_one_level(L, some_node, *parent_key, parent_key, new_brother_node, true);
					default:
						printf("Error!\n");
						break;
				}
			}
		}
		//printf("inserting %d not to a leaf to the end\n", key);
		// key > som_inode->keys[some_inode->n - 1]
		switch( insert_one_level(L, some_node->ptr[some_node->n], key, parent_key, new_brother_node, false) ) {
			case 0:
				//printf("0\n");
				return 0;
			case 1: // value was inserted lower, continue now, new_brother_node != NULL
				//printf("1, Some_node with: %d\n", some_node->keys[0]);
				return insert_one_level(L, some_node, *parent_key, parent_key, new_brother_node, true);
			default:
				printf("Error!\n");
				break;
		}
	}
	return -1;
}

void btree_insert(struct btree *t, int x)
{
	if ( btree_contains(t, x) ) return;
	node **new_brother_node = (node**)calloc(1, sizeof(node*));
	node *new_root, *old_root;
	int parent_key = 0; // initial value doesn't matter
	switch( insert_one_level(t->L, t->root, x, &parent_key, new_brother_node, false) ) {
		case 0:
			break;
		case 1:
			new_root = (node*)malloc(sizeof(node)); construct_node(new_root, t->L);
			old_root = t->root;
			new_root->n++;
			new_root->keys[0] = parent_key;
			new_root->ptr[0] = old_root;
			new_root->ptr[1] = *new_brother_node;
			t->root = new_root;
			break;
		default:
			printf("Error!\n");
	}
	free(new_brother_node);
	return;
}

void merge(node *left_child, node *right_child, node *parent, int move_pos)
{
	printf("1 start with: %d, 2 start with %d\n", left_child->keys[0], right_child->keys[0]);
	int move_key_parent = parent->keys[move_pos];
	for( unsigned int i = move_pos + 1; i < parent->n; i++ ) {
		parent->keys[i-1] = parent->keys[i];
		parent->ptr[i-1] = parent->ptr[i];
	}
	parent->ptr[parent->n-1] = parent->ptr[parent->n];
	parent->n--;
	parent->ptr[move_pos] = left_child;
	printf("merge %d = %d", left_child->keys[left_child->n-1], move_key_parent);
	left_child->keys[left_child->n] = move_key_parent;
	left_child->n++;
	int left_start = left_child->n;
	for( unsigned int i = 0; i < right_child->n; i ++) {
		left_child->keys[left_start + i] = right_child->keys[i];
		left_child->ptr[left_start + i] = right_child->ptr[i];
		left_child->n++;
	}
	left_child->ptr[left_start + right_child->n] = right_child->ptr[right_child->n];
	right_child->n = 0;
	delete_node(right_child);
}

void move(node *left_child, node *right_child, node *parent, int move_pos)
{
	printf("1 start with: %d, 2 start with %d\n", left_child->keys[0], right_child->keys[0]);
	
	int move_key_child = right_child->keys[0];
	node *move_ptr_child = right_child->ptr[0];

	for( unsigned int i = 1; i < right_child->n; i++ ) {
		right_child->keys[i-1] = right_child->keys[i];
		right_child->ptr[i-1] = right_child->ptr[i];
	}
	right_child->n--;

	int move_key_parent = parent->keys[move_pos];
	parent->keys[move_pos] = move_key_child;

	left_child->keys[left_child->n] = move_key_parent;
	left_child->n++;
	right_child->ptr[left_child->n] = move_ptr_child;
}

void delete_value_from_leaf(node *some_node, int key)
{
	unsigned int pos = 0;
	for( unsigned int i = 0; i < some_node->n; i++ ) {
		if(key == some_node->keys[i]) {
			pos = i;
			break;
		}
	}
	printf("deleting %d\n", pos);
	for ( unsigned int i = pos; i < some_node->n - 1; i++ ) {
		some_node->keys[i] = some_node->keys[i + 1];
	}
	some_node->n--;
}

int delete_one_level(unsigned int L, node *some_node, int key)
{
	if( some_node->ptr[0] == 0 ) {
		delete_value_from_leaf(some_node, key);
	} else {
		printf("delete_one_level not leaf %d\n", key);
		for( unsigned int i = 0; i < some_node->n; i++ ) {
			if( key < some_node->keys[i] ) {
				printf(" %d < %d \n", key, some_node->keys[i]);
				if( some_node->ptr[i]->n == L - 1 && some_node->ptr[i+1]->n == L - 1 ) {
					printf(" merge ");
					merge(some_node->ptr[i], some_node->ptr[i+1], some_node, i);
				} else if( some_node->ptr[i]->n == L - 1 && some_node->ptr[i+1]->n > L - 1 ) {
					printf(" move ");
					move(some_node->ptr[i], some_node->ptr[i+1], some_node, i);
				}
				// moved or merged, now can delete
				if( some_node->ptr[i]->n > L - 1 ) {
					return delete_one_level(L, some_node->ptr[i], key);
				} else {
					printf("Error\n");
				}
			} else if( (i == some_node->n - 1) && (key > some_node->keys[i]) ) {
				if( some_node->ptr[some_node->n]->n > L - 1 ) {
					return delete_one_level(L, some_node->ptr[some_node->n], key);
				} else {
					printf(" %d > %d \n", key, some_node->keys[i]);
					if( some_node->ptr[i]->n == L - 1 && some_node->ptr[i+1]->n == L - 1 ) {
						printf(" merge ");
						merge(some_node->ptr[i], some_node->ptr[i+1], some_node, i);
					} else if( some_node->ptr[i]->n == L - 1 && some_node->ptr[i+1]->n > L - 1 ) {
						printf(" move, %d ", some_node->ptr[i]->n);
						move(some_node->ptr[i], some_node->ptr[i+1], some_node, i);
					}
					// moved or merged, now can delete
					if( some_node->ptr[i]->n > L - 1 ) {
						return delete_one_level(L, some_node->ptr[i], key);
					} else {
						printf("Error\n");
					}
				}
			} else if( key == some_node->keys[i] ) {
				if( some_node->ptr[i]->n == L - 1 && some_node->ptr[i + 1]->n == L - 1 ) {
					node *merged = some_node->ptr[i];
					merge(some_node->ptr[i], some_node->ptr[i+1], some_node, i);
					return delete_one_level(L, merged, key);
				} else if ( some_node->ptr[i]->n > L - 1 ){
					some_node->keys[i] = some_node->ptr[i]->keys[some_node->ptr[i]->n];
					return delete_one_level(L, some_node->ptr[i], some_node->keys[i]);
				} else if ( some_node->ptr[i+1]->n > L - 1 ){
					some_node->keys[i] = some_node->ptr[i+1]->keys[0];
					return delete_one_level(L, some_node->ptr[i+1], some_node->keys[i]);
				}
			}
		}
	}
	return 0;
}

void btree_delete(struct btree *t, int x)
{
	if ( !btree_contains(t, x) ) return;
	delete_one_level(t->L, t->root, x);

	if( (t->root->n == 0) && t->root->ptr[0] ){
		node *new_root;
		new_root = t->root->ptr[0];
		free(t->root->keys);
		free(t->root->ptr);
		free(t->root);
		t->root = new_root;
	}

	return;
}

typedef struct btree_iter
{
	node* cur_node;
	struct btree_iter* parent_iter;
	unsigned int pos;
} btree_iter;

struct btree_iter* btree_iter_start(struct btree *t)
{
	node *most_left = t->root;
	btree_iter *old_iter = NULL, *new_iter;
	while(most_left->ptr[0]) {
		new_iter = (btree_iter*)malloc(sizeof(btree_iter));
		new_iter->cur_node = most_left;
		new_iter->parent_iter = old_iter;
		new_iter->pos = 0;
	
		most_left = most_left->ptr[0];
		old_iter = new_iter;
	}
	return old_iter;
}

void btree_iter_end(struct btree_iter *i)
{
	btree_iter *old_iter;
	while(i->parent_iter) {
		old_iter = i;
		i = i->parent_iter;
		free(old_iter);
	}
}

bool btree_iter_next(struct btree_iter *i, int *x)
{
	if(i->cur_node->ptr[i->pos + 1]) {
		btree_iter *new_iter = (btree_iter*)malloc(sizeof(btree_iter));
		new_iter->cur_node = i->cur_node->ptr[i->pos + 1];
		new_iter->parent_iter = i;
		new_iter->pos = 0;
		i = new_iter;
		*x = i->cur_node->keys[i->pos];
		return true;
	} else if (i->pos < i->cur_node->n) {
		i->pos++;
		*x = i->cur_node->keys[i->pos];
		return true;
	} else if ( i->parent_iter ) {
		btree_iter *old_iter = i;
		i = i->parent_iter;
		free(old_iter);
	}

	return false;
}
