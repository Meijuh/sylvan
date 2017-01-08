#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <inttypes.h>

#include "llmsset.h"
#include "sylvan.h"
#include "test_assert.h"
#include "sylvan_int.h"

__thread uint64_t seed = 1;

uint64_t
xorshift_rand(void)
{
    uint64_t x = seed;
    if (seed == 0) seed = rand();
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    seed = x;
    return x * 2685821657736338717LL;
}

double
uniform_deviate(uint64_t seed)
{
    return seed * (1.0 / (0xffffffffffffffffL + 1.0));
}

int
rng(int low, int high)
{
    return low + uniform_deviate(xorshift_rand()) * (high-low);
}

static int
test_cache()
{
    test_assert(cache_getused() == 0);

    /**
     * Test cache for large number of random entries
     */

    size_t number_add = 4000000;
    uint64_t *arr = (uint64_t*)malloc(sizeof(uint64_t)*4*number_add);
    for (size_t i=0; i<number_add*4; i++) arr[i] = xorshift_rand();
    for (size_t i=0; i<number_add; i++) {
        test_assert(cache_put(arr[4*i], arr[4*i+1], arr[4*i+2], arr[4*i+3]));
        uint64_t val;
        int res = cache_get(arr[4*i], arr[4*i+1], arr[4*i+2], &val);
        test_assert(res == 1);
        test_assert(val == arr[4*i+3]);
    }
    size_t count = 0;
    for (size_t i=0; i<number_add; i++) {
        uint64_t val;
        int res = cache_get(arr[4*i], arr[4*i+1], arr[4*i+2], &val);
        test_assert(res == 0 || val == arr[4*i+3]);
        if (res) count++;
    }
    test_assert(count == cache_getused());

    /**
     * Now also test for double entries
     */

    for (size_t i=0; i<number_add/2; i++) {
        test_assert(cache_put6(arr[8*i], arr[8*i+1], arr[8*i+2], arr[8*i+3], arr[8*i+4], arr[8*i+5], arr[8*i+6], arr[8*i+7]));
        uint64_t val1, val2;
        int res = cache_get6(arr[8*i], arr[8*i+1], arr[8*i+2], arr[8*i+3], arr[8*i+4], arr[8*i+5], &val1, &val2);
        test_assert(res == 1);
        test_assert(val1 == arr[8*i+6]);
        test_assert(val2 == arr[8*i+7]);
    }
    for (size_t i=0; i<number_add/2; i++) {
        uint64_t val1, val2;
        int res = cache_get6(arr[8*i], arr[8*i+1], arr[8*i+2], arr[8*i+3], arr[8*i+4], arr[8*i+5], &val1, &val2);
        test_assert(res == 0 || (val1 == arr[8*i+6] && val2 == arr[8*i+7]));
    }

    /**
     * And test that single entries are not corrupted
     */
    for (size_t i=0; i<number_add; i++) {
        uint64_t val;
        int res = cache_get(arr[4*i], arr[4*i+1], arr[4*i+2], &val);
        test_assert(res == 0 || val == arr[4*i+3]);
    }

    /**
     * TODO: multithreaded test
     */

    free(arr);
    return 0;
}

static inline BDD
make_random(int i, int j)
{
    if (i == j) return rng(0, 2) ? sylvan_true : sylvan_false;

    BDD yes = make_random(i+1, j);
    BDD no = make_random(i+1, j);
    BDD result = sylvan_invalid;

    switch(rng(0, 4)) {
    case 0:
        result = no;
        sylvan_deref(yes);
        break;
    case 1:
        result = yes;
        sylvan_deref(no);
        break;
    case 2:
        result = sylvan_ref(sylvan_makenode(i, yes, no));
        sylvan_deref(no);
        sylvan_deref(yes);
        break;
    case 3:
    default:
        result = sylvan_ref(sylvan_makenode(i, no, yes));
        sylvan_deref(no);
        sylvan_deref(yes);
        break;
    }

    return result;
}

static MDD
make_random_ldd_set(int depth, int maxvalue, int elements)
{
    uint32_t values[depth];
    MDD result = mtbdd_false; // empty set
    for (int i=0; i<elements; i++) {
        lddmc_refs_push(result);
        for (int j=0; j<depth; j++) {
            values[j] = rng(0, maxvalue);
        }
        result = lddmc_union_cube(result, values, depth);
        lddmc_refs_pop(1);
    }
    return result;
}

int testEqual(BDD a, BDD b)
{
	if (a == b) return 1;

	if (a == sylvan_invalid) {
		fprintf(stderr, "a is invalid!\n");
		return 0;
	}

	if (b == sylvan_invalid) {
		fprintf(stderr, "b is invalid!\n");
		return 0;
	}

    fprintf(stderr, "a and b are not equal!\n");

    sylvan_fprint(stderr, a);fprintf(stderr, "\n");
    sylvan_fprint(stderr, b);fprintf(stderr, "\n");

	return 0;
}

int
test_bdd()
{
    test_assert(sylvan_makenode(sylvan_ithvar(1), sylvan_true, sylvan_true) == sylvan_not(sylvan_makenode(sylvan_ithvar(1), sylvan_false, sylvan_false)));
    test_assert(sylvan_makenode(sylvan_ithvar(1), sylvan_false, sylvan_true) == sylvan_not(sylvan_makenode(sylvan_ithvar(1), sylvan_true, sylvan_false)));
    test_assert(sylvan_makenode(sylvan_ithvar(1), sylvan_true, sylvan_false) == sylvan_not(sylvan_makenode(sylvan_ithvar(1), sylvan_false, sylvan_true)));
    test_assert(sylvan_makenode(sylvan_ithvar(1), sylvan_false, sylvan_false) == sylvan_not(sylvan_makenode(sylvan_ithvar(1), sylvan_true, sylvan_true)));

    return 0;
}

int
test_cube()
{
    LACE_ME;
    const BDDSET vars = sylvan_set_fromarray(((BDDVAR[]){1,2,3,4,6,8}), 6);

    uint8_t cube[6], check[6];
    int i, j;
    for (i=0;i<6;i++) cube[i] = rng(0,3);
    BDD bdd = sylvan_cube(vars, cube);

    sylvan_sat_one(bdd, vars, check);
    for (i=0; i<6;i++) test_assert(cube[i] == check[i] || (cube[i] == 2 && check[i] == 0));

    BDD picked_single = sylvan_pick_single_cube(bdd, vars);
    test_assert(testEqual(sylvan_and(picked_single, bdd), picked_single));
    assert(sylvan_satcount(picked_single, vars)==1);

    BDD picked = sylvan_pick_cube(bdd);
    test_assert(testEqual(sylvan_and(picked, bdd), picked));

    BDD t1 = sylvan_cube(vars, ((uint8_t[]){1,1,2,2,0,0}));
    BDD t2 = sylvan_cube(vars, ((uint8_t[]){1,1,1,0,0,2}));
    test_assert(testEqual(sylvan_union_cube(t1, vars, ((uint8_t[]){1,1,1,0,0,2})), sylvan_or(t1, t2)));
    t2 = sylvan_cube(vars, ((uint8_t[]){2,2,2,1,1,0}));
    test_assert(testEqual(sylvan_union_cube(t1, vars, ((uint8_t[]){2,2,2,1,1,0})), sylvan_or(t1, t2)));
    t2 = sylvan_cube(vars, ((uint8_t[]){1,1,1,0,0,0}));
    test_assert(testEqual(sylvan_union_cube(t1, vars, ((uint8_t[]){1,1,1,0,0,0})), sylvan_or(t1, t2)));

    bdd = make_random(1, 16);
    for (j=0;j<10;j++) {
        for (i=0;i<6;i++) cube[i] = rng(0,3);
        BDD c = sylvan_cube(vars, cube);
        test_assert(sylvan_union_cube(bdd, vars, cube) == sylvan_or(bdd, c));
    }

    for (i=0;i<10;i++) {
        picked = sylvan_pick_cube(bdd);
        test_assert(testEqual(sylvan_and(picked, bdd), picked));
    }

    // simple test for mtbdd_enum_all
    uint8_t arr[6];
    MTBDD leaf = mtbdd_enum_all_first(mtbdd_true, vars, arr, NULL);
    test_assert(leaf == mtbdd_true);
    test_assert(mtbdd_enum_all_first(mtbdd_true, vars, arr, NULL) == mtbdd_true);
    test_assert(arr[0] == 0 && arr[1] == 0 && arr[2] == 0 && arr[3] == 0 && arr[4] == 0 && arr[5] == 0);
    test_assert(mtbdd_enum_all_next(mtbdd_true, vars, arr, NULL) == mtbdd_true);
    test_assert(arr[0] == 0 && arr[1] == 0 && arr[2] == 0 && arr[3] == 0 && arr[4] == 0 && arr[5] == 1);
    test_assert(mtbdd_enum_all_next(mtbdd_true, vars, arr, NULL) == mtbdd_true);
    test_assert(arr[0] == 0 && arr[1] == 0 && arr[2] == 0 && arr[3] == 0 && arr[4] == 1 && arr[5] == 0);
    test_assert(mtbdd_enum_all_next(mtbdd_true, vars, arr, NULL) == mtbdd_true);
    test_assert(arr[0] == 0 && arr[1] == 0 && arr[2] == 0 && arr[3] == 0 && arr[4] == 1 && arr[5] == 1);
    test_assert(mtbdd_enum_all_next(mtbdd_true, vars, arr, NULL) == mtbdd_true);
    test_assert(arr[0] == 0 && arr[1] == 0 && arr[2] == 0 && arr[3] == 1 && arr[4] == 0 && arr[5] == 0);
    test_assert(mtbdd_enum_all_next(mtbdd_true, vars, arr, NULL) == mtbdd_true);
    test_assert(arr[0] == 0 && arr[1] == 0 && arr[2] == 0 && arr[3] == 1 && arr[4] == 0 && arr[5] == 1);
    test_assert(mtbdd_enum_all_next(mtbdd_true, vars, arr, NULL) == mtbdd_true);
    test_assert(arr[0] == 0 && arr[1] == 0 && arr[2] == 0 && arr[3] == 1 && arr[4] == 1 && arr[5] == 0);

    mtbdd_enum_all_first(mtbdd_true, vars, arr, NULL);
    size_t count = 1;
    while (mtbdd_enum_all_next(mtbdd_true, vars, arr, NULL) != mtbdd_false) {
        test_assert(count < 64);
        count++;
    }
    test_assert(count == 64);

    return 0;
}

static int
test_operators()
{
    // We need to test: xor, and, or, nand, nor, imp, biimp, invimp, diff, less
    LACE_ME;

    //int i;
    BDD a = sylvan_ithvar(1);
    BDD b = sylvan_ithvar(2);
    BDD one = make_random(1, 12);
    BDD two = make_random(6, 24);

    // Test or
    test_assert(testEqual(sylvan_or(a, b), sylvan_makenode(1, b, sylvan_true)));
    test_assert(testEqual(sylvan_or(a, b), sylvan_or(b, a)));
    test_assert(testEqual(sylvan_or(one, two), sylvan_or(two, one)));

    // Test and
    test_assert(testEqual(sylvan_and(a, b), sylvan_makenode(1, sylvan_false, b)));
    test_assert(testEqual(sylvan_and(a, b), sylvan_and(b, a)));
    test_assert(testEqual(sylvan_and(one, two), sylvan_and(two, one)));

    // Test xor
    test_assert(testEqual(sylvan_xor(a, b), sylvan_makenode(1, b, sylvan_not(b))));
    test_assert(testEqual(sylvan_xor(a, b), sylvan_xor(a, b)));
    test_assert(testEqual(sylvan_xor(a, b), sylvan_xor(b, a)));
    test_assert(testEqual(sylvan_xor(one, two), sylvan_xor(two, one)));
    test_assert(testEqual(sylvan_xor(a, b), sylvan_ite(a, sylvan_not(b), b)));

    // Test diff
    test_assert(testEqual(sylvan_diff(a, b), sylvan_diff(a, b)));
    test_assert(testEqual(sylvan_diff(a, b), sylvan_diff(a, sylvan_and(a, b))));
    test_assert(testEqual(sylvan_diff(a, b), sylvan_and(a, sylvan_not(b))));
    test_assert(testEqual(sylvan_diff(a, b), sylvan_ite(b, sylvan_false, a)));
    test_assert(testEqual(sylvan_diff(one, two), sylvan_diff(one, two)));
    test_assert(testEqual(sylvan_diff(one, two), sylvan_diff(one, sylvan_and(one, two))));
    test_assert(testEqual(sylvan_diff(one, two), sylvan_and(one, sylvan_not(two))));
    test_assert(testEqual(sylvan_diff(one, two), sylvan_ite(two, sylvan_false, one)));

    // Test biimp
    test_assert(testEqual(sylvan_biimp(a, b), sylvan_makenode(1, sylvan_not(b), b)));
    test_assert(testEqual(sylvan_biimp(a, b), sylvan_biimp(b, a)));
    test_assert(testEqual(sylvan_biimp(one, two), sylvan_biimp(two, one)));

    // Test nand / and
    test_assert(testEqual(sylvan_not(sylvan_and(a, b)), sylvan_nand(b, a)));
    test_assert(testEqual(sylvan_not(sylvan_and(one, two)), sylvan_nand(two, one)));

    // Test nor / or
    test_assert(testEqual(sylvan_not(sylvan_or(a, b)), sylvan_nor(b, a)));
    test_assert(testEqual(sylvan_not(sylvan_or(one, two)), sylvan_nor(two, one)));

    // Test xor / biimp
    test_assert(testEqual(sylvan_xor(a, b), sylvan_not(sylvan_biimp(b, a))));
    test_assert(testEqual(sylvan_xor(one, two), sylvan_not(sylvan_biimp(two, one))));

    // Test imp
    test_assert(testEqual(sylvan_imp(a, b), sylvan_ite(a, b, sylvan_true)));
    test_assert(testEqual(sylvan_imp(one, two), sylvan_ite(one, two, sylvan_true)));
    test_assert(testEqual(sylvan_imp(one, two), sylvan_not(sylvan_diff(one, two))));
    test_assert(testEqual(sylvan_invimp(one, two), sylvan_not(sylvan_less(one, two))));
    test_assert(testEqual(sylvan_imp(a, b), sylvan_invimp(b, a)));
    test_assert(testEqual(sylvan_imp(one, two), sylvan_invimp(two, one)));

    return 0;
}

int
test_relprod()
{
    LACE_ME;

    BDDVAR vars[] = {0,2,4};
    BDDVAR all_vars[] = {0,1,2,3,4,5};

    BDDSET vars_set = sylvan_set_fromarray(vars, 3);
    BDDSET all_vars_set = sylvan_set_fromarray(all_vars, 6);

    BDD s, t, next, prev;
    BDD zeroes, ones;

    // transition relation: 000 --> 111 and !000 --> 000
    t = sylvan_false;
    t = sylvan_union_cube(t, all_vars_set, ((uint8_t[]){0,1,0,1,0,1}));
    t = sylvan_union_cube(t, all_vars_set, ((uint8_t[]){1,0,2,0,2,0}));
    t = sylvan_union_cube(t, all_vars_set, ((uint8_t[]){2,0,1,0,2,0}));
    t = sylvan_union_cube(t, all_vars_set, ((uint8_t[]){2,0,2,0,1,0}));

    s = sylvan_cube(vars_set, (uint8_t[]){0,0,1});
    zeroes = sylvan_cube(vars_set, (uint8_t[]){0,0,0});
    ones = sylvan_cube(vars_set, (uint8_t[]){1,1,1});

    next = sylvan_relnext(s, t, all_vars_set);
    prev = sylvan_relprev(t, next, all_vars_set);
    test_assert(next == zeroes);
    test_assert(prev == sylvan_not(zeroes));

    next = sylvan_relnext(next, t, all_vars_set);
    prev = sylvan_relprev(t, next, all_vars_set);
    test_assert(next == ones);
    test_assert(prev == zeroes);

    t = sylvan_cube(all_vars_set, (uint8_t[]){0,0,0,0,0,1});
    test_assert(sylvan_relprev(t, s, all_vars_set) == zeroes);
    test_assert(sylvan_relprev(t, sylvan_not(s), all_vars_set) == sylvan_false);
    test_assert(sylvan_relnext(s, t, all_vars_set) == sylvan_false);
    test_assert(sylvan_relnext(zeroes, t, all_vars_set) == s);

    t = sylvan_cube(all_vars_set, (uint8_t[]){0,0,0,0,0,2});
    test_assert(sylvan_relprev(t, s, all_vars_set) == zeroes);
    test_assert(sylvan_relprev(t, zeroes, all_vars_set) == zeroes);
    test_assert(sylvan_relnext(sylvan_not(zeroes), t, all_vars_set) == sylvan_false);

    return 0;
}

int
test_compose()
{
    LACE_ME;

    BDD a = sylvan_ithvar(1);
    BDD b = sylvan_ithvar(2);

    BDD a_or_b = sylvan_or(a, b);

    BDD one = make_random(3, 16);
    BDD two = make_random(8, 24);

    BDDMAP map = sylvan_map_empty();

    map = sylvan_map_add(map, 1, one);
    map = sylvan_map_add(map, 2, two);

    test_assert(sylvan_map_key(map) == 1);
    test_assert(sylvan_map_value(map) == one);
    test_assert(sylvan_map_key(sylvan_map_next(map)) == 2);
    test_assert(sylvan_map_value(sylvan_map_next(map)) == two);

    test_assert(testEqual(one, sylvan_compose(a, map)));
    test_assert(testEqual(two, sylvan_compose(b, map)));

    test_assert(testEqual(sylvan_or(one, two), sylvan_compose(a_or_b, map)));

    map = sylvan_map_add(map, 2, one);
    test_assert(testEqual(sylvan_compose(a_or_b, map), one));

    map = sylvan_map_add(map, 1, two);
    test_assert(testEqual(sylvan_or(one, two), sylvan_compose(a_or_b, map)));

    test_assert(testEqual(sylvan_and(one, two), sylvan_compose(sylvan_and(a, b), map)));

    // test that composing [0:=true] on "0" yields true
    map = sylvan_map_add(sylvan_map_empty(), 1, sylvan_true);
    test_assert(testEqual(sylvan_compose(a, map), sylvan_true));

    // test that composing [0:=false] on "0" yields false
    map = sylvan_map_add(sylvan_map_empty(), 1, sylvan_false);
    test_assert(testEqual(sylvan_compose(a, map), sylvan_false));

    return 0;
}

int
test_ldd()
{
    // very basic testing of makenode
    for (int i=0; i<10; i++) {
        uint32_t value = rng(0, 100);
        MDD m = lddmc_makenode(value, lddmc_true, lddmc_false);
        test_assert(lddmc_getvalue(m) == value);
        test_assert(lddmc_getdown(m) == lddmc_true);
        test_assert(lddmc_getright(m) == lddmc_false);
        test_assert(lddmc_iscopy(m) == 0);
        test_assert(lddmc_follow(m, value) == lddmc_true);
        for (int j=0; j<100; j++) {
            uint32_t other_value = rng(0, 100);
            if (value != other_value) test_assert(lddmc_follow(m, other_value) == lddmc_false);
        }
    }

    // test handling of the copy node by primitives
    MDD m = lddmc_make_copynode(lddmc_true, lddmc_false);
    test_assert(lddmc_iscopy(m) == 1);
    test_assert(lddmc_getvalue(m) == 0);
    test_assert(lddmc_getdown(m) == lddmc_true);
    test_assert(lddmc_getright(m) == lddmc_false);
    m = lddmc_extendnode(m, 0, lddmc_true);
    test_assert(lddmc_iscopy(m) == 1);
    test_assert(lddmc_getvalue(m) == 0);
    test_assert(lddmc_getdown(m) == lddmc_true);
    test_assert(lddmc_getright(m) != lddmc_false);
    test_assert(lddmc_follow(m, 0) == lddmc_true);
    test_assert(lddmc_getvalue(lddmc_getright(m)) == 0);
    test_assert(lddmc_iscopy(lddmc_getright(m)) == 0);
    test_assert(lddmc_makenode(0, lddmc_true, lddmc_false) == lddmc_getright(m));

    LACE_ME;
    // test union_cube
    for (int i=0; i<100; i++) {
        int depth = rng(1, 6);
        int elements = rng(1, 30);
        m = make_random_ldd_set(depth, 10, elements);
        assert(m != lddmc_true);
        assert(m != lddmc_false);
        assert(lddmc_satcount(m) <= elements);
        assert(lddmc_satcount(m) >= 1);
    }

    // test simply transition relation
    {
        MDD states, rel, meta, expected;

        // relation: (0,0) to (1,1)
        rel = lddmc_cube((uint32_t[]){0,1,0,1}, 4);
        test_assert(lddmc_satcount(rel) == 1);
        // relation: (0,0) to (2,2)
        rel = lddmc_union_cube(rel, (uint32_t[]){0,2,0,2}, 4);
        test_assert(lddmc_satcount(rel) == 2);
        // meta: read write read write
        meta = lddmc_cube((uint32_t[]){1,2,1,2}, 4);
        test_assert(lddmc_satcount(meta) == 1);
        // initial state: (0,0)
        states = lddmc_cube((uint32_t[]){0,0}, 2);
        test_assert(lddmc_satcount(states) == 1);
        // relprod should give two states
        states = lddmc_relprod(states, rel, meta);
        test_assert(lddmc_satcount(states) == 2);
        // relprod should give states (1,1) and (2,2)
        expected = lddmc_cube((uint32_t[]){1,1}, 2);
        expected = lddmc_union_cube(expected, (uint32_t[]){2,2}, 2);
        test_assert(states == expected);

        // now test relprod union on the simple example
        states = lddmc_cube((uint32_t[]){0,0}, 2);
        states = lddmc_relprod_union(states, rel, meta, states);
        test_assert(lddmc_satcount(states) == 3);
        test_assert(states == lddmc_union(states, expected));

        // now create transition (1,1) --> (1,1) (using copy nodes)
        rel = lddmc_cube_copy((uint32_t[]){1,0,1,0}, (int[]){0,1,0,1}, 4);
        states = lddmc_relprod(states, rel, meta);
        // the result should be just state (1,1)
        test_assert(states == lddmc_cube((uint32_t[]){1,1}, 2));

        MDD statezero = lddmc_cube((uint32_t[]){0,0}, 2);
        states = lddmc_union_cube(statezero, (uint32_t[]){1,1}, 2);
        test_assert(lddmc_relprod_union(states, rel, meta, statezero) == states);

        // now create transition (*,*) --> (*,*) (copy nodes)
        rel = lddmc_cube_copy((uint32_t[]){0,0}, (int[]){1,1}, 2);
        meta = lddmc_cube((uint32_t[]){4,4}, 2);
        states = make_random_ldd_set(2, 10, 10);
        MDD states2 = make_random_ldd_set(2, 10, 10);
        test_assert(lddmc_union(states, states2) == lddmc_relprod_union(states, rel, meta, states2));
    }

    return 0;
}

uint8_t **enum_arrs;
size_t enum_len;
int enum_idx;
int enum_max;

VOID_TASK_3(test_tbdd_enum_cb, void*, ctx, uint8_t*, arr, size_t, len)
{
    assert(len == enum_len);
    assert(enum_idx != enum_max);
    assert(memcmp(arr, enum_arrs[enum_idx++], len) == 0);
    (void)ctx;
    (void)arr;
    (void)len;
}

int
test_tbdd()
{
    LACE_ME;

    int test_iterations = 1000;

    {
        /**
         * Test tbdd_from_mtbdd
         */

        BDD dom = mtbdd_fromarray((uint32_t[]){0,1,2,3,4,5,6}, 7);
        BDD dd = mtbdd_cube(dom, (uint8_t[]){0,0,2,2,0,2,0}, mtbdd_true);
        TBDD tbdd = tbdd_from_mtbdd(dd, dom);

        test_assert(tbdd_from_mtbdd(dom, dom) == tbdd_from_array((uint32_t[]){0,1,2,3,4,5,6}, 7));

        /**
         * We should now have:
         * Edge tagged 0 to node X
         * Node X with variable 2 and two edges tagged 4 to node Y
         * Node Y with variable 5 and two edges tagged 6, high to False, low to True
         */

        /**
         * We just test if it evaluates correctly.
         */

        test_assert(tbdd_eval(tbdd, 0, 1, 1) == tbdd_false);
        test_assert(tbdd_eval(tbdd, 0, 0, 1) != tbdd_false);
        tbdd = tbdd_eval(tbdd, 0, 0, 1);
        test_assert(tbdd_eval(tbdd, 1, 1, 2) == tbdd_false);
        test_assert(tbdd_eval(tbdd, 1, 0, 2) != tbdd_false);
        tbdd = tbdd_eval(tbdd, 1, 0, 2);
        test_assert(tbdd_eval(tbdd, 2, 1,3 ) == tbdd_eval(tbdd, 2, 0, 3));
        tbdd = tbdd_eval(tbdd, 2, 0, 3);
        test_assert(tbdd_eval(tbdd, 3, 1,4 ) == tbdd_eval(tbdd, 3, 0, 4));
        tbdd = tbdd_eval(tbdd, 3, 1, 4);
        test_assert(tbdd_eval(tbdd, 4, 1, 5) == tbdd_false);
        test_assert(tbdd_eval(tbdd, 4, 0, 5) != tbdd_false);
        tbdd = tbdd_eval(tbdd, 4, 0, 5);
        test_assert(tbdd_eval(tbdd, 5, 1, 6) == tbdd_eval(tbdd, 5, 0, 6));
        tbdd = tbdd_eval(tbdd, 5, 0, 6);
        test_assert(tbdd_eval(tbdd, 6, 1, 0xfffff) == tbdd_false);
        test_assert(tbdd_eval(tbdd, 6, 0, 0xfffff) != tbdd_false);

        /**
         * Test that makenode correctly creates a (k,k) node
         */
        TBDD a = tbdd_ithvar(8);
        a = tbdd_makenode(3, a, tbdd_false, 7);
        test_assert(tbdd_eval(a, 3, 1, 4) == tbdd_false);
        test_assert(tbdd_eval(a, 3, 0, 4) != tbdd_false);
        test_assert(tbdd_getvar(a) == 7);
        test_assert(tbdd_getlow(a) == tbdd_gethigh(a));
    }

    for (int i=0; i<test_iterations; i++) {
        /**
         * Test tbdd_ithvar
         */

        uint32_t var = rng(0, 0xffffe);
        TBDD a = tbdd_makenode(var, tbdd_false, tbdd_true, 0xfffff);
        test_assert(a == tbdd_ithvar(var));
        test_assert(a == tbdd_from_mtbdd(sylvan_ithvar(var), sylvan_ithvar(var)));
    }

    // printf("Testing from_mtbdd/to_mtbdd...\n");

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test tbdd_from_mtbdd and tbdd_to_mtbdd with random sets
         */

        BDD bdd_dom = mtbdd_fromarray((uint32_t[]){0,1,2,3,4,5,6,7}, 8);
        TBDD tbdd_dom = tbdd_from_array((uint32_t[]){0,1,2,3,4,5,6,7}, 8);

        test_assert(tbdd_from_mtbdd(bdd_dom, bdd_dom) == tbdd_dom);

        int count = rng(0,100);
        for (int i=0; i<count; i++) {
            uint8_t arr[8];
            for (int j=0; j<8; j++) arr[j] = rng(0, 2);
            BDD bdd_set = sylvan_cube(bdd_dom, arr);
            TBDD tbdd_set = tbdd_cube(tbdd_dom, arr);
            TBDD tbdd_test = tbdd_from_mtbdd(bdd_set, bdd_dom);
            test_assert(tbdd_test == tbdd_set);
            test_assert(tbdd_to_mtbdd(tbdd_test, tbdd_dom) == bdd_set);
        }
    }

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test tbdd_extend_domain with random sets
         */

        // Create random domain of 6..14 variables
        int nvars = rng(20,50);

        // Create random subdomain 1
        uint32_t subdom1_arr[nvars];
        int nsub1 = 0;
        for (int i=0; i<nvars; i++) if (rng(0,2)) subdom1_arr[nsub1++] = i;
        BDD bdd_subdom1 = mtbdd_fromarray(subdom1_arr, nsub1);
        TBDD tbdd_subdom1 = tbdd_from_array(subdom1_arr, nsub1);
        test_assert(tbdd_subdom1 == tbdd_from_mtbdd(bdd_subdom1, bdd_subdom1));

        // Create random subdomain 2
        uint32_t subdom2_arr[nvars];
        int nsub2 = 0;
        for (int i=0; i<nvars; i++) if (rng(0,2)) subdom2_arr[nsub2++] = i;
        BDD bdd_subdom2 = mtbdd_fromarray(subdom2_arr, nsub2);
        TBDD tbdd_subdom2 = tbdd_from_array(subdom2_arr, nsub2);
        test_assert(tbdd_subdom2 == tbdd_from_mtbdd(bdd_subdom2, bdd_subdom2));

        // combine subdomains
        BDD bdd_subdom = sylvan_and(bdd_subdom1, bdd_subdom2);
        TBDD tbdd_subdom = tbdd_merge_domains(tbdd_subdom1, tbdd_subdom2);
        test_assert(tbdd_subdom == tbdd_from_mtbdd(bdd_subdom, bdd_subdom));
    }

    // printf("Testing cube...\n");

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test tbdd_cube with random sets
         * This also tests tbdd_from_mtbdd...
         */

        BDD bdd_dom = mtbdd_fromarray((uint32_t[]){0,1,2,3,4,5,6,7}, 8);
        TBDD tbdd_dom = tbdd_from_array((uint32_t[]){0,1,2,3,4,5,6,7}, 8);

        test_assert(tbdd_from_mtbdd(bdd_dom, bdd_dom) == tbdd_dom);

        int count = rng(0,100);
        for (int i=0; i<count; i++) {
            uint8_t arr[8];
            for (int j=0; j<8; j++) arr[j] = rng(0, 3);
            BDD bdd_set = sylvan_cube(bdd_dom, arr);
            TBDD tbdd_set = tbdd_cube(tbdd_dom, arr);
            test_assert(tbdd_from_mtbdd(bdd_set, bdd_dom) == tbdd_set);
        }
    }

    // printf("Testing union_cube...\n");

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test tbdd_union_cube with random sets
         * This also tests tbdd_from_mtbdd...
         */

        BDD bdd_dom = mtbdd_fromarray((uint32_t[]){0,1,2,3,4,5,6,7}, 8);
        TBDD tbdd_dom = tbdd_from_array((uint32_t[]){0,1,2,3,4,5,6,7}, 8);

        test_assert(tbdd_from_mtbdd(bdd_dom, bdd_dom) == tbdd_dom);

        BDD bdd_set = sylvan_false;
        TBDD tbdd_set = tbdd_false;
        int count = rng(0,40);
        for (int i=0; i<count; i++) {
            uint8_t arr[8];
            for (int j=0; j<8; j++) arr[j] = rng(0, 3);
            bdd_set = sylvan_union_cube(bdd_set, bdd_dom, arr);
            tbdd_set = tbdd_union_cube(tbdd_set, tbdd_dom, arr);
            test_assert(tbdd_from_mtbdd(bdd_set, bdd_dom) == tbdd_set);
        }
    }

    // printf("Testing extend_domain...\n");

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test tbdd_extend_domain with random sets
         */

        // Create random domain of 6..14 variables
        int nvars = rng(6,14);
        uint32_t dom_arr[nvars];
        for (int i=0; i<nvars; i++) dom_arr[i] = i;
        BDD bdd_dom = mtbdd_fromarray(dom_arr, nvars);
        TBDD tbdd_dom = tbdd_from_array(dom_arr, nvars);
        test_assert(tbdd_dom == tbdd_from_mtbdd(bdd_dom, bdd_dom));

        // Create random subdomain
        uint32_t subdom_arr[nvars];
        int nsub = 0;
        for (int i=0; i<nvars; i++) if (rng(0,2)) subdom_arr[nsub++] = i;
        BDD bdd_subdom = mtbdd_fromarray(subdom_arr, nsub);
        TBDD tbdd_subdom = tbdd_from_array(subdom_arr, nsub);
        test_assert(tbdd_subdom == tbdd_from_mtbdd(bdd_subdom, bdd_subdom));

        // Create random set on subdomain
        BDD bdd_set = sylvan_false;
        TBDD tbdd_set = tbdd_false;
        {
            int count = rng(10,200);
            for (int i=0; i<count; i++) {
                uint8_t arr[nsub];
                for (int j=0; j<nsub; j++) arr[j] = rng(0, 2);
                bdd_set = sylvan_union_cube(bdd_set, bdd_subdom, arr);
                tbdd_set = tbdd_union_cube(tbdd_set, tbdd_subdom, arr);
            }
        }
        test_assert(tbdd_set == tbdd_from_mtbdd(bdd_set, bdd_subdom));

        TBDD tbdd_test_result = tbdd_extend_domain(tbdd_set, tbdd_subdom, tbdd_dom);
        test_assert(tbdd_test_result == tbdd_from_mtbdd(bdd_set, bdd_dom));
    }

    // printf("Testing satcount...\n");

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test tbdd_satcount with random sets
         * This also tests tbdd_from_mtbdd...
         */

        BDD bdd_dom = mtbdd_fromarray((uint32_t[]){0,1,2,3,4,5,6,7}, 8);

        int count = rng(0,100);
        BDD bdd_set = sylvan_false;
        for (int i=0; i<count; i++) {
            uint8_t arr[8];
            for (int j=0; j<8; j++) arr[j] = rng(0, 2);
            bdd_set = sylvan_union_cube(bdd_set, bdd_dom, arr);
        }

        TBDD tbdd_set = tbdd_from_mtbdd(bdd_set, bdd_dom);
        TBDD tbdd_dom = tbdd_from_mtbdd(bdd_dom, bdd_dom);

        test_assert((size_t)mtbdd_satcount(bdd_set, 8) == (size_t)tbdd_satcount(tbdd_set, tbdd_dom));
    }

    // printf("Testing enum...\n");

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test tbdd_enum with random sets
         */
        int nvars = rng(8,12);

        // Create random source set
        uint32_t dom_arr[nvars];
        for (int i=0; i<nvars; i++) dom_arr[i] = i*2;
        TBDD tbdd_dom = tbdd_from_array(dom_arr, nvars);

        TBDD tbdd_set = tbdd_false;
        int count = rng(4,100);
        for (int i=0; i<count; i++) {
            uint8_t arr[nvars];
            for (int j=0; j<nvars; j++) arr[j] = rng(0, 2);
            tbdd_set = tbdd_union_cube(tbdd_set, tbdd_dom, arr);
        }

        enum_max = tbdd_satcount(tbdd_set, tbdd_dom);
        enum_len = nvars;
        enum_idx = 0;

        uint8_t arr[nvars];
        enum_arrs = malloc(sizeof(uint8_t*[enum_max]));
        TBDD res = tbdd_enum_first(tbdd_set, tbdd_dom, arr);
        for (int i=0; i<enum_max; i++) {
            test_assert(res != tbdd_false);
            enum_arrs[i] = malloc(sizeof(uint8_t[nvars]));
            memcpy(enum_arrs[i], arr, nvars);
            res = tbdd_enum_next(tbdd_set, tbdd_dom, arr);
        }
        assert(res == tbdd_false);

        tbdd_enum_seq(tbdd_set, tbdd_dom, TASK(test_tbdd_enum_cb), NULL);
        for (int i=0; i<enum_max; i++) free(enum_arrs[i]);
        free(enum_arrs);
    }

    // printf("Testing and...\n");

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test tbdd_and with random sets
         */

        BDD bdd_dom = mtbdd_fromarray((uint32_t[]){0,1,2,3,4,5}, 6);

        BDD bdd_set_a = sylvan_false;
        BDD bdd_set_b = sylvan_false;

        int count = rng(0,100);
        for (int i=0; i<count; i++) {
            uint8_t arr[6];
            for (int j=0; j<6; j++) arr[j] = rng(0, 2);
            bdd_set_a = sylvan_union_cube(bdd_set_a, bdd_dom, arr);
            for (int j=0; j<6; j++) arr[j] = rng(0, 2);
            bdd_set_b = sylvan_union_cube(bdd_set_b, bdd_dom, arr);
        }

        BDD bdd_set = sylvan_and(bdd_set_a, bdd_set_b);

        TBDD tbdd_set_a = tbdd_from_mtbdd(bdd_set_a, bdd_dom);
        TBDD tbdd_set_b = tbdd_from_mtbdd(bdd_set_b, bdd_dom);
        TBDD tbdd_set = tbdd_from_mtbdd(bdd_set, bdd_dom);
        TBDD tbdd_dom = tbdd_from_mtbdd(bdd_dom, bdd_dom);

        TBDD tbdd_test_result = tbdd_and(tbdd_set_a, tbdd_set_b, tbdd_dom);
        
        test_assert(tbdd_set == tbdd_test_result);
    }

    // printf("Testing or...\n");

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test tbdd_or with random sets
         */

        BDD bdd_dom = mtbdd_fromarray((uint32_t[]){0,1,2,3,4,5}, 6);

        BDD bdd_set_a = sylvan_false;
        BDD bdd_set_b = sylvan_false;

        int count = rng(0,100);
        for (int i=0; i<count; i++) {
            uint8_t arr[6];
            for (int j=0; j<6; j++) arr[j] = rng(0, 2);
            bdd_set_a = sylvan_union_cube(bdd_set_a, bdd_dom, arr);
            for (int j=0; j<6; j++) arr[j] = rng(0, 2);
            bdd_set_b = sylvan_union_cube(bdd_set_b, bdd_dom, arr);
        }

        BDD bdd_set = sylvan_or(bdd_set_a, bdd_set_b);

        TBDD tbdd_set_a = tbdd_from_mtbdd(bdd_set_a, bdd_dom);
        TBDD tbdd_set_b = tbdd_from_mtbdd(bdd_set_b, bdd_dom);
        TBDD tbdd_set = tbdd_from_mtbdd(bdd_set, bdd_dom);
        TBDD tbdd_dom = tbdd_from_mtbdd(bdd_dom, bdd_dom);

        TBDD tbdd_test_result = tbdd_or(tbdd_set_a, tbdd_set_b, tbdd_dom);

        if (tbdd_set != tbdd_test_result) {
            // check each element
            for (int i=0; i<64; i++) {
                uint8_t arr[6];
                arr[0] = i & 0x1 ? 1 : 0;
                arr[1] = i & 0x2 ? 1 : 0;
                arr[2] = i & 0x4 ? 1 : 0;
                arr[3] = i & 0x8 ? 1 : 0;
                arr[4] = i & 0x10 ? 1 : 0;
                arr[5] = i & 0x20 ? 1 : 0;
                TBDD x = tbdd_set;
                for (int j=0; j<6; j++) x = tbdd_eval(x, j, arr[j], j == 5 ? 0xfffff : j+1);
                assert(x == tbdd_true || x == tbdd_false);
                TBDD y = tbdd_test_result;
                for (int j=0; j<6; j++) y = tbdd_eval(y, j, arr[j], j == 5 ? 0xfffff : j+1);
                assert(y == tbdd_true || y == tbdd_false);
            }
        }
        
        test_assert(tbdd_set == tbdd_test_result);
    }

    // printf("Testing not...\n");

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test negation with tbdd_ite
         */

        BDD bdd_dom = mtbdd_fromarray((uint32_t[]){0,1,2,3,4,5,6,7}, 8);

        int count = rng(0,100);
        BDD bdd_set = sylvan_false;
        for (int i=0; i<count; i++) {
            uint8_t arr[8];
            for (int j=0; j<8; j++) arr[j] = rng(0, 2);
            bdd_set = sylvan_union_cube(bdd_set, bdd_dom, arr);
        }

        TBDD tbdd_set = tbdd_from_mtbdd(bdd_set, bdd_dom);
        TBDD tbdd_set_inv = tbdd_from_mtbdd(sylvan_not(bdd_set), bdd_dom);
        TBDD tbdd_dom = tbdd_from_mtbdd(bdd_dom, bdd_dom);

        test_assert((size_t)mtbdd_satcount(sylvan_not(bdd_set), 8) == (size_t)tbdd_satcount(tbdd_set_inv, tbdd_dom));
        test_assert(tbdd_set_inv == tbdd_ite(tbdd_set, tbdd_false, tbdd_true, tbdd_dom));
        test_assert(tbdd_set_inv == tbdd_not(tbdd_set, tbdd_dom));
    }

    // printf("Testing ite...\n");

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test tbdd_ite, first by comparing with tbdd_and
         */

        BDD bdd_dom = mtbdd_fromarray((uint32_t[]){0,1,2,3,4,5}, 6);

        BDD bdd_set_a = sylvan_false;
        BDD bdd_set_b = sylvan_false;

        int count = rng(0,100);
        for (int i=0; i<count; i++) {
            uint8_t arr[6];
            for (int j=0; j<6; j++) arr[j] = rng(0, 2);
            bdd_set_a = sylvan_union_cube(bdd_set_a, bdd_dom, arr);
            for (int j=0; j<6; j++) arr[j] = rng(0, 2);
            bdd_set_b = sylvan_union_cube(bdd_set_b, bdd_dom, arr);
        }

        BDD bdd_set = sylvan_and(bdd_set_a, bdd_set_b);

        TBDD tbdd_set_a = tbdd_from_mtbdd(bdd_set_a, bdd_dom);
        TBDD tbdd_set_b = tbdd_from_mtbdd(bdd_set_b, bdd_dom);
        TBDD tbdd_set = tbdd_from_mtbdd(bdd_set, bdd_dom);
        TBDD tbdd_dom = tbdd_from_mtbdd(bdd_dom, bdd_dom);

        TBDD tbdd_test_result = tbdd_ite(tbdd_set_a, tbdd_set_b, tbdd_false, tbdd_dom);
        test_assert(tbdd_set == tbdd_test_result);
        tbdd_test_result = tbdd_ite(tbdd_set_b, tbdd_set_a, tbdd_false, tbdd_dom);
        test_assert(tbdd_set == tbdd_test_result);
    }

         /**
         * TODO Test tbdd_ite with random sets vs BDD
         */

    // printf("Testing exists...\n");

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test tbdd_exists with random sets
         */

        // Create random domain of 6..14 variables
        int nvars = rng(3,4);
        uint32_t dom_arr[nvars];
        for (int i=0; i<nvars; i++) dom_arr[i] = i;
        BDD bdd_dom = mtbdd_fromarray(dom_arr, nvars);
        TBDD tbdd_dom = tbdd_from_array(dom_arr, nvars);
        test_assert(tbdd_dom == tbdd_from_mtbdd(bdd_dom, bdd_dom));

        // Create random subdomain
        uint32_t subdom_arr[nvars], q_arr[nvars];
        int nsub = 0, nq = 0;
        for (int i=0; i<nvars; i++) {
            if (rng(0,2)) subdom_arr[nsub++] = i;
            else q_arr[nq++] = i;
        }
        BDD bdd_subdom = mtbdd_fromarray(subdom_arr, nsub);
        TBDD tbdd_subdom = tbdd_from_array(subdom_arr, nsub);
        BDD bdd_qdom = mtbdd_fromarray(q_arr, nq);
        TBDD tbdd_qdom = tbdd_from_array(q_arr, nq);
        test_assert(tbdd_subdom == tbdd_from_mtbdd(bdd_subdom, bdd_subdom));
        test_assert(tbdd_qdom == tbdd_from_mtbdd(bdd_qdom, bdd_qdom));

        // Create random set on subdomain
        BDD bdd_set = sylvan_false;
        TBDD tbdd_set = tbdd_false;
        {
            int count = rng(10,200);
            for (int i=0; i<count; i++) {
                uint8_t arr[nvars];
                for (int j=0; j<nvars; j++) arr[j] = rng(0, 2);
                bdd_set = sylvan_union_cube(bdd_set, bdd_dom, arr);
                tbdd_set = tbdd_union_cube(tbdd_set, tbdd_dom, arr);
            }
        }
        test_assert(tbdd_set == tbdd_from_mtbdd(bdd_set, bdd_dom));

        BDD bdd_qset = sylvan_exists(bdd_set, bdd_qdom);
        TBDD tbdd_test_result = tbdd_exists(tbdd_set, tbdd_qdom, tbdd_dom);
        TBDD tbdd_test_result2 = tbdd_exists_dom(tbdd_set, tbdd_subdom);
        test_assert(tbdd_test_result == tbdd_from_mtbdd(bdd_qset, bdd_dom));
        test_assert(tbdd_test_result2 == tbdd_from_mtbdd(bdd_qset, bdd_subdom));
    }

    // printf("Testing relnext...\n");

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test tbdd_relnext with random sets
         */
        int nvars = rng(8,12);

        // Create random source set
        uint32_t dom_arr[nvars];
        for (int i=0; i<nvars; i++) dom_arr[i] = i*2;
        BDD bdd_dom = mtbdd_fromarray(dom_arr, nvars);
        TBDD tbdd_dom = tbdd_from_array(dom_arr, nvars);

        BDD bdd_set = sylvan_false;
        TBDD tbdd_set = tbdd_false;
        {
            int count = rng(4,100);
            for (int i=0; i<count; i++) {
                uint8_t arr[nvars];
                for (int j=0; j<nvars; j++) arr[j] = rng(0, 2);
                bdd_set = sylvan_union_cube(bdd_set, bdd_dom, arr);
                tbdd_set = tbdd_union_cube(tbdd_set, tbdd_dom, arr);
            }
        }
        test_assert(tbdd_set == tbdd_from_mtbdd(bdd_set, bdd_dom));

        // Create random transition relation domain
        BDD bdd_vars;
        TBDD tbdd_vars;
        uint32_t vars_arr[2*nvars];
        int len = 0;
        {
            int _vars = rng(1, 256);
            for (int i=0; i<nvars; i++) {
                if (_vars & (1<<i)) {
                    vars_arr[len++] = i*2;
                    vars_arr[len++] = i*2+1;
                }
            }
            bdd_vars = mtbdd_fromarray(vars_arr, len);
            tbdd_vars = tbdd_from_array(vars_arr, len);
        }
        test_assert(tbdd_vars == tbdd_from_mtbdd(bdd_vars, bdd_vars));

        // Create random transitions
        BDD bdd_rel = sylvan_false;
        TBDD tbdd_rel = tbdd_false;
        {
            int count = rng(100, 200);
            for (int i=0; i<count; i++) {
                uint8_t arr[len];
                for (int j=0; j<len; j++) arr[j] = rng(0, 2);
                bdd_rel = sylvan_union_cube(bdd_rel, bdd_vars, arr);
                tbdd_rel = tbdd_union_cube(tbdd_rel, tbdd_vars, arr);
            }
        }
        test_assert(tbdd_rel == tbdd_from_mtbdd(bdd_rel, bdd_vars));

        // Check if sat counts are the same
        test_assert(sylvan_satcount(bdd_set, bdd_dom) == tbdd_satcount(tbdd_set, tbdd_dom));
        test_assert(sylvan_satcount(bdd_rel, bdd_vars) == tbdd_satcount(tbdd_rel, tbdd_vars));

        BDD bdd_succ = sylvan_relnext(bdd_set, bdd_rel, bdd_vars);
        TBDD tbdd_succ = tbdd_relnext(tbdd_set, tbdd_rel, tbdd_vars, tbdd_dom);

        test_assert(tbdd_succ == tbdd_from_mtbdd(bdd_succ, bdd_dom));
    }

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test tbdd_and_dom with random sets
         */

        // Create random domain of 6..14 variables
        int nvars = rng(6,14);
        uint32_t dom_arr[nvars];
        for (int i=0; i<nvars; i++) dom_arr[i] = i;
        BDD bdd_dom = mtbdd_fromarray(dom_arr, nvars);
        TBDD tbdd_dom = tbdd_from_array(dom_arr, nvars);
        test_assert(tbdd_dom == tbdd_from_mtbdd(bdd_dom, bdd_dom));

        // Create random subdomain 1
        uint32_t subdom1_arr[nvars];
        int nsub1 = 0;
        for (int i=0; i<nvars; i++) if (rng(0,2)) subdom1_arr[nsub1++] = i;
        BDD bdd_subdom1 = mtbdd_fromarray(subdom1_arr, nsub1);
        TBDD tbdd_subdom1 = tbdd_from_array(subdom1_arr, nsub1);
        test_assert(tbdd_subdom1 == tbdd_from_mtbdd(bdd_subdom1, bdd_subdom1));

        // Create random subdomain 2
        uint32_t subdom2_arr[nvars];
        int nsub2 = 0;
        for (int i=0; i<nvars; i++) if (rng(0,2)) subdom2_arr[nsub2++] = i;
        BDD bdd_subdom2 = mtbdd_fromarray(subdom2_arr, nsub2);
        TBDD tbdd_subdom2 = tbdd_from_array(subdom2_arr, nsub2);
        test_assert(tbdd_subdom2 == tbdd_from_mtbdd(bdd_subdom2, bdd_subdom2));

        // Create random set on subdomain 1
        BDD bdd_set1 = sylvan_false;
        TBDD tbdd_set1 = tbdd_false;
        {
            int count = rng(10,200);
            for (int i=0; i<count; i++) {
                uint8_t arr[nsub1];
                for (int j=0; j<nsub1; j++) arr[j] = rng(0, 2);
                bdd_set1 = sylvan_union_cube(bdd_set1, bdd_subdom1, arr);
                tbdd_set1 = tbdd_union_cube(tbdd_set1, tbdd_subdom1, arr);
            }
        }
        test_assert(tbdd_set1 == tbdd_from_mtbdd(bdd_set1, bdd_subdom1));

        // Create random set on subdomain 2
        BDD bdd_set2 = sylvan_false;
        TBDD tbdd_set2 = tbdd_false;
        {
            int count = rng(10,200);
            for (int i=0; i<count; i++) {
                uint8_t arr[nsub2];
                for (int j=0; j<nsub2; j++) arr[j] = rng(0, 2);
                bdd_set2 = sylvan_union_cube(bdd_set2, bdd_subdom2, arr);
                tbdd_set2 = tbdd_union_cube(tbdd_set2, tbdd_subdom2, arr);
            }
        }
        test_assert(tbdd_set2 == tbdd_from_mtbdd(bdd_set2, bdd_subdom2));

        BDD bdd_set = sylvan_and(bdd_set1, bdd_set2);
        BDD bdd_subdom = sylvan_and(bdd_subdom1, bdd_subdom2);
        TBDD tbdd_set = tbdd_and_dom(tbdd_set1, tbdd_subdom1, tbdd_set2, tbdd_subdom2);
        test_assert(tbdd_set == tbdd_from_mtbdd(bdd_set, bdd_subdom));
    }

    for (int k=0; k<test_iterations; k++) {
        /**
         * Test reading/writing with random sets
         */
        int nvars = rng(8,12);

        // Create random source sets
        uint32_t dom_arr[nvars];
        for (int i=0; i<nvars; i++) dom_arr[i] = i*2;
        TBDD tbdd_dom = tbdd_from_array(dom_arr, nvars);

        int set_count = rng(1,10);
        TBDD tbdd_set[set_count];
        for (int k=0; k<set_count; k++) {
            tbdd_set[k] = tbdd_false;
            int count = rng(4,100);
            for (int i=0; i<count; i++) {
                uint8_t arr[nvars];
                for (int j=0; j<nvars; j++) arr[j] = rng(0, 2);
                tbdd_set[k] = tbdd_union_cube(tbdd_set[k], tbdd_dom, arr);
            }
        }

        FILE *f = tmpfile();
        tbdd_writer_tobinary(f, tbdd_set, set_count);
        rewind(f);
        TBDD test[set_count];
        test_assert(tbdd_reader_frombinary(f, test, set_count) == 0);
        for (int i=0; i<set_count; i++) test_assert(test[i] == tbdd_set[i]);
    }

    return 0;
}

int runtests()
{
    // we are not testing garbage collection
    sylvan_gc_disable();

    if (test_cache()) return 1;
    if (test_bdd()) return 1;
    for (int j=0;j<10;j++) if (test_cube()) return 1;
    for (int j=0;j<10;j++) if (test_relprod()) return 1;
    for (int j=0;j<10;j++) if (test_compose()) return 1;
    for (int j=0;j<10;j++) if (test_operators()) return 1;

    if (test_ldd()) return 1;
    if (test_tbdd()) return 1;

    return 0;
}

int main()
{
    // Standard Lace initialization with 1 worker
	lace_init(1, 0);
	lace_startup(0, NULL, NULL);

    // Simple Sylvan initialization, also initialize BDD, MTBDD and LDD support
	//sylvan_init_package(1LL<<20, 1LL<<20, 1LL<<16, 1LL<<16);
	sylvan_init_package(1LL<<26, 1LL<<26, 1LL<<20, 1LL<<20);
	sylvan_init_bdd();
    sylvan_init_mtbdd();
    sylvan_init_ldd();
    sylvan_init_tbdd();

    int res = runtests();

    sylvan_quit();
    lace_exit();

    return res;
}
