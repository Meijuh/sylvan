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

#include <sylvan_config.h>

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sylvan.h>
#include <sylvan_int.h>

#include <sylvan_refs.h>

/**
 * Primitives
 */

int
tbdd_isleaf(TBDD dd)
{
    if (TBDD_GETINDEX(dd) <= 1) return 1;
    return tbddnode_isleaf(TBDD_GETNODE(dd));
}

uint32_t
tbdd_getvar(TBDD node)
{
    return tbddnode_getvariable(TBDD_GETNODE(node));
}

TBDD
tbdd_getlow(TBDD tbdd)
{
    return tbddnode_low(tbdd, TBDD_GETNODE(tbdd));
}

TBDD
tbdd_gethigh(TBDD tbdd)
{
    return tbddnode_high(tbdd, TBDD_GETNODE(tbdd));
}

uint32_t
tbdd_gettype(TBDD leaf)
{
    return tbddnode_gettype(TBDD_GETNODE(leaf));
}

uint64_t
tbdd_getvalue(TBDD leaf)
{
    return tbddnode_getvalue(TBDD_GETNODE(leaf));
}

/**
 * Implementation of garbage collection
 */

/**
 * Recursively mark MDD nodes as 'in use'
 */
VOID_TASK_IMPL_1(tbdd_gc_mark_rec, MDD, tbdd)
{
    if (tbdd == tbdd_true) return;
    if (tbdd == tbdd_false) return;

    if (llmsset_mark(nodes, TBDD_GETINDEX(tbdd))) {
        tbddnode_t n = TBDD_GETNODE(tbdd);
        if (!tbddnode_isleaf(n)) {
            SPAWN(tbdd_gc_mark_rec, tbddnode_getlow(n));
            CALL(tbdd_gc_mark_rec, tbddnode_gethigh(n));
            SYNC(tbdd_gc_mark_rec);
        }
    }
}

/**
 * External references
 */

// refs_table_t tbdd_refs;
refs_table_t tbdd_protected;
static int tbdd_protected_created = 0;

/*MDD
tbdd_ref(MDD a)
{
    if (a == tbdd_true || a == tbdd_false) return a;
    refs_up(&tbdd_refs, TBDD_GETINDEX(a));
    return a;
}

void
tbdd_deref(MDD a)
{
    if (a == tbdd_true || a == tbdd_false) return;
    refs_down(&tbdd_refs, TBDD_GETINDEX(a));
}

size_t
tbdd_count_refs()
{
    return refs_count(&tbdd_refs);
}*/

void
tbdd_protect(TBDD *a)
{
    if (!tbdd_protected_created) {
        // In C++, sometimes tbdd_protect is called before Sylvan is initialized. Just create a table.
        protect_create(&tbdd_protected, 4096);
        tbdd_protected_created = 1;
    }
    protect_up(&tbdd_protected, (size_t)a);
}

void
tbdd_unprotect(TBDD *a)
{
    if (tbdd_protected.refs_table != NULL) protect_down(&tbdd_protected, (size_t)a);
}

size_t
tbdd_count_protected()
{
    return protect_count(&tbdd_protected);
}

/* Called during garbage collection */
/*
VOID_TASK_0(tbdd_gc_mark_external_refs)
{
    // iterate through refs hash table, mark all found
    size_t count=0;
    uint64_t *it = refs_iter(&tbdd_refs, 0, tbdd_refs.refs_size);
    while (it != NULL) {
        SPAWN(tbdd_gc_mark_rec, refs_next(&tbdd_refs, &it, tbdd_refs.refs_size));
        count++;
    }
    while (count--) {
        SYNC(tbdd_gc_mark_rec);
    }
}*/

VOID_TASK_0(tbdd_gc_mark_protected)
{
    // iterate through refs hash table, mark all found
    size_t count=0;
    uint64_t *it = protect_iter(&tbdd_protected, 0, tbdd_protected.refs_size);
    while (it != NULL) {
        BDD *to_mark = (BDD*)protect_next(&tbdd_protected, &it, tbdd_protected.refs_size);
        SPAWN(tbdd_gc_mark_rec, *to_mark);
        count++;
    }
    while (count--) {
        SYNC(tbdd_gc_mark_rec);
    }
}

/* Infrastructure for internal markings */
DECLARE_THREAD_LOCAL(tbdd_refs_key, tbdd_refs_internal_t);

VOID_TASK_0(tbdd_refs_mark_task)
{
    LOCALIZE_THREAD_LOCAL(tbdd_refs_key, tbdd_refs_internal_t);
    size_t i, j=0;
    for (i=0; i<tbdd_refs_key->r_count; i++) {
        if (j >= 40) {
            while (j--) SYNC(tbdd_gc_mark_rec);
            j=0;
        }
        SPAWN(tbdd_gc_mark_rec, tbdd_refs_key->results[i]);
        j++;
    }
    for (i=0; i<tbdd_refs_key->s_count; i++) {
        Task *t = tbdd_refs_key->spawns[i];
        if (!TASK_IS_STOLEN(t)) break;
        if (TASK_IS_COMPLETED(t)) {
            if (j >= 40) {
                while (j--) SYNC(tbdd_gc_mark_rec);
                j=0;
            }
            SPAWN(tbdd_gc_mark_rec, *(BDD*)TASK_RESULT(t));
            j++;
        }
    }
    while (j--) SYNC(tbdd_gc_mark_rec);
}

VOID_TASK_0(tbdd_refs_mark)
{
    TOGETHER(tbdd_refs_mark_task);
}

VOID_TASK_0(tbdd_refs_init_task)
{
    tbdd_refs_internal_t s = (tbdd_refs_internal_t)malloc(sizeof(struct tbdd_refs_internal));
    s->r_size = 128;
    s->r_count = 0;
    s->s_size = 128;
    s->s_count = 0;
    s->results = (BDD*)malloc(sizeof(BDD) * 128);
    s->spawns = (Task**)malloc(sizeof(Task*) * 128);
    SET_THREAD_LOCAL(tbdd_refs_key, s);
}

VOID_TASK_0(tbdd_refs_init)
{
    INIT_THREAD_LOCAL(tbdd_refs_key);
    TOGETHER(tbdd_refs_init_task);
    sylvan_gc_add_mark(TASK(tbdd_refs_mark));
}

/**
 * Initialize and quit functions
 */

static int tbdd_initialized = 0;

static void
tbdd_quit()
{
    //refs_free(&tbdd_refs);
    if (tbdd_protected_created) {
        protect_free(&tbdd_protected);
        tbdd_protected_created = 0;
    }

    tbdd_initialized = 0;
}

void
sylvan_init_tbdd()
{
    if (tbdd_initialized) return;
    tbdd_initialized = 1;

    sylvan_register_quit(tbdd_quit);
    // sylvan_gc_add_mark(TASK(tbdd_gc_mark_external_refs));
    sylvan_gc_add_mark(TASK(tbdd_gc_mark_protected));

    // refs_create(&tbdd_refs, 1024);
    if (!tbdd_protected_created) {
        protect_create(&tbdd_protected, 4096);
        tbdd_protected_created = 1;
    }

    LACE_ME;
    CALL(tbdd_refs_init);
}

/**
 * Primitives
 */
TBDD
tbdd_makeleaf(uint32_t type, uint64_t value)
{
    struct tbddnode n;
    tbddnode_makeleaf(&n, type, value);

    int created;
    uint64_t index = llmsset_lookup(nodes, n.a, n.b, &created);
    if (index == 0) {
        LACE_ME;

        sylvan_gc();

        index = llmsset_lookup(nodes, n.a, n.b, &created);
        if (index == 0) {
            fprintf(stderr, "BDD Unique table full, %zu of %zu buckets filled!\n", llmsset_count_marked(nodes), llmsset_get_size(nodes));
            exit(1);
        }
    }

    if (created) sylvan_stats_count(BDD_NODES_CREATED);
    else sylvan_stats_count(BDD_NODES_REUSED);

    return (TBDD)TBDD_SETTAG(index, 0xfffff);
}

/**
 * Node creation primitive.
 *
 * Returns the TBDD representing the formula <var> then <high> else <low>.
 * Variable <nextvar> is the next variable in the domain, necessary to correctly
 * perform the ZDD minimization rule.
 */
TBDD
tbdd_makenode(uint32_t var, TBDD low, TBDD high, uint32_t nextvar)
{
    struct tbddnode n;

    if (low == high) {
        /**
         * Same children (BDD minimization)
         * Just return one of them, this is correct in all cases.
         */
        return low;
    } else if (high == tbdd_false) {
        /**
         * high equals False (ZDD minimization)
         * low != False (because low != high)
         * if tag is next in domain just update tag to var
         * if tag is * (all BDD minimization) 
         */
        /* check if no next var; then low must be a terminal */
        if (nextvar == 0xFFFFF) return TBDD_SETTAG(low, var);
        /* check if next var is skipped with ZDD rule */
        if (nextvar == TBDD_GETTAG(low)) return TBDD_SETTAG(low, var);
        /* nodes are skipped with (k,k), so we must make the next node */
        tbddnode_makenode(&n, nextvar, low, low);
    } else {
        /**
         * No minimization rule.
         */
        tbddnode_makenode(&n, var, low, high);
    }

    /* if low had a mark, it is moved to the result */
#if TBDD_COMPLEMENT_EDGES
    int mark = TBDD_HASMARK(low);
#else
    assert(!TBDD_HASMARK(low));
    assert(!TBDD_HASMARK(high));
    int mark = 0;
#endif

    int created;
    uint64_t index = llmsset_lookup(nodes, n.a, n.b, &created);
    if (index == 0) {
        LACE_ME;

        tbdd_refs_push(low);
        tbdd_refs_push(high);
        sylvan_gc();
        tbdd_refs_pop(2);

        index = llmsset_lookup(nodes, n.a, n.b, &created);
        if (index == 0) {
            fprintf(stderr, "BDD Unique table full, %zu of %zu buckets filled!\n", llmsset_count_marked(nodes), llmsset_get_size(nodes));
            exit(1);
        }
    }

    if (created) sylvan_stats_count(TBDD_NODES_CREATED);
    else sylvan_stats_count(TBDD_NODES_REUSED);

    TBDD result = TBDD_SETTAG(index, var);
    return mark ? result | tbdd_complement : result;
}

TBDD
tbdd_makemapnode(uint32_t var, TBDD low, TBDD high)
{
    struct tbddnode n;
    uint64_t index;
    int created;

    // in an TBDDMAP, the low edges eventually lead to 0 and cannot have a low mark
    assert(!TBDD_HASMARK(low));

    tbddnode_makemapnode(&n, var, low, high);
    index = llmsset_lookup(nodes, n.a, n.b, &created);
    if (index == 0) {
        LACE_ME;

        tbdd_refs_push(low);
        tbdd_refs_push(high);
        sylvan_gc();
        tbdd_refs_pop(2);

        index = llmsset_lookup(nodes, n.a, n.b, &created);
        if (index == 0) {
            fprintf(stderr, "BDD Unique table full, %zu of %zu buckets filled!\n", llmsset_count_marked(nodes), llmsset_get_size(nodes));
            exit(1);
        }
    }

    if (created) sylvan_stats_count(BDD_NODES_CREATED);
    else sylvan_stats_count(BDD_NODES_REUSED);

    return index;
}

/**
 * Change the tag on an edge; this function ensures the minimization rules are followed.
 * This is relevant when the new tag is identical to the variable of the node.
 */
TBDD
tbdd_settag(TBDD dd, uint32_t tag)
{
    if (TBDD_GETINDEX(dd) > 1) {
        tbddnode_t n = TBDD_GETNODE(dd);
        if (!tbddnode_isleaf(n)) {
            uint32_t var = tbddnode_getvariable(n);
            assert(tag <= var);
            if (var == tag) {
                TBDD low = tbddnode_low(dd, n);
                TBDD high = tbddnode_high(dd, n);
                if (low == high) return low;
            }
        }
    }
    return TBDD_SETTAG(dd, tag);
}

/**
 * Evaluate a TBDD, assigning <value> (1 or 0) to <variable>;
 * <variable> is the current variable in the domain, and <nextvar> the 
 * next variable in the domain.
 */
TBDD
tbdd_eval(TBDD dd, uint32_t variable, int value, uint32_t next_var)
{
    uint32_t tag = TBDD_GETTAG(dd);
    if (variable < tag) return dd;
    assert(variable == tag);
    if (tbdd_isleaf(dd)) return value ? tbdd_false : tbdd_settag(dd, next_var);
    tbddnode_t n = TBDD_GETNODE(dd);
    uint32_t var = tbddnode_getvariable(n);
    if (variable < var) return value ? tbdd_false : tbdd_settag(dd, next_var);
    assert(variable == var);
    return value ? tbddnode_high(dd, n) : tbddnode_low(dd, n);
}

/**
 * Obtain a TBDD representing a positive literal of variable <var>.
 */
TBDD
tbdd_ithvar(uint32_t var)
{
    return tbdd_makenode(var, tbdd_false, tbdd_true, 0xfffff);
}

/**
 * Obtain a TBDD representing a negative literal of variable <var>.
 */
TBDD
tbdd_nithvar(uint32_t var)
{
    return tbdd_makenode(var, tbdd_true, tbdd_false, 0xfffff);
}

/**
 * Convert an MTBDD to a TBDD
 */
TASK_IMPL_2(TBDD, tbdd_from_mtbdd, MTBDD, dd, MTBDD, domain)
{
    /* Special treatment for True and False */
    if (dd == mtbdd_false) return tbdd_false;
    if (dd == mtbdd_true) return tbdd_true;

    /* Maybe perform garbage collection */
    sylvan_gc_test();

    /* Count operation */
    sylvan_stats_count(TBDD_FROM_MTBDD);

    /* First (maybe) match domain with dd */
    mtbddnode_t ndd = MTBDD_GETNODE(dd);
    mtbddnode_t ndomain = NULL;
    if (mtbddnode_isleaf(ndd)) {
        domain = mtbdd_true;
    } else {
        /* Get variable and cofactors */
        assert(domain != mtbdd_true && domain != mtbdd_false);
        ndomain = MTBDD_GETNODE(domain);
        uint32_t domain_var = mtbddnode_getvariable(ndomain);

        uint32_t var = mtbddnode_getvariable(ndd);
        while (domain_var != var) {
            assert(domain_var < var);
            domain = mtbddnode_followhigh(domain, ndomain);
            assert(domain != mtbdd_true && domain != mtbdd_false);
            ndomain = MTBDD_GETNODE(domain);
            domain_var = mtbddnode_getvariable(ndomain);
        }
     }

    /* Check cache */
    TBDD result;
    if (cache_get(CACHE_TBDD_FROM_MTBDD|dd, domain, 0, &result)) {
        sylvan_stats_count(TBDD_FROM_MTBDD_CACHED);
        return result;
    }

    if (mtbddnode_isleaf(ndd)) {
        /* Convert a leaf */
        uint32_t type = mtbddnode_gettype(ndd);
        uint64_t value = mtbddnode_getvalue(ndd);
        result = tbdd_makeleaf(type, value);
        return result;
    } else {
        /* Get variable and cofactors */
        uint32_t var = mtbddnode_getvariable(ndd);
        MTBDD dd_low = mtbddnode_followlow(dd, ndd);
        MTBDD dd_high = mtbddnode_followhigh(dd, ndd);

        /* Recursive */
        MTBDD next_domain = mtbddnode_followhigh(domain, ndomain);
        tbdd_refs_spawn(SPAWN(tbdd_from_mtbdd, dd_high, next_domain));
        TBDD low = tbdd_refs_push(CALL(tbdd_from_mtbdd, dd_low, next_domain));
        TBDD high = tbdd_refs_sync(SYNC(tbdd_from_mtbdd));
        tbdd_refs_pop(1);
        uint32_t next_domain_var = next_domain != mtbdd_true ? mtbdd_getvar(next_domain) : 0xfffff;
        result = tbdd_makenode(var, low, high, next_domain_var);
    }

    /* Store in cache */
    if (cache_put(CACHE_TBDD_FROM_MTBDD|dd, domain, 0, result)) {
        sylvan_stats_count(TBDD_FROM_MTBDD_CACHEDPUT);
    }

    return result;
}

/**
 * Convert a TBDD to an MTBDD.
 */
TASK_IMPL_2(TBDD, tbdd_to_mtbdd, TBDD, dd, TBDD, dom)
{
    /* Special treatment for True and False */
    if (dd == tbdd_false) return mtbdd_false;
    if (dd == tbdd_true) return mtbdd_true;

    /* Maybe perform garbage collection */
    sylvan_gc_test();

    /* Count operation */
    sylvan_stats_count(TBDD_TO_MTBDD);

    /* Check cache */
    TBDD result;
    if (cache_get3(CACHE_TBDD_TO_MTBDD, dd, dom, 0, &result)) {
        sylvan_stats_count(TBDD_TO_MTBDD_CACHED);
        return result;
    }

    /**
     * Get dd variable, domain variable, and next domain variable
     */
    const tbddnode_t dd_node = TBDD_NOTAG(dd) == tbdd_true ? NULL : TBDD_GETNODE(dd);
    const uint32_t dd_tag = TBDD_GETTAG(dd);
    const uint32_t dd_var = dd_node == NULL || tbddnode_isleaf(dd_node) ? 0xfffff : tbddnode_getvariable(dd_node);

    if (dd_tag == 0xfffff) {
        /* Convert a leaf */
        uint32_t type = tbddnode_gettype(dd_node);
        uint64_t value = tbddnode_getvalue(dd_node);
        result = mtbdd_makeleaf(type, value);
        return result;
    }

    const tbddnode_t dom_node = TBDD_GETNODE(dom);
    const uint32_t dom_var = tbddnode_getvariable(dom_node);
    const TBDD dom_next = tbddnode_high(dom, dom_node);
    const uint32_t dom_next_var = dom_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(dom_next));

    assert(dom_var <= dd_tag);
    assert(dom_var <= dd_var);

    /**
     * Get cofactors
     */
    TBDD dd0, dd1;
    if (dom_var < dd_tag) {
        dd0 = dd1 = dd;
    } else if (dom_var < dd_var) {
        dd0 = tbdd_settag(dd, dom_next_var);
        dd1 = tbdd_false;
    } else {
        dd0 = tbddnode_low(dd, dd_node);
        dd1 = tbddnode_high(dd, dd_node);
    }

    mtbdd_refs_spawn(SPAWN(tbdd_to_mtbdd, dd0, dom_next));
    MTBDD high = tbdd_to_mtbdd(dd1, dom_next);
    MTBDD low = mtbdd_refs_sync(SYNC(tbdd_to_mtbdd));
    result = mtbdd_makenode(dom_var, low, high);

    /* Store in cache */
    if (cache_put3(CACHE_TBDD_TO_MTBDD, dd, dom, 0, result)) {
        sylvan_stats_count(TBDD_TO_MTBDD_CACHEDPUT);
    }

    return result;
}

/**
 * Create a cube of positive literals of the variables in arr.
 * This represents sets of variables, also variable domains.
 */
TBDD
tbdd_from_array(uint32_t *arr, size_t len)
{
    if (len == 0) return tbdd_true;
    else if (len == 1) return tbdd_makenode(*arr, tbdd_false, tbdd_true, 0xfffff);
    else return tbdd_makenode(arr[0], tbdd_false, tbdd_from_array(arr+1, len-1), arr[1]);
}

/**
 * Create a cube of literals of the given domain with the values given in <arr>.
 * Uses True as the leaf.
 */
TBDD tbdd_cube(TBDD dom, uint8_t *arr)
{
    if (dom == tbdd_true) return tbdd_true;
    tbddnode_t n = TBDD_GETNODE(dom);
    uint32_t dom_var = tbddnode_getvariable(n);
    TBDD dom_next = tbddnode_high(dom, n);
    uint32_t dom_next_var = dom_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(dom_next));
        TBDD res = tbdd_cube(dom_next, arr+1);
    if (*arr == 0) {
        return tbdd_makenode(dom_var, res, tbdd_false, dom_next_var);
    } else if (*arr == 1) {
        return tbdd_makenode(dom_var, tbdd_false, res, dom_next_var);
    } else if (*arr == 2) {
        return tbdd_makenode(dom_var, res, res, dom_next_var);
    }
    return tbdd_invalid;
}

/**
 * Same as tbdd_cube, but adds the cube to an existing set.
 */
TASK_IMPL_3(TBDD, tbdd_union_cube, TBDD, set, TBDD, dom, uint8_t*, arr)
{
    /**
     * Terminal cases
     */
    if (dom == tbdd_true) return tbdd_true;
    if (set == tbdd_true) return tbdd_true;
    if (set == tbdd_false) return tbdd_cube(dom, arr);

    /**
     * Test for garbage collection
     */
    sylvan_gc_test();

    /**
     * Count operation
     */
    sylvan_stats_count(TBDD_UNION_CUBE);

    /**
     * Get set variable, domain variable, and next domain variable
     */
    const tbddnode_t set_node = TBDD_NOTAG(set) == tbdd_true ? NULL : TBDD_GETNODE(set);
    const uint32_t set_var = set_node == NULL ? 0xfffff : tbddnode_getvariable(set_node);
    const uint32_t set_tag = TBDD_GETTAG(set);
    const tbddnode_t dom_node = TBDD_GETNODE(dom);
    const uint32_t dom_var = tbddnode_getvariable(dom_node);
    const TBDD dom_next = tbddnode_high(dom, dom_node);
    const uint32_t dom_next_var = dom_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(dom_next));

    assert(dom_var <= set_tag);
    assert(dom_var <= set_var);

    TBDD set0, set1;
    if (dom_var < set_tag) {
        set0 = set1 = set;
    } else if (dom_var < set_var) {
        set0 = tbdd_settag(set, dom_next_var);
        set1 = tbdd_false;
    } else {
        set0 = tbddnode_low(set, set_node);
        set1 = tbddnode_high(set, set_node);
    }

    if (*arr == 0) {
        TBDD low = tbdd_union_cube(set0, dom_next, arr+1);
        return tbdd_makenode(dom_var, low, set1, dom_next_var);
    } else if (*arr == 1) {
        TBDD high = tbdd_union_cube(set1, dom_next, arr+1);
        return tbdd_makenode(dom_var, set0, high, dom_next_var);
    } else if (*arr == 2) {
        tbdd_refs_spawn(SPAWN(tbdd_union_cube, set0, dom_next, arr+1));
        TBDD high = tbdd_union_cube(set1, dom_next, arr+1);
        tbdd_refs_push(high);
        TBDD low = tbdd_refs_sync(SYNC(tbdd_union_cube));
        tbdd_refs_pop(1);
        return tbdd_makenode(dom_var, low, high, dom_next_var);
    }

    return tbdd_invalid;
}

/**
 * Implementation of the AND operator for Boolean TBDDs
 * We interpret <a> and <b> under the given domain <dom>
 */
TASK_IMPL_3(TBDD, tbdd_and, TBDD, a, TBDD, b, TBDD, dom)
{
    /**
     * Check the case where A or B is False
     */
    if (a == tbdd_false || b == tbdd_false) {
        return tbdd_false;
    }

    /**
     * Check the case A \and A == A
     * Also checks the case True \and True == True
     */
    if (TBDD_NOTAG(a) == TBDD_NOTAG(b)) {
        uint32_t a_tag = TBDD_GETTAG(a);
        uint32_t b_tag = TBDD_GETTAG(b);
        uint32_t tag = a_tag < b_tag ? a_tag : b_tag;
        return tbdd_settag(a, tag);
    }

    assert(dom != tbdd_true);

    /**
     * Switch A and B if A > B (for cache)
     */
    if (TBDD_GETINDEX(a) > TBDD_GETINDEX(b)) {
        TBDD t = a;
        a = b;
        b = t;
    }

    /**
     * Maybe run garbage collection
     */
    sylvan_gc_test();

    /**
     * Count operation
     */
    sylvan_stats_count(TBDD_BAND);

    /**
     * Check the cache
     */
    TBDD result;
    if (cache_get3(CACHE_TBDD_BAND, a, b, dom, &result)) {
        sylvan_stats_count(TBDD_BAND_CACHED);
        return result;
    }

    /**
     * Compute the pivot variable
     */
    tbddnode_t a_node, b_node;
    uint32_t a_var, b_var;

    if (TBDD_NOTAG(a) == tbdd_true) {
        a_node = NULL;
        a_var = 0xfffff;
    } else {
        a_node = TBDD_GETNODE(a);
        a_var = tbddnode_getvariable(a_node);
    }

    if (TBDD_NOTAG(b) == tbdd_true) {
        b_node = NULL;
        b_var = 0xfffff;
    } else {
        b_node = TBDD_GETNODE(b);
        b_var = tbddnode_getvariable(b_node);
    }

    uint32_t var = a_var < b_var ? a_var : b_var;
    assert(var < 0xfffff);

    /**
     * Forward domain to pivot variable
     */
    tbddnode_t d_node = TBDD_GETNODE(dom);
    uint32_t d_var = tbddnode_getvariable(d_node);
    while (d_var != var) {
        assert(d_var < var);
        dom = tbddnode_high(dom, d_node);
        assert(dom != tbdd_true);
        d_node = TBDD_GETNODE(dom);
        d_var = tbddnode_getvariable(d_node);
    }

    /**
     * Get next variable in domain
     */
    TBDD d_next = tbddnode_high(dom, d_node);
    uint32_t d_next_var = d_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(d_next));

    /**
     * Get tags
     */
    uint32_t a_tag = TBDD_GETTAG(a);
    uint32_t b_tag = TBDD_GETTAG(b);

    assert(a_tag <= a_var);
    assert(b_tag <= b_var);

    /**
     * Get cofactors for A
     */
    TBDD a0, a1;
    if (a_var == var) {
        /**
         * Pivot variable is our variable
         */
        a0 = tbddnode_low(a, a_node);
        a1 = tbddnode_high(a, a_node);
    } else {
        /**
         * Pivot variable is before our variable
         * Or A is True
         */
        if (var < a_tag) {
            /**
             * Pivot variable is before our tag
             * A := ***00x..     A := ***0True
             * B := ??y.....     B := ??y.....
             */
            a0 = a1 = a;
        } else {
            /**
             * Pivot variable is >= our tag
             * A := **000x..     A := *0000x..   A := **00True   A := *000True
             * B := ??y.....     B := ??y.....   B := ??y.....   B := ??y.....
             */
            a0 = tbdd_settag(a, d_next_var);
            a1 = tbdd_false;
        }
    }

    /**
     * Get cofactors for B
     */
    TBDD b0, b1;
    if (b_var == var) {
        /**
         * Pivot variable is our variable
         */
        b0 = tbddnode_low(b, b_node);
        b1 = tbddnode_high(b, b_node);
    } else {
        /**
         * Pivot variable is before our variable
         * Or B is True
         */
        if (var < b_tag) {
            /**
             * Pivot variable is before our tag
             */
            b0 = b1 = b;
        } else {
            /**
             * Pivot variable is >= our tag
             */
            b0 = tbdd_settag(b, d_next_var);
            b1 = tbdd_false;
        }
    }

    assert(TBDD_GETTAG(a0) >= d_next_var);
    assert(TBDD_GETTAG(a1) >= d_next_var);
    assert(TBDD_GETTAG(b0) >= d_next_var);
    assert(TBDD_GETTAG(b1) >= d_next_var);

    /**
     * Now we call recursive tasks
     */
    tbdd_refs_spawn(SPAWN(tbdd_and, a0, b0, d_next));
    TBDD high = CALL(tbdd_and, a1, b1, d_next);
    tbdd_refs_push(high);
    TBDD low = tbdd_refs_sync(SYNC(tbdd_and));
    tbdd_refs_pop(1);

    /**
     * Compute result node
     */
    result = tbdd_makenode(var, low, high, d_next_var);
    uint32_t tag = a_tag < b_tag ? a_tag : b_tag;
    if (tag < var) {
        result = tbdd_makenode(tag, result, tbdd_false, var);
    }

    /**
     * Cache the result
     */ 
    if (cache_put3(CACHE_TBDD_BAND, a, b, dom, result)) {
        sylvan_stats_count(TBDD_BAND_CACHEDPUT);
    }

    return result;
}

/**
 * Implementation of the ITE operator for Boolean TBDDs
 * We interpret <a>, <b> and <c> under the given domain <dom>
 */
TASK_IMPL_4(TBDD, tbdd_ite, TBDD, a, TBDD, b, TBDD, c, TBDD, dom)
{
    /**
     * Trivial cases (similar to sylvan_ite)
     */
    if (a == tbdd_true) return b;
    if (a == tbdd_false) return c;
    if (a == b) b = tbdd_true;
    if (a == c) c = tbdd_false;
    if (c == tbdd_false) return tbdd_and(a, b, dom);
    if (b == c) return b;
    // not much more here, because negation is not constant...

    assert(dom != tbdd_true);

    /**
     * Maybe run garbage collection
     */
    sylvan_gc_test();

    /**
     * Count operation
     */
    //sylvan_stats_count(TBDD_ITE);

    /**
     * Check the cache
     * this is problem!
     * 53 bits per thing...
     * with 4 things: 212 bits... plus operation id
     * but only 3*64 = 192 bits available!
     */
    /*TBDD result;
    if (cache_get4(CACHE_TBDD_BAND, a, b, dom, &result)) {
        sylvan_stats_count(TBDD_BAND_CACHED);
        return result;
    }*/

    /**
     * Obtain variables and tags
     */
    uint32_t a_var, b_var, c_var;
    tbddnode_t a_node, b_node, c_node;

    // a cannot be False
    if (TBDD_NOTAG(a) == tbdd_true) {
        a_node = NULL;
        a_var = 0xfffff;
    } else {
        a_node = TBDD_GETNODE(a);
        a_var = tbddnode_getvariable(a_node);
    }

    // b can be True or False
    if (b == tbdd_false || TBDD_NOTAG(b) == tbdd_true) {
        b_node = NULL;
        b_var = 0xfffff;
    } else {
        b_node = TBDD_GETNODE(b);
        b_var = tbddnode_getvariable(b_node);
    }

    // c cannot be False
    if (c == tbdd_false || TBDD_NOTAG(c) == tbdd_true) {
        c_node = NULL;
        c_var = 0xfffff;
    } else {
        c_node = TBDD_GETNODE(c);
        c_var = tbddnode_getvariable(c_node);
    }

    uint32_t a_tag = TBDD_GETTAG(a);
    uint32_t b_tag = TBDD_GETTAG(b);
    uint32_t c_tag = TBDD_GETTAG(c);

    uint32_t minvar = a_var < b_var ? (a_var < c_var ? a_var : c_var) : (b_var < c_var ? b_var : c_var);
    uint32_t mintag = a_tag < b_tag ? (a_tag < c_tag ? a_tag : c_tag) : (b_tag < c_tag ? b_tag : c_tag);

    /**
     * Compute the pivot variable
     * if tags are the same: lowest variable
     * otherwise: lowest tag
     */
    const uint32_t var = (a_tag == b_tag && b_tag == c_tag) ? minvar : mintag;
    assert(var != 0xfffff);

    /**
     * Forward domain to pivot variable
     */
    tbddnode_t d_node = TBDD_GETNODE(dom);
    uint32_t d_var = tbddnode_getvariable(d_node);
    while (d_var != var) {
        assert(d_var < var);
        dom = tbddnode_high(dom, d_node);
        assert(dom != tbdd_true);
        d_node = TBDD_GETNODE(dom);
        d_var = tbddnode_getvariable(d_node);
    }

    /**
     * Get next variable in domain
     */
    TBDD d_next = tbddnode_high(dom, d_node);
    uint32_t d_next_var = d_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(d_next));

    if (a_var == var) assert(a_tag == mintag);
    if (b_var == var) assert(b_tag == mintag);
    if (c_var == var) assert(c_tag == mintag);
    if (a_var != var && a_tag == mintag) assert(var >= a_tag);
    if (b_var != var && b_tag == mintag) assert(var >= b_tag);
    if (c_var != var && c_tag == mintag) assert(var >= c_tag);

    /**
     * Get cofactors for A
     */
    TBDD a0, a1;
    if (var == a_var) {
        /**
         * Pivot variable is our variable
         */
        a0 = tbddnode_low(a, a_node);
        a1 = tbddnode_high(a, a_node);
    } else if (var >= a_tag) {
        /**
         * Pivot variable is >= our tag
         */
        a0 = tbdd_settag(a, d_next_var);
        a1 = tbdd_false;
    } else {
        /**
         * Pivot variable is a (k,k) node
         */
        a0 = a1 = a;
    }

    /**
     * Get cofactors for B
     */
    TBDD b0, b1;
    if (var == b_var) {
        /**
         * Pivot variable is our variable
         */
        b0 = tbddnode_low(b, b_node);
        b1 = tbddnode_high(b, b_node);
    } else if (var >= b_tag) {
        /**
         * Pivot variable is >= our tag
         */
        b0 = tbdd_settag(b, d_next_var);
        b1 = tbdd_false;
    } else {
        /**
         * Pivot variable is a (k,k) node
         */
        b0 = b1 = b;
    }

    /**
     * Get cofactors for C
     */
    TBDD c0, c1;
    if (var == c_var) {
        /**
         * Pivot variable is our variable
         */
        c0 = tbddnode_low(c, c_node);
        c1 = tbddnode_high(c, c_node);
    } else if (var >= c_tag) {
        /**
         * Pivot variable is >= our tag
         */
        c0 = tbdd_settag(c, d_next_var);
        c1 = tbdd_false;
    } else {
        /**
         * Pivot variable is a (k,k) node
         */
        c0 = c1 = c;
    }

    assert(TBDD_GETTAG(a0) >= d_next_var);
    assert(TBDD_GETTAG(a1) >= d_next_var);
    assert(TBDD_GETTAG(b0) >= d_next_var);
    assert(TBDD_GETTAG(b1) >= d_next_var);
    assert(TBDD_GETTAG(c0) >= d_next_var);
    assert(TBDD_GETTAG(c1) >= d_next_var);

    /**
     * Now we call recursive tasks
     */
    tbdd_refs_spawn(SPAWN(tbdd_ite, a0, b0, c0, d_next));
    TBDD high = CALL(tbdd_ite, a1, b1, c1, d_next);
    tbdd_refs_push(high);
    TBDD low = tbdd_refs_sync(SYNC(tbdd_ite));
    tbdd_refs_pop(1);

    /**
     * Compute result node
     */
    TBDD result = tbdd_makenode(var, low, high, d_next_var);
    if (mintag < var) result = tbdd_makenode(mintag, result, tbdd_false, var);

    /**
     * Cache the result
     */ 
    /*if (cache_put3(CACHE_TBDD_BAND, a, b, dom, result)) {
        sylvan_stats_count(TBDD_BAND_CACHEDPUT);
    }*/

    return result;
}


/**
 * Compute the not operator
 */
TASK_IMPL_2(TBDD, tbdd_not, TBDD, dd, TBDD, dom)
{
    /**
     * Trivial cases (similar to sylvan_ite)
     */
    if (dd == tbdd_true) return tbdd_false;
    if (dd == tbdd_false) return tbdd_true;

    assert(dom != tbdd_true);

    /**
     * Maybe run garbage collection
     */
    sylvan_gc_test();

    /**
     * Count operation
     */
    sylvan_stats_count(TBDD_NOT);

    /**
     * Check the cache
     */
    TBDD result;
    if (cache_get3(CACHE_TBDD_NOT, dd, dom, 0, &result)) {
        sylvan_stats_count(TBDD_NOT_CACHED);
        return result;
    }

    /**
     * Obtain variables and tags
     */
    tbddnode_t dd_node = TBDD_GETNODE(dd);
    uint32_t var = tbddnode_getvariable(dd_node);
    uint32_t tag = TBDD_GETTAG(dd);

    /**
     * Forward domain to tag
     */
    tbddnode_t d_node = TBDD_GETNODE(dom);
    uint32_t d_var = tbddnode_getvariable(d_node);
    while (d_var != tag) {
        assert(d_var < tag);
        dom = tbddnode_high(dom, d_node);
        assert(dom != tbdd_true);
        d_node = TBDD_GETNODE(dom);
        d_var = tbddnode_getvariable(d_node);
    }

    /**
     * Get next variable in domain
     */
    TBDD d_next = tbddnode_high(dom, d_node);
    uint32_t d_next_var = d_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(d_next));

    /**
     * Get cofactors
     */
    TBDD dd0, dd1;
    if (tag == var) {
        dd0 = tbddnode_low(dd, dd_node);
        dd1 = tbddnode_high(dd, dd_node);
    } else {
        dd0 = tbdd_settag(dd, d_next_var);
        dd1 = tbdd_false;
    }

    assert(TBDD_GETTAG(dd0) >= d_next_var);
    assert(TBDD_GETTAG(dd1) >= d_next_var);

    /**
     * Now we call recursive tasks
     */
    tbdd_refs_spawn(SPAWN(tbdd_not, dd0, d_next));
    TBDD high = CALL(tbdd_not, dd1, d_next);
    tbdd_refs_push(high);
    TBDD low = tbdd_refs_sync(SYNC(tbdd_not));
    tbdd_refs_pop(1);

    /**
     * Compute result node
     */
    result = tbdd_makenode(tag, low, high, d_next_var);

    /**
     * Cache the result
     */ 
    if (cache_put3(CACHE_TBDD_NOT, dd, dom, 0, result)) {
        sylvan_stats_count(TBDD_NOT_CACHEDPUT);
    }

    return result;
}

/**
 * Compute existential quantification, but stay in same domain
 */
TASK_IMPL_3(TBDD, tbdd_exists, TBDD, dd, TBDD, vars, TBDD, dom)
{
    /**
     * Trivial cases
     */
    if (dd == tbdd_true) return dd;
    if (dd == tbdd_false) return dd;
    if (vars == tbdd_true) return dd;

    assert(dom != tbdd_true);

    /**
     * Maybe run garbage collection
     */
    sylvan_gc_test();

    /**
     * Count operation
     */
    sylvan_stats_count(TBDD_EXISTS);

    /**
     * Check the cache
     */
    TBDD result;
    if (cache_get3(CACHE_TBDD_EXISTS, dd, vars, dom, &result)) {
        sylvan_stats_count(TBDD_EXISTS_CACHED);
        return result;
    }

    /**
     * Obtain tag and var
     */
    const tbddnode_t dd_node = TBDD_NOTAG(dd) == tbdd_true ? NULL : TBDD_GETNODE(dd);
    const uint32_t dd_var = dd_node == NULL ? 0xfffff : tbddnode_getvariable(dd_node);
    const uint32_t dd_tag = TBDD_GETTAG(dd);

    /**
     * Obtain next variable to remove
     */
    tbddnode_t vars_node = TBDD_GETNODE(vars);
    uint32_t vars_var = tbddnode_getvariable(vars_node);

    /**
     * Forward <vars> to tag (skip to-remove when before tag)
     */
    while (vars_var < dd_tag) {
        vars = tbddnode_high(vars, vars_node);
        if (vars == tbdd_true) return dd;
        vars_node = TBDD_GETNODE(vars);
        vars_var = tbddnode_getvariable(vars_node);
    }

    /**
     * Compute pivot variable
     */
    const uint32_t var = vars_var < dd_var ? vars_var : dd_var;

    /**
     * Forward domain and get dom_var and dom_next_var
     */
    tbddnode_t dom_node = TBDD_GETNODE(dom);
    uint32_t dom_var = tbddnode_getvariable(dom_node);

    while (dom_var != var) {
        assert(dom_var < var);
        dom = tbddnode_high(dom, dom_node);
        assert(dom != tbdd_true);
        dom_node = TBDD_GETNODE(dom);
        dom_var = tbddnode_getvariable(dom_node);
    }

    const TBDD dom_next = tbddnode_high(dom, dom_node);
    const uint32_t dom_next_var = dom_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(dom_next));

    /**
     * Get cofactors
     */
    TBDD dd0, dd1;
    assert(dd_tag <= var);
    if (var < dd_var) {
        dd0 = tbdd_settag(dd, dom_next_var);
        dd1 = tbdd_false;
    } else {
        dd0 = tbddnode_low(dd, dd_node);
        dd1 = tbddnode_high(dd, dd_node);
    }

    if (var == vars_var) {
        // Quantify variable

        /**
         * Now we call recursive tasks
         */
        TBDD vars_next = tbddnode_high(vars, vars_node);
        tbdd_refs_spawn(SPAWN(tbdd_exists, dd0, vars_next, dom_next));
        TBDD high = CALL(tbdd_exists, dd1, vars_next, dom_next);
        tbdd_refs_push(high);
        TBDD low = tbdd_refs_sync(SYNC(tbdd_exists));
        tbdd_refs_pop(1);

        /**
         * Compute result node
         */
        result = tbdd_or(low, high, dom);
        if (dd_tag != var) result = tbdd_makenode(dd_tag, result, tbdd_false, var);
    } else {
        // Quantify variable

        /**
         * Now we call recursive tasks
         */
        tbdd_refs_spawn(SPAWN(tbdd_exists, dd0, vars, dom_next));
        TBDD high = CALL(tbdd_exists, dd1, vars, dom_next);
        tbdd_refs_push(high);
        TBDD low = tbdd_refs_sync(SYNC(tbdd_exists));
        tbdd_refs_pop(1);

        /**
         * Compute result node
         */
        result = tbdd_makenode(var, low, high, dom_next_var);
        if (dd_tag != var) result = tbdd_makenode(dd_tag, result, tbdd_false, var);
    }

    /**
     * Cache the result
     */ 
    if (cache_put3(CACHE_TBDD_EXISTS, dd, vars, dom, result)) {
        sylvan_stats_count(TBDD_EXISTS_CACHEDPUT);
    }

    return result;
}

/**
 * Compute existential quantification
 * Remove all variables from <dd> that are not in <newdom>
 */
TASK_IMPL_2(TBDD, tbdd_exists_dom, TBDD, dd, TBDD, dom)
{
    /**
     * Trivial cases
     */
    if (dd == tbdd_true) return dd;
    if (dd == tbdd_false) return dd;
    if (dom == tbdd_true) return tbdd_true;

    /**
     * Maybe run garbage collection
     */
    sylvan_gc_test();

    /**
     * Count operation
     */
    sylvan_stats_count(TBDD_EXISTS);

    /**
     * Check the cache
     */
    TBDD result;
    if (cache_get3(CACHE_TBDD_EXISTS, dd, dom, 0, &result)) {
        sylvan_stats_count(TBDD_EXISTS_CACHED);
        return result;
    }

    /**
     * Obtain tag
     */
    uint32_t tag = TBDD_GETTAG(dd);

    /**
     * Obtain domain variable
     */
    tbddnode_t d_node = TBDD_GETNODE(dom);
    uint32_t d_var = tbddnode_getvariable(d_node);

    /**
     * Forward domain to tag
     */
    while (d_var < tag) {
        dom = tbddnode_high(dom, d_node);
        if (dom == tbdd_true) return tbdd_true;
        d_node = TBDD_GETNODE(dom);
        d_var = tbddnode_getvariable(d_node);
    }

    // d_var is the new tag for the result.
    uint32_t newtag = d_var;

    if (TBDD_NOTAG(dd) == tbdd_true) {
        return tbdd_settag(tbdd_true, newtag);
    }
        
    /**
     * Obtain variable
     */
    tbddnode_t dd_node = TBDD_GETNODE(dd);
    uint32_t var = tbddnode_getvariable(dd_node);

    /**
     * Forward domain to var
     */
    while (d_var < var) {
        dom = tbddnode_high(dom, d_node);
        if (dom == tbdd_true) return tbdd_settag(tbdd_true, newtag);
        d_node = TBDD_GETNODE(dom);
        d_var = tbddnode_getvariable(d_node);
    }

    /**
     * Get cofactors
     */
    TBDD dd0 = tbddnode_low(dd, dd_node);
    TBDD dd1 = tbddnode_high(dd, dd_node);

    if (d_var == var) {
        // Keep variable

        /**
         * Get next variable in domain
         */
        TBDD d_next = tbddnode_high(dom, d_node);
        uint32_t d_next_var = d_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(d_next));

        /**
         * Now we call recursive tasks
         */
        tbdd_refs_spawn(SPAWN(tbdd_exists_dom, dd0, d_next));
        TBDD high = CALL(tbdd_exists_dom, dd1, d_next);
        tbdd_refs_push(high);
        TBDD low = tbdd_refs_sync(SYNC(tbdd_exists_dom));
        tbdd_refs_pop(1);

        /**
         * Compute result node
         */
        result = tbdd_makenode(d_var, low, high, d_next_var);
    } else {
        // Quantify variable

        /**
         * Now we call recursive tasks
         */
        tbdd_refs_spawn(SPAWN(tbdd_exists_dom, dd0, dom));
        TBDD high = CALL(tbdd_exists_dom, dd1, dom);
        tbdd_refs_push(high);
        TBDD low = tbdd_refs_sync(SYNC(tbdd_exists_dom));
        tbdd_refs_pop(1);

        /**
         * Compute result node
         */
        result = tbdd_or(low, high, dom);
    }

    /**
     * Add tag on top
     */
    if (newtag != d_var) result = tbdd_makenode(newtag, result, tbdd_false, d_var);
    
    /**
     * Cache the result
     */ 
    if (cache_put3(CACHE_TBDD_EXISTS, dd, dom, 0, result)) {
        sylvan_stats_count(TBDD_EXISTS_CACHEDPUT);
    }

    return result;
}

/**
 * Compute the application of a transition relation to a set.
 * Assumes interleaved variables, with s even and t odd (s+1).
 * Assumes dom (the domain) only contains even (s) variables.
 * Assumes the relation is defined only on vars
 * Assumes vars contains the relational variables (s and t)
 * Assumes the even variables in vars are all in dom
 * Assumes the relation does not contain other information but the transitions.
 */
TASK_IMPL_4(TBDD, tbdd_relnext, TBDD, set, TBDD, rel, TBDD, vars, TBDD, dom)
{
    /**
     * Trivial cases
     */
    if (set == tbdd_false) return tbdd_false;
    if (rel == tbdd_false) return tbdd_false;
    if (vars == tbdd_true) return set;
    assert(dom != tbdd_true); // because vars is not True

    // TODO: count statistics, cache

    /**
     * Obtain tag and var of set and rel
     */
    const tbddnode_t set_node = TBDD_NOTAG(set) == tbdd_true ? NULL : TBDD_GETNODE(set);
    const uint32_t set_var = set_node == NULL ? 0xfffff : tbddnode_getvariable(set_node);
    const uint32_t set_tag = TBDD_GETTAG(set);

    const tbddnode_t rel_node = TBDD_NOTAG(rel) == tbdd_true ? NULL : TBDD_GETNODE(rel);
    const uint32_t rel_var = rel_node == NULL ? 0xfffff : tbddnode_getvariable(rel_node);
    const uint32_t rel_tag = TBDD_GETTAG(rel);

    /**
     * Obtain domain variable
     */
    tbddnode_t dom_node = TBDD_GETNODE(dom);
    uint32_t dom_var = tbddnode_getvariable(dom_node);

    /**
     * Obtain relation variable
     */
    tbddnode_t vars_node = TBDD_GETNODE(vars);
    uint32_t vars_var = tbddnode_getvariable(vars_node);

    assert((dom_var&1) == 0);
    assert((vars_var&1) == 0);
    assert(dom_var <= vars_var);
    assert(set_tag == 0xfffff || (set_tag&1)==0);
    assert(set_var == 0xfffff || (set_var&1)==0);

    /**
     * Forward vars as long as it is before either tag
     */
    while (vars_var < set_tag && vars_var < (rel_tag&~1)) {
        vars = tbddnode_high(vars, vars_node);
        if (vars == tbdd_true) return set;
        vars_node = TBDD_GETNODE(vars);
        vars_var = tbddnode_getvariable(vars_node);
    }

    /**
     * Forward domain as logn as it is before set tag or next relation variable
     */
    while (dom_var < set_tag && dom_var < vars_var) {
        dom = tbddnode_high(dom, dom_node);
        assert(dom != tbdd_true);
        dom_node = TBDD_GETNODE(dom);
        dom_var = tbddnode_getvariable(dom_node);
    }

    /**
     * Select pivot variable 
     */
    const uint32_t var = dom_var;
    const TBDD dom_next = tbddnode_high(dom, dom_node);
    const uint32_t dom_next_var = dom_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(dom_next));

    if (dom_var < vars_var) {
        /**
         * Pivot variable is not a relation variable
         */

        /**
         * Obtain cofactors of set
         */
        TBDD set0, set1;
        if (var < set_tag) {
            set0 = set1 = set;
        } else if (var < set_var) {
            set0 = tbdd_settag(set, dom_next_var);
            set1 = tbdd_false;
        } else {
            set0 = tbddnode_low(set, set_node);
            set1 = tbddnode_high(set, set_node);
        }

        /**
         * Obtain cofactors of rel (because non relevant variables are set to 0)
         */
        assert(var < rel_tag);

        tbdd_refs_spawn(SPAWN(tbdd_relnext, set0, rel, vars, dom_next));
        TBDD high = CALL(tbdd_relnext, set1, rel, vars, dom_next);
        tbdd_refs_push(high);
        TBDD low = tbdd_refs_sync(SYNC(tbdd_relnext));
        tbdd_refs_pop(1);
        TBDD result = tbdd_makenode(var, low, high, dom_next_var);

        return result;
    }

    /**
     * If we are here, then the pivot variable is a relation variable
     */

    uint32_t var_s = var;
    uint32_t var_t = var_s + 1;

    TBDD vars_next = tbddnode_high(vars, vars_node);
    tbddnode_t vars_next_node = vars_next == tbdd_true ? NULL : TBDD_GETNODE(vars_next);
    uint32_t vars_next_var = vars_next_node == NULL ? 0xfffff : tbddnode_getvariable(vars_next_node);
    assert(vars_next_var == var_t);
    vars_next = tbddnode_high(vars_next, vars_next_node);
    vars_next_node = vars_next == tbdd_true ? NULL : TBDD_GETNODE(vars_next);
    vars_next_var = vars_next_node == NULL ? 0xfffff : tbddnode_getvariable(vars_next_node);

    /**
     * Obtain cofactors of set
     */
    TBDD set0, set1;
    if (var_s < set_tag) {
        set0 = set1 = set;
    } else if (var_s < set_var) {
        set0 = tbdd_settag(set, dom_next_var);
        set1 = tbdd_false;
    } else {
        set0 = tbddnode_low(set, set_node);
        set1 = tbddnode_high(set, set_node);
    }

    /**
     * Obtain cofactors of rel
     */
    TBDD rel0, rel1;
    if (var_s < rel_tag) {
        rel0 = rel1 = rel;
    } else if (var_s < rel_var) {
        rel0 = tbdd_settag(rel, var_t);
        rel1 = tbdd_false;
    } else {
        rel0 = tbddnode_low(rel, rel_node);
        rel1 = tbddnode_high(rel, rel_node);
    }

    /**
     * Obtain cofactors of rel0
     */
    const tbddnode_t rel0_node = TBDD_GETINDEX(rel0) <= 1 ? NULL : TBDD_GETNODE(rel0);
    const uint32_t rel0_tag = TBDD_GETTAG(rel0);
    const uint32_t rel0_var = rel0_node == NULL ? 0xfffff : tbddnode_getvariable(rel0_node);

    TBDD rel00, rel01;
    if (var_t < rel0_tag) {
        rel00 = rel01 = rel0;
    } else if (var_t < rel0_var) {
        rel00 = tbdd_settag(rel0, vars_next_var);
        rel01 = tbdd_false;
    } else {
        rel00 = tbddnode_low(rel0, rel0_node);
        rel01 = tbddnode_high(rel0, rel0_node);
    }

    /**
     * Obtain cofactors of rel1
     */
    const tbddnode_t rel1_node = TBDD_GETINDEX(rel1) <= 1 ? NULL : TBDD_GETNODE(rel1);
    const uint32_t rel1_tag = TBDD_GETTAG(rel1);
    const uint32_t rel1_var = rel1_node == NULL ? 0xfffff : tbddnode_getvariable(rel1_node);

    TBDD rel10, rel11;
    if (var_t < rel1_tag) {
        rel10 = rel11 = rel1;
    } else if (var_t < rel1_var) {
        rel10 = tbdd_settag(rel1, vars_next_var);
        rel11 = tbdd_false;
    } else {
        rel10 = tbddnode_low(rel1, rel1_node);
        rel11 = tbddnode_high(rel1, rel1_node);
    }

    /**
     * Perform recursive computations
     */
    tbdd_refs_spawn(SPAWN(tbdd_relnext, set0, rel00, vars_next, dom_next));
    tbdd_refs_spawn(SPAWN(tbdd_relnext, set0, rel01, vars_next, dom_next));
    tbdd_refs_spawn(SPAWN(tbdd_relnext, set1, rel10, vars_next, dom_next));
    TBDD res11 = CALL(tbdd_relnext, set1, rel11, vars_next, dom_next);
    tbdd_refs_push(res11);
    TBDD res10 = tbdd_refs_sync(SYNC(tbdd_relnext));
    tbdd_refs_push(res10);
    TBDD res01 = tbdd_refs_sync(SYNC(tbdd_relnext));
    tbdd_refs_push(res01);
    TBDD res00 = tbdd_refs_sync(SYNC(tbdd_relnext));
    tbdd_refs_push(res00);

    /**
     * Now compute res0 and res1
     */
    tbdd_refs_spawn(SPAWN(tbdd_ite, res00, tbdd_true, res10, dom_next));
    TBDD res1 = CALL(tbdd_ite, res01, tbdd_true, res11, dom_next);
    tbdd_refs_push(res1);
    TBDD res0 = tbdd_refs_sync(SYNC(tbdd_ite));
    tbdd_refs_pop(5);

    /**
     * Now compute final result
     */
    TBDD result = tbdd_makenode(var_s, res0, res1, dom_next_var);
    
    return result;
}

/**
 * Compute number of variables in a set of variables / domain
 */
static int tbdd_set_count(TBDD dom)
{
    int res = 0;
    while (dom != tbdd_true) {
        res++;
        dom = tbddnode_high(dom, TBDD_GETNODE(dom));
    }
    return res;
}

TASK_IMPL_2(double, tbdd_satcount, TBDD, dd, TBDD, dom)
{
    /**
     * Handle False
     */
    if (dd == tbdd_false) return 0.0;

    /**
     * Handle no tag (True leaf)
     */
    uint32_t tag = TBDD_GETTAG(dd);
    if (tag == 0xfffff) {
        return powl(2.0L, tbdd_set_count(dom));
    }

    /**
     * Get domain variable
     */
    assert(dom != tbdd_true);
    tbddnode_t dom_node = TBDD_GETNODE(dom);
    uint32_t dom_var = tbddnode_getvariable(dom_node);

    /**
     * Count number of skipped nodes (BDD rule)
     */
    int skipped = 0;
    while (tag != dom_var) {
        skipped++;
        dom = tbddnode_high(dom, dom_node);
        dom_node = TBDD_GETNODE(dom);
        dom_var = tbddnode_getvariable(dom_node);
    }

    /**
     * Handle True
     */
    if (TBDD_NOTAG(dd) == tbdd_true) {
        return powl(2.0, skipped);
    }

    /**
     * Get variable of dd
     */
    tbddnode_t dd_node = TBDD_GETNODE(dd);
    uint32_t dd_var = tbddnode_getvariable(dd_node);

    /**
     * Forward domain
     */
    while (dd_var != dom_var) {
        dom = tbddnode_high(dom, dom_node);
        dom_node = TBDD_GETNODE(dom);
        dom_var = tbddnode_getvariable(dom_node);
    }

    SPAWN(tbdd_satcount, tbddnode_high(dd, dd_node), tbddnode_high(dom, dom_node));
    double result = CALL(tbdd_satcount, tbddnode_low(dd, dd_node), tbddnode_high(dom, dom_node));
    result += SYNC(tbdd_satcount);
    return result * powl(2.0L, skipped);
}

TBDD tbdd_enum_first(TBDD dd, TBDD dom, uint8_t *arr)
{
    if (dd == tbdd_false) {
        return tbdd_false;
    } else if (dom == tbdd_true) {
        assert(dd == tbdd_true);
        return dd;
    } else {
        /**
         * Obtain domain variable
         */
        const tbddnode_t dom_node = TBDD_GETNODE(dom);
        const uint32_t dom_var = tbddnode_getvariable(dom_node);
        const TBDD dom_next = tbddnode_high(dom, dom_node);
        const uint32_t dom_next_var = dom_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(dom_next));

        /**
         * Obtain cofactors
         */
        const tbddnode_t dd_node = TBDD_NOTAG(dd) == tbdd_true ? NULL : TBDD_GETNODE(dd);
        const uint32_t dd_var = dd_node == NULL ? 0xfffff : tbddnode_getvariable(dd_node);
        const uint32_t dd_tag = TBDD_GETTAG(dd);

        TBDD dd0, dd1;
        if (dom_var < dd_tag) {
            dd0 = dd1 = dd;
        } else if (dom_var < dd_var) {
            dd0 = tbdd_settag(dd, dom_next_var);
            dd1 = tbdd_false;
        } else {
            dd0 = tbddnode_low(dd, dd_node);
            dd1 = tbddnode_high(dd, dd_node);
        }

        /**
         * Try low first, else high, else return False
         */
        TBDD res = tbdd_enum_first(dd0, dom_next, arr+1);
        if (res != tbdd_false) {
            *arr = 0;
            return res;
        }

        res = tbdd_enum_first(dd1, dom_next, arr+1);
        if (res != tbdd_false) {
            *arr = 1;
            return res;
        }

        return tbdd_false;
    }
}

TBDD tbdd_enum_next(TBDD dd, TBDD dom, uint8_t *arr)
{
    if (dd == tbdd_false) {
        return tbdd_false;
    } else if (dom == tbdd_true) {
        assert(dd == tbdd_true);
        return tbdd_false;
    } else {
        /**
         * Obtain domain variable
         */
        const tbddnode_t dom_node = TBDD_GETNODE(dom);
        const uint32_t dom_var = tbddnode_getvariable(dom_node);
        const TBDD dom_next = tbddnode_high(dom, dom_node);
        const uint32_t dom_next_var = dom_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(dom_next));

        /**
         * Obtain cofactors
         */
        const tbddnode_t dd_node = TBDD_NOTAG(dd) == tbdd_true ? NULL : TBDD_GETNODE(dd);
        const uint32_t dd_var = dd_node == NULL ? 0xfffff : tbddnode_getvariable(dd_node);
        const uint32_t dd_tag = TBDD_GETTAG(dd);

        TBDD dd0, dd1;
        if (dom_var < dd_tag) {
            dd0 = dd1 = dd;
        } else if (dom_var < dd_var) {
            dd0 = tbdd_settag(dd, dom_next_var);
            dd1 = tbdd_false;
        } else {
            dd0 = tbddnode_low(dd, dd_node);
            dd1 = tbddnode_high(dd, dd_node);
        }

        if (*arr == 0) {
            TBDD res = tbdd_enum_next(dd0, dom_next, arr+1);
            if (res == tbdd_false) {
                res = tbdd_enum_first(dd1, dom_next, arr+1);
                if (res != tbdd_false) *arr = 1;
            }
            return res;
        } else if (*arr == 1) {
            return tbdd_enum_next(dd1, dom_next, arr+1);
        } else {
            return tbdd_invalid;
        }
    }
}

VOID_TASK_5(tbdd_enum_do, TBDD, dd, TBDD, dom, tbdd_enum_cb, cb, void*, ctx, tbdd_trace_t, trace)
{
    if (dd == tbdd_false) {
        return;
    } else if (dom == tbdd_true) {
        /**
         * Compute trace length
         */
        size_t len = 0;
        tbdd_trace_t p = trace;
        while (p != NULL) {
            len++;
            p = p->prev;
        }
        /**
         * Fill array
         */
        uint8_t arr[len];
        for (size_t i=0; i<len; i++) {
            arr[len-i-1] = trace->val;
            trace = trace->prev;
        }
        /**
         * Call callback
         */
        WRAP(cb, ctx, arr, len);
    } else {
        /**
         * Obtain domain variable
         */
        const tbddnode_t dom_node = TBDD_GETNODE(dom);
        const uint32_t dom_var = tbddnode_getvariable(dom_node);
        const TBDD dom_next = tbddnode_high(dom, dom_node);
        const uint32_t dom_next_var = dom_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(dom_next));

        /**
         * Obtain cofactors
         */
        const tbddnode_t dd_node = TBDD_NOTAG(dd) == tbdd_true ? NULL : TBDD_GETNODE(dd);
        const uint32_t dd_var = dd_node == NULL ? 0xfffff : tbddnode_getvariable(dd_node);
        const uint32_t dd_tag = TBDD_GETTAG(dd);

        TBDD dd0, dd1;
        if (dom_var < dd_tag) {
            dd0 = dd1 = dd;
        } else if (dom_var < dd_var) {
            dd0 = tbdd_settag(dd, dom_next_var);
            dd1 = tbdd_false;
        } else {
            dd0 = tbddnode_low(dd, dd_node);
            dd1 = tbddnode_high(dd, dd_node);
        }

        /**
         * Call recursive functions
         */
        struct tbdd_trace t0 = (struct tbdd_trace){trace, dom_var, 0};
        struct tbdd_trace t1 = (struct tbdd_trace){trace, dom_var, 1};
        SPAWN(tbdd_enum_do, dd0, dom_next, cb, ctx, &t0);
        CALL(tbdd_enum_do, dd1, dom_next, cb, ctx, &t1);
        SYNC(tbdd_enum_do);
    }
}

VOID_TASK_IMPL_4(tbdd_enum, TBDD, dd, TBDD, dom, tbdd_enum_cb, cb, void*, ctx)
{
    CALL(tbdd_enum_do, dd, dom, cb, ctx, NULL);
}

VOID_TASK_5(tbdd_enum_seq_do, TBDD, dd, TBDD, dom, tbdd_enum_cb, cb, void*, ctx, tbdd_trace_t, trace)
{
    if (dd == tbdd_false) {
        return;
    } else if (dom == tbdd_true) {
        /**
         * Compute trace length
         */
        size_t len = 0;
        tbdd_trace_t p = trace;
        while (p != NULL) {
            len++;
            p = p->prev;
        }
        /**
         * Fill array
         */
        uint8_t arr[len];
        for (size_t i=0; i<len; i++) {
            arr[len-i-1] = trace->val;
            trace = trace->prev;
        }
        /**
         * Call callback
         */
        WRAP(cb, ctx, arr, len);
    } else {
        /**
         * Obtain domain variable
         */
        const tbddnode_t dom_node = TBDD_GETNODE(dom);
        const uint32_t dom_var = tbddnode_getvariable(dom_node);
        const TBDD dom_next = tbddnode_high(dom, dom_node);
        const uint32_t dom_next_var = dom_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(dom_next));

        /**
         * Obtain cofactors
         */
        const tbddnode_t dd_node = TBDD_NOTAG(dd) == tbdd_true ? NULL : TBDD_GETNODE(dd);
        const uint32_t dd_var = dd_node == NULL ? 0xfffff : tbddnode_getvariable(dd_node);
        const uint32_t dd_tag = TBDD_GETTAG(dd);

        TBDD dd0, dd1;
        if (dom_var < dd_tag) {
            dd0 = dd1 = dd;
        } else if (dom_var < dd_var) {
            dd0 = tbdd_settag(dd, dom_next_var);
            dd1 = tbdd_false;
        } else {
            dd0 = tbddnode_low(dd, dd_node);
            dd1 = tbddnode_high(dd, dd_node);
        }

        /**
         * Call recursive functions
         */
        struct tbdd_trace t0 = (struct tbdd_trace){trace, dom_var, 0};
        CALL(tbdd_enum_seq_do, dd0, dom_next, cb, ctx, &t0);
        struct tbdd_trace t1 = (struct tbdd_trace){trace, dom_var, 1};
        CALL(tbdd_enum_seq_do, dd1, dom_next, cb, ctx, &t1);
    }
}

VOID_TASK_IMPL_4(tbdd_enum_seq, TBDD, dd, TBDD, dom, tbdd_enum_cb, cb, void*, ctx)
{
    CALL(tbdd_enum_seq_do, dd, dom, cb, ctx, NULL);
}

TASK_6(TBDD, tbdd_collect_do, TBDD, dd, TBDD, dom, TBDD, res_dom, tbdd_collect_cb, cb, void*, ctx, tbdd_trace_t, trace)
{
    if (dd == tbdd_false) {
        return tbdd_false;
    } else if (dom == tbdd_true) {
        /**
         * Compute trace length
         */
        size_t len = 0;
        tbdd_trace_t p = trace;
        while (p != NULL) {
            len++;
            p = p->prev;
        }
        /**
         * Fill array
         */
        uint8_t arr[len];
        for (size_t i=0; i<len; i++) {
            arr[len-i-1] = trace->val;
            trace = trace->prev;
        }
        /**
         * Call callback
         */
        return WRAP(cb, ctx, arr, len);
    } else {
        /**
         * Obtain domain variable
         */
        const tbddnode_t dom_node = TBDD_GETNODE(dom);
        const uint32_t dom_var = tbddnode_getvariable(dom_node);
        const TBDD dom_next = tbddnode_high(dom, dom_node);
        const uint32_t dom_next_var = dom_next == tbdd_true ? 0xfffff : tbddnode_getvariable(TBDD_GETNODE(dom_next));

        /**
         * Obtain cofactors
         */
        const tbddnode_t dd_node = TBDD_NOTAG(dd) == tbdd_true ? NULL : TBDD_GETNODE(dd);
        const uint32_t dd_var = dd_node == NULL ? 0xfffff : tbddnode_getvariable(dd_node);
        const uint32_t dd_tag = TBDD_GETTAG(dd);

        TBDD dd0, dd1;
        if (dom_var < dd_tag) {
            dd0 = dd1 = dd;
        } else if (dom_var < dd_var) {
            dd0 = tbdd_settag(dd, dom_next_var);
            dd1 = tbdd_false;
        } else {
            dd0 = tbddnode_low(dd, dd_node);
            dd1 = tbddnode_high(dd, dd_node);
        }

        /**
         * Call recursive functions
         */
        struct tbdd_trace t0 = (struct tbdd_trace){trace, dom_var, 0};
        struct tbdd_trace t1 = (struct tbdd_trace){trace, dom_var, 1};
        tbdd_refs_spawn(SPAWN(tbdd_collect_do, dd0, dom_next, res_dom, cb, ctx, &t0));
        TBDD high = CALL(tbdd_collect_do, dd1, dom_next, res_dom, cb, ctx, &t1);
        tbdd_refs_push(high);
        TBDD low = tbdd_refs_sync(SYNC(tbdd_collect_do));
        tbdd_refs_push(low);
        TBDD res = tbdd_or(low, high, res_dom);
        tbdd_refs_pop(2);
        return res;
    }
}

TASK_IMPL_5(TBDD, tbdd_collect, TBDD, dd, TBDD, dom, TBDD, res_dom, tbdd_collect_cb, cb, void*, ctx)
{
    return CALL(tbdd_collect_do, dd, dom, res_dom, cb, ctx, NULL);
}

/**
 * Helper function for recursive unmarking
 */
static void
tbdd_unmark_rec(TBDD tbdd)
{
    tbddnode_t n = TBDD_GETNODE(tbdd);
    if (!tbddnode_getmark(n)) return;
    tbddnode_setmark(n, 0);
    if (tbddnode_isleaf(n)) return;
    tbdd_unmark_rec(tbddnode_getlow(n));
    tbdd_unmark_rec(tbddnode_gethigh(n));
}

/**
 * Count number of nodes in TBDD
 */

static size_t
tbdd_nodecount_mark(TBDD tbdd)
{
    if (tbdd == tbdd_true) return 0; // do not count true/false leaf
    if (tbdd == tbdd_false) return 0; // do not count true/false leaf
    tbddnode_t n = TBDD_GETNODE(tbdd);
    if (tbddnode_getmark(n)) return 0;
    tbddnode_setmark(n, 1);
    if (tbddnode_isleaf(n)) return 1; // count leaf as 1
    return 1 + tbdd_nodecount_mark(tbddnode_getlow(n)) + tbdd_nodecount_mark(tbddnode_gethigh(n));
}

size_t
tbdd_nodecount_more(const TBDD *tbdds, size_t count)
{
    size_t result = 0, i;
    for (i=0; i<count; i++) result += tbdd_nodecount_mark(tbdds[i]);
    for (i=0; i<count; i++) tbdd_unmark_rec(tbdds[i]);
    return result;
}

/**
 * Export to .dot file
 */
static inline int tag_to_label(TBDD tbdd)
{
    uint32_t tag = TBDD_GETTAG(tbdd);
    if (tag == 0xfffff) return -1;
    else return (int)tag;
}

static void
tbdd_fprintdot_rec(FILE *out, TBDD tbdd)
{
    tbddnode_t n = TBDD_GETNODE(tbdd); // also works for tbdd_false
    if (tbddnode_getmark(n)) return;
    tbddnode_setmark(n, 1);

    if (TBDD_GETINDEX(tbdd) == 0) {  // tbdd == tbdd_true || tbdd == tbdd_false
        fprintf(out, "0 [shape=box, style=filled, label=\"F\"];\n");
    } else if (TBDD_GETINDEX(tbdd) == 1) {  // tbdd == tbdd_true || tbdd == tbdd_false
        fprintf(out, "1 [shape=box, style=filled, label=\"T\"];\n");
    } else if (tbddnode_isleaf(n)) {
        fprintf(out, "%" PRIu64 " [shape=box, style=filled, label=\"", TBDD_GETINDEX(tbdd));
        /* tbdd_fprint_leaf(out, tbdd); */  // TODO
        fprintf(out, "\"];\n");
    } else {
        fprintf(out, "%" PRIu64 " [label=\"%" PRIu32 "\\n%" PRIu64 "\"];\n",
                TBDD_GETINDEX(tbdd), tbddnode_getvariable(n), TBDD_GETINDEX(tbdd));

        tbdd_fprintdot_rec(out, tbddnode_getlow(n));
        tbdd_fprintdot_rec(out, tbddnode_gethigh(n));

        fprintf(out, "%" PRIu64 " -> %" PRIu64 " [style=dashed, label=\" %d\"];\n",
                TBDD_GETINDEX(tbdd), TBDD_GETINDEX(tbddnode_getlow(n)),
                tag_to_label(tbddnode_getlow(n)));
        fprintf(out, "%" PRIu64 " -> %" PRIu64 " [style=solid dir=both arrowtail=%s, label=\" %d\"];\n",
                TBDD_GETINDEX(tbdd), TBDD_GETINDEX(tbddnode_gethigh(n)),
                tbddnode_getcomp(n) ? "dot" : "none", tag_to_label(tbddnode_gethigh(n)));
    }
}

void
tbdd_fprintdot(FILE *out, TBDD tbdd)
{
    fprintf(out, "digraph \"DD\" {\n");
    fprintf(out, "graph [dpi = 300];\n");
    fprintf(out, "center = true;\n");
    fprintf(out, "edge [dir = forward];\n");
    fprintf(out, "root [style=invis];\n");
    fprintf(out, "root -> %" PRIu64 " [style=solid dir=both arrowtail=%s label=\" %d\"];\n",
            TBDD_GETINDEX(tbdd), TBDD_HASMARK(tbdd) ? "dot" : "none", tag_to_label(tbdd));

    tbdd_fprintdot_rec(out, tbdd);
    tbdd_unmark_rec(tbdd);

    fprintf(out, "}\n");
}
