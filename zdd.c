// ZDD stack-based calculator library.
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gmp.h>
#include "memo.h"
#include "darray.h"
#include "zdd.h"
#include "io.h"

struct node_s {
  uint16_t v;
  uint32_t lo, hi;
};
typedef struct node_s *node_ptr;
typedef struct node_s node_t[1];

static node_t pool[1<<24];
static uint32_t freenode, POOL_MAX = (1<<24) - 1;
static darray_t stack;
static uint16_t vmax;
static char vmax_is_set;

uint16_t zdd_set_vmax(int i) {
  vmax_is_set = 1;
  return vmax = i;
}

void zdd_push() { darray_append(stack, (void *) freenode); }
void zdd_pop() { darray_remove_last(stack); }

void set_node(uint32_t n, uint16_t v, uint32_t lo, uint32_t hi) {
  pool[n]->v = v;
  pool[n]->lo = lo;
  pool[n]->hi = hi;
}

uint32_t zdd_v(uint32_t n) { return pool[n]->v; }
uint32_t zdd_hi(uint32_t n) { return pool[n]->hi; }
uint32_t zdd_lo(uint32_t n) { return pool[n]->lo; }
uint32_t zdd_set_lo(uint32_t n, uint32_t lo) { return pool[n]->lo = lo; }
uint32_t zdd_set_hi(uint32_t n, uint32_t hi) { return pool[n]->hi = hi; }
uint32_t zdd_set_hilo(uint32_t n, uint32_t hilo) {
  return pool[n]->lo = pool[n]->hi = hilo;
}
uint32_t zdd_next_node() { return freenode; }
uint32_t zdd_last_node() { return freenode - 1; }

static void pool_swap(uint32_t x, uint32_t y) {
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

uint32_t zdd_root() { return (uint32_t) darray_last(stack); }
uint32_t zdd_set_root(uint32_t root) {
  uint32_t i = zdd_root();
  if (i != root) pool_swap(i, root);
  return i;
}

void zdd_count(mpz_ptr z) {
  static mpz_ptr count[1<<24];
  uint32_t root = zdd_root();
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
    if (n == root) {
      mpz_add(z, count[x], count[y]);
    } else {
      mpz_add(count[n], count[x], count[y]);
    }
  }

  get_count(root);

  void clearz(uint32_t n) {
    if (!count[n]) return;
    mpz_clear(count[n]);
    free(count[n]);
    count[n] = NULL;
    if (n <= 1) return;
    clearz(pool[n]->lo);
    clearz(pool[n]->hi);
  }
  clearz(root);
}

uint32_t zdd_abs_node(uint32_t v, uint32_t lo, uint32_t hi) {
  set_node(freenode, v, lo, hi);
  return freenode++;
}

uint32_t zdd_add_node(uint32_t v, int offlo, int offhi) {
  int n = freenode;
  uint32_t adjust(int off) {
    if (!off) return 0;
    if (-1 == off) return 1;
    return n + off;
  }
  set_node(n, v, adjust(offlo), adjust(offhi));
  return freenode++;
}

uint32_t zdd_intersection() {
  if (darray_count(stack) == 0) return 0;
  if (darray_count(stack) == 1) return (uint32_t) darray_last(stack);
  uint32_t z0 = (uint32_t) darray_at(stack, darray_count(stack) - 2);
  uint32_t z1 = (uint32_t) darray_remove_last(stack);
  struct node_template_s {
    uint16_t v;
    // NULL means this template have been instantiated.
    // Otherwise it points to the LO template.
    memo_it lo;
    union {
      // Points to HI template when template is not yet instantiated.
      memo_it hi;
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
  // by level from the bottom up.
  memo_t tab;
  memo_init(tab);

  memo_t node_tab;
  memo_init(node_tab);

  memo_it insert_template(uint32_t k0, uint32_t k1) {
    uint32_t key[2];
    // Taking advantage of symmetry of intersection appears to help a tiny bit.
    if (k0 < k1) {
      key[0] = k0;
      key[1] = k1;
    } else {
      key[0] = k1;
      key[1] = k0;
    }
    memo_it it;
    int just_created = memo_it_insert_u(&it, tab, (void *) key, 8);
    if (!just_created) return it;
    if (!k0 || !k1) {
      memo_it_put(it, bot);
      return it;
    }
    if (k0 == 1 && k1 == 1) {
      memo_it_put(it, top);
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
      memo_it_put(it, t);
      return it;
    } else if (n0->v < n1->v) {
      memo_it it2 = insert_template(n0->lo, k1);
      memo_it_put(it, memo_it_data(it2));
      return it2;
    } else {
      memo_it it2 = insert_template(k0, n1->lo);
      memo_it_put(it, memo_it_data(it2));
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
    uint32_t *l = (uint32_t *) memo_it_key(t->lo);
    uint32_t *h = (uint32_t *) memo_it_key(t->hi);
    printf("%d:%d = %d:%d, %d:%d\n", n[0], n[1], l[0], l[1], h[0], h[1]);
  }

  uint32_t get_node(uint16_t v, uint32_t lo, uint32_t hi) {
    // Create or return existing node representing !v ? lo : hi.
    uint32_t key[3];
    key[0] = lo;
    key[1] = hi;
    key[2] = v;
    memo_it it;
    int just_created = memo_it_insert_u(&it, node_tab, (void *) key, 12);
    if (just_created) {
      memo_it_put(it, (void *) freenode);
      node_ptr n = pool[freenode];
      n->v = v;
      n->lo = lo;
      n->hi = hi;
      if (!(freenode % 100000)) printf("freenode = %d\n", freenode);
      if (POOL_MAX == freenode) {
	die("pool is full");
      }
      return freenode++;
    }
    return (uint32_t) memo_it_data(it);
  }

  uint32_t instantiate(memo_it it) {
    node_template_ptr t = (node_template_ptr) memo_it_data(it);
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
  //memo_forall(tab, dump);
  uint32_t key[2];
  key[0] = z0;
  key[1] = z1;
  memo_it it = memo_it_at_u(tab, (void *) key, 8);
  uint32_t root = instantiate(it);
  // TODO: What if the intersection is node 0 or 1?
  if (root <= 1) {
    die("root is 0 or 1!");
  }
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
  memo_forall(tab, clear_it);
  memo_clear(tab);

  memo_clear(node_tab);
  return z0;
}

void zdd_check() {
  memo_t node_tab;
  memo_init(node_tab);
  for (uint32_t i = 2; i < freenode; i++) {
    memo_it it;
    uint32_t key[3];
    key[0] = pool[i]->lo;
    key[1] = pool[i]->hi;
    key[2] = pool[i]->v;
    if (!memo_it_insert_u(&it, node_tab, (void *) key, 12)) {
      printf("duplicate: %d %d\n", i, (int) it->data);
    } else {
      it->data = (void *) i;
    }
    if (!pool[i]->hi) {
      printf("HI -> FALSE: %d\n", i);
    }
    if (i == pool[i]->lo) {
      printf("LO self-loop: %d\n", i);
    }
    if (i == pool[i]->hi) {
      printf("HI self-loop: %d\n", i);
    }
  }
  memo_clear(node_tab);
}

void zdd_init() {
  // Initialize TRUE and FALSE nodes.
  pool[0]->v = ~0;
  pool[0]->lo = 0;
  pool[0]->hi = 0;
  pool[1]->v = ~0;
  pool[1]->lo = 1;
  pool[1]->hi = 1;
  freenode = 2;
  darray_init(stack);
}

void zdd_dump() {
  for(uint32_t i = (uint32_t) darray_last(stack); i < freenode; i++) {
    printf("I%d: !%d ? %d : %d\n", i, pool[i]->v, pool[i]->lo, pool[i]->hi);
  }
}

void vmax_check() {
  if (!vmax_is_set) die("vmax not set");
}

uint32_t zdd_powerset() {
  vmax_check();
  uint16_t r = zdd_next_node();
  zdd_push();
  for(int v = 1; v < vmax; v++) zdd_add_node(v, 1, 1);
  zdd_add_node(vmax, -1, -1);
  return r;
}

void zdd_forall(void (*fn)(int *, int)) {
  vmax_check();
  int v[vmax], vcount = 0;
  void recurse(uint32_t p) {
    if (!p) return;
    if (1 == p) {
      fn(v, vcount);
      return;
    }
    if (zdd_lo(p)) recurse(zdd_lo(p));
    v[vcount++] = zdd_v(p);
    recurse(zdd_hi(p));
    vcount--;
  }
  recurse(zdd_root());
}

uint16_t zdd_vmax() {
  vmax_check();
  return vmax;
}

uint32_t zdd_size() {
  return zdd_next_node() - zdd_root() + 2;
}

// Construct ZDD of sets containing exactly 1 of the elements in the given list.
// Zero suppression means we must treat sequences in the list carefully.
void zdd_contains_exactly_1(const int *a, int count) {
  vmax_check();
  zdd_push();
  int v = 1;
  int i = 0;
  while(v <= vmax) {
    if (i >= count) {
      // Don't care about the rest of the elements.
      zdd_add_node(v++, 1, 1);
    } else if (v == a[i]) {
      // Find length of consecutive sequence.
      int k;
      for(k = 0; i + k < count && v + k == a[i + k]; k++);
      uint32_t n = zdd_next_node();
      uint32_t h = v + k > vmax ? 1 : n + k + (count != i + k);
      if (i >= 1) {
	// In the middle of the list: must fix previous node; we reach said node
	// if we've seen an element in the list already, in which case the
	// arrows must bypass the entire sequence, i.e. we need the whole
	// sequence to be out of the set.
	//set_node(n - 1, v - 1, h, h);
	zdd_set_hilo(n - 1, h);
      }
      i += k;
      k += v;
      while (v < k) {
	// If we see an element, bypass the rest of the sequence (see above),
	// otherwise we look for the next element in the sequence.
	zdd_add_node(v++, 1, 1);
	zdd_set_hi(zdd_last_node(), h);
	//set_node(n, v++, n + 1, h);
	//n++;
      }
      //v--;
      if (count == i) {
	// If none of the list showed up, then return false, otherwise,
	// onwards! (Through the rest of the elements to the end.)
	//set_node(n - 1, v, 0, h);
	zdd_set_lo(zdd_last_node(), 0);
	zdd_set_hi(zdd_last_node(), h);
      }
    } else if (!i) {
      // We don't care about the membership of elements before the list.
      zdd_add_node(v++, 1, 1);
    } else {
      zdd_add_node(v, 2, 2);
      zdd_add_node(v, 2, 2);
      v++;
    }
  }
  // Fix last node.
  uint32_t last = zdd_last_node();
  if (zdd_lo(last) > last) zdd_set_lo(last, 1);
  if (zdd_hi(last) > last) zdd_set_hi(last, 1);
}

// Construct ZDD of sets containing at most 1 of the elements in the given
// list.
void zdd_contains_at_most_1(const int *a, int count) {
  vmax_check();
  zdd_push();
  uint32_t n = zdd_last_node();
  // Start with ZDD of all sets.
  int v = 1;
  while(v < vmax) {
    zdd_add_node(v++, 1, 1);
  }
  zdd_add_node(v, -1, -1);
  // If there is nothing or only one element in the list then we are done.
  if (count <= 1) return;

  // At this point, there are at least two elements in the list.
  // Construct new branch for when elements of the list are detected. We
  // branch off at the first element, then hop over all remaining elements,
  // then rejoin.
  v = a[0];

  uint32_t n1 = zdd_next_node();
  zdd_set_hi(n + v, n1);
  v++;
  uint32_t last = 0;
  for(int i = 1; i < count; i++) {
    int v1 = a[i];
    while(v < v1) {
      last = zdd_add_node(v++, 1, 1);
    }
    zdd_set_hi(n + v, zdd_next_node());
    v++;
  }
  // v = last element of list + 1

  // The HI edges of the last element of the list, and more generally, the last
  // sequence of the list must be corrected.
  for(int v1 = a[count - 1]; zdd_hi(n + v1) == zdd_next_node(); v1--) {
    zdd_set_hi(n + v1, n + v);
  }

  if (vmax < v) {
    // Special case: list ends with vmax. Especially troublesome if there's
    // a little sequence, e.g. vmax - 2, vmax - 1, vmax.
    for(v = vmax; zdd_hi(n + v) > n + vmax; v--) {
      zdd_set_hi(n + v, 1);
    }
    // The following line is only needed if we added any nodes to the branch,
    // but is harmless if we execute it unconditionally since the last node
    // to be added was (!vmax ? 1 : 1).
    zdd_set_hilo(zdd_last_node(), 1);
    return;
  }

  // Rejoin main branch.
  if (last) zdd_set_hilo(last, n + v);
}

// Construct ZDD of sets containing at least 1 of the elements in the given
// list.
void zdd_contains_at_least_1(const int *a, int count) {
  vmax_check();
  zdd_push();
  uint32_t n = zdd_last_node();
  // Start with ZDD of all sets.
  int v = 1;
  while(v < vmax) {
    zdd_add_node(v++, 1, 1);
  }
  zdd_add_node(v, -1, -1);
  if (!count) return;

  // Construct new branch for when elements of the list are not found.
  v = a[0];
  if (1 == count) {
    zdd_set_lo(n + v, 0);
    return;
  }

  uint32_t n1 = zdd_next_node();
  zdd_set_lo(n + v, n1);
  v++;
  for(int i = 1; i < count; i++) {
    int v1 = a[i];
    while(v <= v1) {
      zdd_add_node(v++, 1, 1);
    }
    zdd_set_hi(zdd_last_node(), n + v);
  }

  zdd_set_lo(zdd_last_node(), 0);
  if (vmax < v) zdd_set_hi(zdd_last_node(), 1);
}

// Construct ZDD of sets not containing any elements from the given list.
// Assumes not every variable is on the list.
void zdd_contains_0(const int *a, int count) {
  vmax_check();
  zdd_push();
  int i = 1;
  int v1 = count ? a[0] : -1;
  for(int v = 1; v <= vmax; v++) {
    if (v1 == v) {
      v1 = i < count ? a[i++] : -1;
    } else {
      zdd_add_node(v, 1, 1);
    }
  }
  uint32_t n = zdd_last_node();
  zdd_set_lo(n, 1);
  zdd_set_hi(n, 1);
}

// Construct ZDD of sets containing exactly 1 element for each interval
// [a_k, a_{k+1}) in given list. List must start with a_0 = 1, while there is an
// implied vmax + 1 at end of list, so the last interval is [a_n, vmax + 1).
//
// The ZDD begins:
//   1 ... 2
//   1 --- a_1
//   2 ... 3
//   2 --- a_1
//   ...
//   a_1 - 1 ... F
//   a_1 - 1 --- a_1
//
// and so on:
//   a_k ... a_k + 1
//   a_k --- a_{k+1}
// and so on until vmax --- F, vmax ... T.
void zdd_1_per_interval(const int* list, int count) {
  vmax_check();
  zdd_push();
  // Check list[0] is 1.
  int i = 0;
  uint32_t n = zdd_last_node();
  int get() {
    i++;
    //return i < inta_count(a) ? inta_at(a, i) : -1;
    return i < count ? list[i] : -1;
  }
  int target = get();
  for (int v = 1; v <= vmax; v++) {
    zdd_abs_node(v, n + v + 1, target > 0 ? n + target : 1);
    if (v == target - 1 || v == vmax) {
      zdd_set_lo(zdd_last_node(), 0);
      target = get();
    }
  }
}

// Construct ZDD of sets containing exactly n of the elements in the
// given list.
void zdd_contains_exactly_n(int n, const int *a, int count) {
  zdd_push();
  if (n > count) {
    die("unhandled special case (should return empty family");
  }
  // Lookup table for sub-ZDDs we construct recursively.
  uint32_t tab[count][n + 1];
  memset(tab, 0, count * (n + 1) * sizeof(uint32_t));
  uint32_t recurse(int i, int n) {
    // The outermost invocation is a special case, as other invocations
    // assume part of the ZDD has already been built. We have i == -1
    // during this special case.
    int v = -1 == i ? 1 : a[i] + 1;
    uint32_t root;
    if (i == count - 1) {
      // Base case: by now, n should be zero, so finish off the ZDD with
      // everything leading to TRUE.
      // We can reach here even in the first invocation of recurse(); this
      // happens if there is nothing in the list.
      if (-1 != i && tab[i][0]) return tab[i][0];
      if (vmax < v) {
	root = 1;
      } else {
        root = zdd_next_node();
	while(v < vmax) zdd_add_node(v++, 1, 1);
	zdd_add_node(v, -1, -1);
      }
      if (-1 != i) tab[i][0] = root;
      return root;
    }
    if (-1 != i && tab[i][n]) return tab[i][n];
    int v1 = a[i + 1];
    int is_empty = v == v1;
    root = zdd_next_node();
    while(v < v1) zdd_add_node(v++, 1, 1);
    if (!n) {
      if (is_empty) {
	root = recurse(i + 1, n);
      } else {
	uint32_t last = zdd_last_node();
	zdd_set_hilo(last, recurse(i + 1, n));
      }
      if (-1 != i) tab[i][n] = root;
      return root;
    }
    uint32_t last = zdd_add_node(v, 0, 0);
    // If we include this variable, then that's one down, n - 1 more to go
    // in the remaining.
    zdd_set_hi(last, recurse(i + 1, n - 1));
    if (n < count - i - 1) {
      // If there are enough unexamined nodes we can leave this variable
      // out and still make the quota.
      zdd_set_lo(last, recurse(i + 1, n));
    }
    if (-1 != i) tab[i][n] = root;
    return root;
  }
  recurse(-1, n);
}
