// How many ways can you tile a chessboard with 1-, 2- and 3-polyonominos?
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <gmp.h>
#include "cbt.h"
#include "darray.h"

struct node_s {
  uint16_t v;
  uint32_t lo, hi;
};
typedef struct node_s *node_ptr;
typedef struct node_s node_t[1];

node_t pool[1<<24];
uint32_t freenode, POOL_MAX = (1<<24) - 1;
mpz_ptr count[1<<24];
uint32_t vmax;

void set_node(uint32_t n, uint16_t v, uint32_t lo, uint32_t hi) {
  pool[n]->v = v;
  pool[n]->lo = lo;
  pool[n]->hi = hi;
}

void pool_swap(uint32_t x, uint32_t y) {
  struct node_s tmp = *pool[y];
  *pool[y] = *pool[x];
  *pool[x] = tmp;
  for(uint32_t i = 2; i < freenode; i++) {
    if (pool[i]->lo == x) pool[i]->lo = y;
    else if (pool[i]->lo == y) pool[i]->lo = x;
    if (pool[i]->hi == x) pool[i]->hi = y;
    else if (pool[i]->hi == y) pool[i]->hi = y;
  }
}

// Count elements in ZDD rooted at node n.
void get_count(uint32_t n) {
  if (count[n]) return;
  count[n] = malloc(sizeof(mpz_t));
  mpz_init(count[n]);
  if (n <= 1) {
    mpz_set_ui(count[n], n);
    return;
  }
  uint32_t x = pool[n]->lo;
  uint32_t y = pool[n]->hi;
  if (!count[x]) get_count(x);
  if (!count[y]) get_count(y);
  mpz_add(count[n], count[x], count[y]);
  gmp_printf("%d: %Zd\n", n, count[n]);
}

// Construct ZDD of sets containing exactly 1 of the elements in the given list.
void contains_exactly_one(darray_t a) {
  int n = freenode;
  int v = 1;
  int i = 0;
  while(v <= vmax) {
    if (i >= darray_count(a)) {
      set_node(n, v, n + 1, n + 1);
      n++;
    } else if (v == (int) darray_at(a, i)) {
      // Find length of consecutive sequence.
      int k;
      for(k = 0;
	  i + k < darray_count(a) && v + k == (int) darray_at(a, i + k); k++);
      uint32_t h = v + k > vmax ? 1 : n + k + (darray_count(a) != i + k);
      if (i >= 1) {
	// In the middle of the list: must fix previous node too.
	set_node(n - 1, v - 1, h, h);
      }
      i += k;
      k += v;
      while (v < k) {
	set_node(n, v, n + 1, h);
	v++;
	n++;
      }
      v--;
      if (darray_count(a) == i) {
	set_node(n - 1, v, 0, h);
      }
    } else if (!i) {
      set_node(n, v, n + 1, n + 1);
      n++;
    } else {
      set_node(n, v, n + 2, n + 2);
      n++;
      set_node(n, v, n + 2, n + 2);
      n++;
    }
    v++;
  }
  // Fix last node.
  if (pool[n - 1]->lo >= n) pool[n - 1]->lo = 1;
  if (pool[n - 1]->hi >= n) pool[n - 1]->hi = 1;
  freenode = n;
}

// Construct ZDD of sets containing all elements in the given list.
// The list is terminated by -1.
void contains_all(int *list) {
  int n = freenode;
  int v = 1;
  int *next = list;
  while (v <= vmax) {
    if (v == *next) {
      next++;
      set_node(n, v, 0, n + 1);
      n++;
    } else {
      set_node(n, v, n + 1, n + 1);
      n++;
    }
    v++;
  }
  if (pool[n - 1]->lo == n) pool[n - 1]->lo = 1;
  if (pool[n - 1]->hi == n) pool[n - 1]->hi = 1;
  freenode = n;
}

void intersect(uint32_t z0, uint32_t z1) {
  struct node_template_s {
    uint16_t v;
    // NULL means this template have been instantiated.
    // Otherwise it points to the LO template.
    cbt_it lo;
    union {
      // Points to HI template when template is not yet instantiated.
      cbt_it hi;
      // During template instantiation we set n to the pool index
      // of the newly created node.
      uint32_t n;
    };
  };
  typedef struct node_template_s *node_template_ptr;
  typedef struct node_template_s node_template_t[1];

  node_template_t top, bot;
  bot->v = 0;
  bot->lo = NULL;
  bot->n = 0;
  top->v = 1;
  top->lo = NULL;
  top->n = 1;

  // Naive implementation with two tries. One stores templates, the other
  // unique nodes. Knuth describes how to meld using just memory allocated for
  // a pool of nodes. Briefly, handle duplicates by executing bucket sort level
  // by level, from the bottom up.
  cbt_t tab;
  cbt_init(tab);

  cbt_t node_tab;
  cbt_init(node_tab);

  cbt_it insert_template(uint32_t k0, uint32_t k1) {
    uint32_t key[2];
    // Taking advantage of symmetry of intersection appears to help a tiny bit.
    if (k0 < k1) {
      key[0] = k0;
      key[1] = k1;
    } else {
      key[0] = k1;
      key[1] = k0;
    }
    cbt_it it;
    int just_created = cbt_it_insert_u(&it, tab, (void *) key, 8);
    if (!just_created) return it;
    if (!k0 || !k1) {
      cbt_it_put(it, bot);
      return it;
    }
    if (k0 == 1 && k1 == 1) {
      cbt_it_put(it, top);
      return it;
    }
    node_ptr n0 = pool[k0];
    node_ptr n1 = pool[k1];
    if (n0->v == n1->v) {
      node_template_ptr t = malloc(sizeof(*t));
      t->v = n0->v;
      if (n0->lo == n0->hi && n1->lo == n0->hi) {
	t->lo = t->hi = insert_template(n0->lo, n1->lo);
      } else {
	t->lo = insert_template(n0->lo, n1->lo);
	t->hi = insert_template(n0->hi, n1->hi);
      }
      cbt_it_put(it, t);
      return it;
    } else if (n0->v < n1->v) {
      cbt_it it2 = insert_template(n0->lo, k1);
      cbt_it_put(it, cbt_it_data(it2));
      return it2;
    } else {
      cbt_it it2 = insert_template(k0, n1->lo);
      cbt_it_put(it, cbt_it_data(it2));
      return it2;
    }
  }

  void dump(void* data, const char* key) {
    uint32_t *n = (uint32_t *) key;
    if (!data) {
      printf("NULL %d:%d\n", n[0], n[1]);
      return;
    }
    node_template_ptr t = (node_template_ptr) data;
    if (!t->lo)  {
      printf("%d:%d = (%d)\n", n[0], n[1], t->n);
      return;
    }
    uint32_t *l = (uint32_t *) cbt_it_key(t->lo);
    uint32_t *h = (uint32_t *) cbt_it_key(t->hi);
    printf("%d:%d = %d:%d, %d:%d\n", n[0], n[1], l[0], l[1], h[0], h[1]);
  }

  uint32_t get_node(uint16_t v, uint32_t lo, uint32_t hi) {
    // Create or return existing node representing !v ? lo : hi.
    uint32_t key[3];
    key[0] = lo;
    key[1] = hi;
    key[2] = v;
    cbt_it it;
    int just_created = cbt_it_insert_u(&it, node_tab, (void *) key, 12);
    if (just_created) {
      cbt_it_put(it, (void *) freenode);
      node_ptr n = pool[freenode];
      n->v = v;
      n->lo = lo;
      n->hi = hi;
      if (!(freenode % 100000)) printf("freenode = %d\n", freenode);
      if (POOL_MAX == freenode) {
	fprintf(stderr, "pool is full\n");
	exit(1);
      }
      return freenode++;
    }
    return (uint32_t) cbt_it_data(it);
  }

  uint32_t instantiate(cbt_it it) {
    node_template_ptr t = (node_template_ptr) cbt_it_data(it);
    // Return if already converted to node.
    if (!t->lo) return t->n;
    // Recurse on LO, HI edges.
    uint32_t lo = instantiate(t->lo);
    uint32_t hi = instantiate(t->hi);
    // Remove HI edges pointing to FALSE.
    if (!hi) {
      t->lo = NULL;
      t->n = lo;
      return lo;
    }
    // Convert to node.
    uint32_t r = get_node(t->v, lo, hi);
    t->lo = NULL;
    t->n = r;
    return r;
  }

  insert_template(z0, z1);
  freenode = z0;  // Overwrite input trees.
  //cbt_forall(tab, dump);
  uint32_t key[2];
  key[0] = z0;
  key[1] = z1;
  cbt_it it = cbt_it_at_u(tab, (void *) key, 8);
  uint32_t root = instantiate(it);
  if (root < z0) {
    *pool[z0] = *pool[root];
  } else if (root > z0)  {
    pool_swap(z0, root);
  }
  void clear_it(void* data, const char* key) {
    node_template_ptr t = (node_template_ptr) data;
    uint32_t *k = (uint32_t *) key;
    if (k[0] == k[1] && t != top && t != bot) free(t);
  }
  cbt_forall(tab, clear_it);
  cbt_clear(tab);

  cbt_clear(node_tab);
}

void check_reduced() {
  cbt_t node_tab;
  cbt_init(node_tab);
  for (uint32_t i = 2; i < freenode; i++) {
    cbt_it it;
    uint32_t key[3];
    key[0] = pool[i]->lo;
    key[1] = pool[i]->hi;
    key[2] = pool[i]->v;
    if (!cbt_it_insert_u(&it, node_tab, (void *) key, 12)) {
      printf("duplicate: %d %d\n", i, (int) it->data);
    } else {
      it->data = (void *) i;
    }
  }
  cbt_clear(node_tab);
}

int main() {
  // Initialize TRUE and FALSE nodes.
  pool[0]->v = ~0;
  pool[0]->lo = 0;
  pool[0]->hi = 0;
  pool[1]->v = ~0;
  pool[1]->lo = 1;
  pool[1]->hi = 1;
  freenode = 2;

  int v = 1;
  darray_t board[8][8];
  // Initialize board.
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      darray_init(board[i][j]);
    }
  }
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      // Monominoes.
      darray_append(board[i][j], (void *) v++);
      // Dominoes.
      if (j != 8 - 1) {
	darray_append(board[i][j], (void *) v);
	darray_append(board[i][j + 1], (void *) v++);
      }
      if (i != 8 - 1) {
	darray_append(board[i][j], (void *) v);
	darray_append(board[i + 1][j], (void *) v++);
      }
      // Trionimoes.
      // 3x1 trionimoes.
      if (i < 8 - 2) {
	darray_append(board[i][j], (void *) v);
	darray_append(board[i + 1][j], (void *) v);
	darray_append(board[i + 2][j], (void *) v++);
      }
      if (j < 8 - 2) {
	darray_append(board[i][j], (void *) v);
	darray_append(board[i][j + 1], (void *) v);
	darray_append(board[i][j + 2], (void *) v++);
      }
      // 2x2 - 1 tile trionimoes.
      if (i != 8 - 1 && j != 8 - 1) {
	darray_append(board[i][j], (void *) v);
	darray_append(board[i + 1][j], (void *) v);
	darray_append(board[i][j + 1], (void *) v++);

	darray_append(board[i][j], (void *) v);
	darray_append(board[i + 1][j], (void *) v);
	darray_append(board[i + 1][j + 1], (void *) v++);

	darray_append(board[i][j], (void *) v);
	darray_append(board[i][j + 1], (void *) v);
	darray_append(board[i + 1][j + 1], (void *) v++);

	darray_append(board[i + 1][j], (void *) v);
	darray_append(board[i][j + 1], (void *) v);
	darray_append(board[i + 1][j + 1], (void *) v++);
      }
    }
  }

  vmax = v - 1;

  /*
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      void print_it(void *data) {
	int i = (int) data;
	printf(" %d", i);
      }
      darray_forall(board[i][j], print_it);
      printf("\t\t");
    }
    printf("\n");
  }
  */

  uint32_t k0 = freenode;
  uint32_t ni[8];
  for (int i = 0; i < 8; i++) {
    ni[i] = freenode;
    for (int j = 0; j < 8; j++) {
      uint32_t k1 = freenode;
      contains_exactly_one(board[i][j]);
      if (j) intersect(ni[i], k1);
    }
  }
  for (int i = 6; i >= 0; i--) {
    printf("%d\n", i), fflush(stdout);
    intersect(ni[i], ni[i + 1]);
  }

  for(int i = k0; i < freenode; i++) {
    printf("I%d: !%d ? %d : %d\n", i, pool[i]->v, pool[i]->lo, pool[i]->hi);
  }

  get_count(k0);
  return 0;
}