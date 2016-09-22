/*
 * Copyright 2016 Tom van Dijk, Johannes Kepler University Linz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * This is an implementation of Hybrid Multi-Terminal Zero-Suppressed Binary Decision Diagrams.
 */

/* Do not include this file directly. Instead, include sylvan.h */

#ifndef SYLVAN_TBDD_H
#define SYLVAN_TBDD_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Hybrid ZDDs, combining ZDD and BDD minimisation rules.
 *
 * Each edge to a node has a tag. Tag 0xfffff is magical "*".
 * The edge to False always has tag *.
 * The edge to a terminal for an empty domain also has tag *.
 *
 * Edges to nodes and terminals are interpreted under a given domain.
 * - tag X means all variables from X to the node/terminal use the ZDD rule
 *           and all variables before X use the BDD rule
 * - tag * means all variables use the BDD rule
 */

/**
 * An TBDD is a 64-bit value. The low 40 bits are an index into the unique table.
 * The highest 1 bit is the complement edge, indicating negation.
 * For Boolean TBDDs, this means "not X", for Integer and Real TBDDs, this means "-X".
 */
typedef uint64_t TBDD;
typedef TBDD TBDDMAP;

#define TBDD_COMPLEMENT_EDGES 0

/**
 * Todo check:
 * Special tag "0xfffff" means "bottom" for when there are NO (0,k) nodes
 * Complement edges: transfer only to low child, not to high child.
 * Note that this value of "true" is for the "empty" domain
 */
#define tbdd_complement    ((TBDD)0x8000000000000000LL)
#define tbdd_emptydomain   ((TBDD)0x000fffff00000000LL)
#define tbdd_false         ((TBDD)0x000fffff00000000LL)
#if TBDD_COMPLEMENT_EDGES
#define tbdd_true          ((TBDD)0x800fffff00000000LL)
#else
#define tbdd_true          ((TBDD)0x000fffff00000001LL)
#endif
#define tbdd_invalid       ((TBDD)0xffffffffffffffffLL)

/**
 * Initialize TBDD functionality.
 * This initializes internal and external referencing datastructures,
 * and registers them in the garbage collection framework.
 */
void sylvan_init_tbdd(void);

/**
 * Create a TBDD terminal of type <type> and value <value>.
 * For custom types, the value could be a pointer to some external struct.
 */
TBDD tbdd_makeleaf(uint32_t type, uint64_t value);

/**
 * Create an internal TBDD node of Boolean variable <var>, with low edge <low> and high edge <high>.
 * <var> is a 24-bit integer.
 * Please note that this does NOT check variable ordering!
 */
TBDD tbdd_makenode(uint32_t var, TBDD low, TBDD high, uint32_t nextvar);

/**
 * Change the tag on an edge; this function ensures the minimization rules are followed.
 * This is relevant when the new tag is identical to the variable of the node.
 */
TBDD tbdd_settag(TBDD dd, uint32_t tag);

/**
 * Returns 1 is the TBDD is a terminal, or 0 otherwise.
 */
int tbdd_isleaf(TBDD tbdd);
#define tbdd_isnode(tbdd) (tbdd_isleaf(tbdd) ? 0 : 1)

/**
 * For TBDD terminals, returns <type> and <value>
 */
uint32_t tbdd_gettype(TBDD terminal);
uint64_t tbdd_getvalue(TBDD terminal);

/**
 * For internal TBDD nodes, returns <var>, <low> and <high>
 */
uint32_t tbdd_getvar(TBDD node);
TBDD tbdd_getlow(TBDD node);
TBDD tbdd_gethigh(TBDD node);

/**
 * Evaluate a TBDD, assigning <value> (1 or 0) to <variable>;
 * <variable> is the current variable in the domain, and <nextvar> the 
 * next variable in the domain.
 */
TBDD tbdd_eval(TBDD dd, uint32_t variable, int value, uint32_t nextvar);

/**
 * Obtain a TBDD representing a positive literal of variable <var>.
 */
TBDD tbdd_ithvar(uint32_t var);

/**
 * Obtain a TBDD representing a negative literal of variable <var>.
 */
TBDD tbdd_nithvar(uint32_t var);

/**
 * Convert an MTBDD to a TBDD.
 */
TASK_DECL_2(TBDD, tbdd_from_mtbdd, MTBDD, MTBDD);
#define tbdd_from_mtbdd(dd, domain) CALL(tbdd_from_mtbdd, dd, domain)

/**
 * Convert a TBDD to an MTBDD.
 */
TASK_DECL_2(TBDD, tbdd_to_mtbdd, TBDD, TBDD);
#define tbdd_to_mtbdd(dd, domain) CALL(tbdd_to_mtbdd, dd, domain)

/**
 * Create a cube of positive literals of the variables in arr.
 * This represents sets of variables, also variable domains.
 */
TBDD tbdd_from_array(uint32_t *arr, size_t len);

/**
 * Create a cube of literals of the given domain with the values given in <arr>.
 * Uses True as the leaf.
 */
TBDD tbdd_cube(TBDD dom, uint8_t *arr);

/**
 * Same as tbdd_cube, but adds the cube to an existing set.
 */
TASK_DECL_3(TBDD, tbdd_union_cube, TBDD, TBDD, uint8_t*);
#define tbdd_union_cube(set, dom, arr) CALL(tbdd_union_cube, set, dom, arr)

/**
 * Compute the and operator for two boolean TBDDs
 */
TASK_DECL_3(TBDD, tbdd_and, TBDD, TBDD, TBDD);
#define tbdd_and(a, b, dom) CALL(tbdd_and, a, b, dom)

/**
 * Compute the ite operator, where a must be a boolean TBDD
 */
TASK_DECL_4(TBDD, tbdd_ite, TBDD, TBDD, TBDD, TBDD);
#define tbdd_ite(a, b, c, dom) CALL(tbdd_ite, a, b, c, dom)

/**
 * Compute the not operator
 */
TASK_DECL_2(TBDD, tbdd_not, TBDD, TBDD);
#define tbdd_not(dd, dom) CALL(tbdd_not, dd, dom)

/**
 * Other operators: not, diff...
 */
// #define tbdd_not(dd, dom) tbdd_ite(dd, tbdd_false, tbdd_true, dom)
// #define tbdd_and(a, b, dom) tbdd_ite(a, b, tbdd_false, dom)
#define tbdd_or(a, b, dom) tbdd_ite(a, tbdd_true, b, dom)
#define tbdd_imp(a, b, dom) tbdd_ite(a, b, tbdd_true, dom)
#define tbdd_invimp(a, b, dom) tbdd_imp(b, a, dom)
#define tbdd_less(a, b, dom) tbdd_ite(a, tbdd_false, b, dom)
#define tbdd_diff(a, b, dom) tbdd_less(b, a, dom)
// Missing operators: xor, equiv, nand, nor

/**
 * Compute existential quantification, but stay in same domain
 */
TASK_DECL_3(TBDD, tbdd_exists, TBDD, TBDD, TBDD);
#define tbdd_exists(dd, vars, dom) CALL(tbdd_exists, dd, vars, dom)

/**
 * Compute existential quantification, by changing the domain
 * Remove all variables from <dd> that are not in <newdom>
 */
TASK_DECL_2(TBDD, tbdd_exists_dom, TBDD, TBDD);
#define tbdd_exists_dom(dd, newdom) CALL(tbdd_exists_dom, dd, newdom)

/**
 * Compute the combination of the AND operator and existential quantification
 */
// TASK_DECL_3(TBDD, tbdd_and_exists, TBDD, TBDD, TBDD, TBDD);

/**
 * Compute the application of a transition relation to a set.
 * Assumes the relation is valid on given variables, and set to 0 for other variables.
 * Assumes the variables are interleaved, with s even and t odd (s+1).
 * Assumes the relation does not contain other information but the transitions.
 */
TASK_DECL_4(TBDD, tbdd_relnext, TBDD, TBDD, TBDD, TBDD);
#define tbdd_relnext(set, rel, vars, dom) CALL(tbdd_relnext, set, rel, vars, dom)

/**
 * Compute the number of satisfying assignments
 */
TASK_DECL_2(double, tbdd_satcount, TBDD, TBDD);
#define tbdd_satcount(dd, dom) CALL(tbdd_satcount, dd, dom)

TBDD tbdd_enum_first(TBDD dd, TBDD dom, uint8_t *arr);
TBDD tbdd_enum_next(TBDD dd, TBDD dom, uint8_t *arr);

typedef struct tbdd_trace {
    struct tbdd_trace *prev;
    uint32_t var;
    uint8_t val;
} * tbdd_trace_t;

LACE_TYPEDEF_CB(void, tbdd_enum_cb, void*, uint8_t*, size_t);
VOID_TASK_DECL_4(tbdd_enum, TBDD, TBDD, tbdd_enum_cb, void*);
#define tbdd_enum(dd, dom, cb, context) CALL(tbdd_enum, dd, dom, cb, context)

VOID_TASK_DECL_4(tbdd_enum_seq, TBDD, TBDD, tbdd_enum_cb, void*);
#define tbdd_enum_seq(dd, dom, cb, context) CALL(tbdd_enum_seq, dd, dom, cb, context)

LACE_TYPEDEF_CB(TBDD, tbdd_collect_cb, void*, uint8_t*, size_t);
TASK_DECL_5(TBDD, tbdd_collect, TBDD, TBDD, TBDD, tbdd_collect_cb, void*);
#define tbdd_collect(dd, dom, res_dom, cb, context) CALL(tbdd_collect, dd, dom, res_dom, cb, context)

/**
 * TODO:
 * - forall
 * - and_exists (for one domain)
 * - sat_one / pick_cube and to_array
 * - visitor
 * - relprev
 * - compose
 * - serialization functions
 * - and for two domains
 * - or for two domains
 * - combine two domains (variable sets)
 * - extend_domain with default value 0 or 1 or 2 ("both")
 * - and_exists for two domains (left, left_dom, right, right_dom, ex_vars)
 */

/**
 * Count the number of TBDD nodes and terminals (excluding tbdd_false and tbdd_true) in the given <count> TBDDs
 */
size_t tbdd_nodecount_more(const TBDD *tbdds, size_t count);

static inline size_t
tbdd_nodecount(const TBDD dd) {
    return tbdd_nodecount_more(&dd, 1);
}

/**
 * Write a .dot representation of a given TBDD
 */
void tbdd_fprintdot(FILE *out, TBDD mtbdd);
#define tbdd_printdot(dd, cb) tbdd_fprintdot(stdout, dd)

/**
 * Write a .dot representation of a given TBDD, but without complement edges.
 */
// void tbdd_fprintdot_nc(FILE *out, TBDD mtbdd);
// #define tbdd_printdot_nc(dd, cb) tbdd_fprintdot_nc(stdout, dd)

/**
 * Garbage collection
 * Sylvan supplies two default methods to handle references to nodes, but the user
 * is encouraged to implement custom handling. Simply add a handler using sylvan_gc_add_mark
 * and let the handler call tbdd_gc_mark_rec for every TBDD that should be saved
 * during garbage collection.
 */

/**
 * Call tbdd_gc_mark_rec for every tbdd you want to keep in your custom mark functions.
 */
VOID_TASK_DECL_1(tbdd_gc_mark_rec, TBDD);
#define tbdd_gc_mark_rec(tbdd) CALL(tbdd_gc_mark_rec, tbdd)

/**
 * Default external referencing. During garbage collection, TBDDs marked with tbdd_ref will
 * be kept in the forest.
 * It is recommended to prefer tbdd_protect and tbdd_unprotect.
 */
// TBDD tbdd_ref(TBDD a);
// void tbdd_deref(TBDD a);
// size_t tbdd_count_refs(void);

/**
 * Default external pointer referencing. During garbage collection, the pointers are followed and the TBDD
 * that they refer to are kept in the forest.
 */
void tbdd_protect(TBDD* ptr);
void tbdd_unprotect(TBDD* ptr);
size_t tbdd_count_protected(void);

/**
 * If tbdd_set_ondead is set to a callback, then this function marks TBDDs (terminals).
 * When they are dead after the mark phase in garbage collection, the callback is called for marked TBDDs.
 * The ondead callback can either perform cleanup or resurrect dead terminals.
 */
#define tbdd_notify_ondead(dd) llmsset_notify_ondead(nodes, dd&~tbdd_complement)

/**
 * Infrastructure for internal references (per-thread, e.g. during TBDD operations)
 * Use tbdd_refs_push and tbdd_refs_pop to put TBDDs on a thread-local reference stack.
 * Use tbdd_refs_spawn and tbdd_refs_sync around SPAWN and SYNC operations when the result
 * of the spawned Task is a TBDD that must be kept during garbage collection.
 */
typedef struct tbdd_refs_internal
{
    size_t r_size, r_count;
    size_t s_size, s_count;
    TBDD *results;
    Task **spawns;
} *tbdd_refs_internal_t;

extern DECLARE_THREAD_LOCAL(tbdd_refs_key, tbdd_refs_internal_t);

static inline TBDD
tbdd_refs_push(TBDD tbdd)
{
    LOCALIZE_THREAD_LOCAL(tbdd_refs_key, tbdd_refs_internal_t);
    if (tbdd_refs_key->r_count >= tbdd_refs_key->r_size) {
        tbdd_refs_key->r_size *= 2;
        tbdd_refs_key->results = (TBDD*)realloc(tbdd_refs_key->results, sizeof(TBDD) * tbdd_refs_key->r_size);
    }
    tbdd_refs_key->results[tbdd_refs_key->r_count++] = tbdd;
    return tbdd;
}

static inline void
tbdd_refs_pop(int amount)
{
    LOCALIZE_THREAD_LOCAL(tbdd_refs_key, tbdd_refs_internal_t);
    tbdd_refs_key->r_count-=amount;
}

static inline void
tbdd_refs_spawn(Task *t)
{
    LOCALIZE_THREAD_LOCAL(tbdd_refs_key, tbdd_refs_internal_t);
    if (tbdd_refs_key->s_count >= tbdd_refs_key->s_size) {
        tbdd_refs_key->s_size *= 2;
        tbdd_refs_key->spawns = (Task**)realloc(tbdd_refs_key->spawns, sizeof(Task*) * tbdd_refs_key->s_size);
    }
    tbdd_refs_key->spawns[tbdd_refs_key->s_count++] = t;
}

static inline TBDD
tbdd_refs_sync(TBDD result)
{
    LOCALIZE_THREAD_LOCAL(tbdd_refs_key, tbdd_refs_internal_t);
    tbdd_refs_key->s_count--;
    return result;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
