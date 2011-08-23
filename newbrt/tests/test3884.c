/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "$Id: test3856.c 33984 2011-08-17 03:03:54Z leifwalsh $"
#ident "Copyright (c) 2007-2011 Tokutek Inc.  All rights reserved."

// it used to be the case that we copied the left and right keys of a
// range to be prelocked but never freed them, this test checks that they
// are freed (as of this time, this happens in destroy_bfe_for_prefetch)

#include "test.h"

#include "includes.h"

static TOKUTXN const null_txn = 0;
static DB * const null_db = 0;
static const char fname[]= __FILE__ ".brt";

static int omt_long_cmp(OMTVALUE p, void *q)
{
    LEAFENTRY a = p, b = q;
    void *ak, *bk;
    u_int32_t al, bl;
    ak = le_key_and_len(a, &al);
    bk = le_key_and_len(b, &bl);
    assert(al == sizeof(long) && bl == sizeof(long));
    long *ai = (long *) ak;
    long *bi = (long *) bk;
    return (*ai > *bi) - (*ai < *bi);
}

static LEAFENTRY
le_fastmalloc(char *key, int keylen, char *val, int vallen)
{
    LEAFENTRY r = toku_malloc(sizeof(r->type) + sizeof(r->keylen) + sizeof(r->u.clean.vallen) +
                              keylen + vallen);
    resource_assert(r);
    r->type = LE_CLEAN;
    r->keylen = keylen;
    r->u.clean.vallen = vallen;
    memcpy(&r->u.clean.key_val[0], key, keylen);
    memcpy(&r->u.clean.key_val[keylen], val, vallen);
    return r;
}

int
test_main (int argc __attribute__((__unused__)), const char *argv[] __attribute__((__unused__))) {
    toku_memory_check = 1;

    const int nodesize = 1024, eltsize = 64, bnsize = 256;
    const int keylen = sizeof(long), vallen = eltsize - keylen - (sizeof(((LEAFENTRY)NULL)->type)  // overhead from LE_CLEAN_MEMSIZE
                                                                  +sizeof(((LEAFENTRY)NULL)->keylen)
                                                                  +sizeof(((LEAFENTRY)NULL)->u.clean.vallen));
    const int eltsperbn = bnsize / eltsize;
    struct brtnode sn;

    int fd = open(__FILE__ ".brt", O_RDWR|O_CREAT|O_BINARY, S_IRWXU|S_IRWXG|S_IRWXO); assert(fd >= 0);

    int r;

    sn.max_msn_applied_to_node_on_disk.msn = 0;
    sn.nodesize = nodesize;
    sn.flags = 0x11223344;
    sn.thisnodename.b = 20;
    sn.layout_version = BRT_LAYOUT_VERSION;
    sn.layout_version_original = BRT_LAYOUT_VERSION;
    sn.height = 0;
    const int nelts = 2 * nodesize / eltsize;
    sn.n_children = nelts * eltsize / bnsize;
    sn.dirty = 1;
    LEAFENTRY elts[nelts];
    MALLOC_N(sn.n_children, sn.bp);
    MALLOC_N(sn.n_children - 1, sn.childkeys);
    sn.totalchildkeylens = 0;
    for (int bn = 0; bn < sn.n_children; ++bn) {
        BP_SUBTREE_EST(&sn,bn).ndata = random() + (((long long)random())<<32);
        BP_SUBTREE_EST(&sn,bn).nkeys = random() + (((long long)random())<<32);
        BP_SUBTREE_EST(&sn,bn).dsize = random() + (((long long)random())<<32);
        BP_SUBTREE_EST(&sn,bn).exact =  (BOOL)(random()%2 != 0);
        BP_STATE(&sn,bn) = PT_AVAIL;
        set_BLB(&sn, bn, toku_create_empty_bn());
        BLB_NBYTESINBUF(&sn,bn) = 0;
        BLB_OPTIMIZEDFORUPGRADE(&sn, bn) = BRT_LAYOUT_VERSION;
        long k;
        for (int i = 0; i < eltsperbn; ++i) {
            k = bn * eltsperbn + i;
            char val[vallen];
            memset(val, k, sizeof val);
            elts[k] = le_fastmalloc((char *) &k, keylen, val, vallen);
            r = toku_omt_insert(BLB_BUFFER(&sn, bn), elts[k], omt_long_cmp, elts[k], NULL); assert(r == 0);
            BLB_NBYTESINBUF(&sn, bn) += OMT_ITEM_OVERHEAD + leafentry_disksize(elts[k]);
        }
        if (bn < sn.n_children - 1) {
            sn.childkeys[bn] = kv_pair_malloc(&k, sizeof k, 0, 0);
            sn.totalchildkeylens += (sizeof k);
        }
    }

    unlink(fname);
    CACHETABLE ct;
    BRT brt;
    r = toku_brt_create_cachetable(&ct, 0, ZERO_LSN, NULL_LOGGER);   assert(r==0);
    r = toku_open_brt(fname, 1, &brt, nodesize, bnsize, ct, null_txn, toku_builtin_compare_fun, null_db); assert(r==0);

    BRTNODE nodea, nodeb;
    DBT splitk;
    // if we haven't done it right, we should hit the assert in the top of move_leafentries
    brtleaf_split(brt, &sn, &nodea, &nodeb, &splitk, TRUE);

    // r = toku_close_brt(brt, NULL); assert(r == 0);
    // r = toku_cachetable_close(&ct); assert(r == 0);

    if (splitk.data) {
        toku_free(splitk.data);
    }

    toku_brtnode_free(&nodeb);

    for (int i = 0; i < sn.n_children - 1; ++i) {
        kv_pair_free(sn.childkeys[i]);
    }
    for (int i = 0; i < nelts / 2; ++i) {
        toku_free(elts[i]);
    }
    for (int i = 0; i < sn.n_children; ++i) {
        destroy_basement_node(BLB(&sn, i));
    }
    toku_free(sn.bp);
    toku_free(sn.childkeys);

    return 0;
}
