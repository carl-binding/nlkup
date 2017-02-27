
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <pthread.h>

#include "mem.h"
#include "logger.h"
#include "json.h"
#include "utils.h"
#include "nlkup.h"

static LkupTblEntry *alloc_lkup_tbl_entry() {
  LkupTblEntry *e = mem_alloc( sizeof( LkupTblEntry));
  memset( (void *) e, 0, sizeof( LkupTblEntry));
  return e;
}

static void free_lkup_tbl_entry( LkupTblEntry *e) {
  memset( e, 0, sizeof( LkupTblEntry));
  mem_free( e);
}

// creates a copy of given lookup table
static LkupTbl *copy_lkup_tbl( LkupTbl *old_t) {
  if ( old_t == NULL) 
    return NULL;

  LkupTbl *t = mem_alloc( sizeof( LkupTbl));

  t->table_sz = old_t->table_sz;
  t->table_len = old_t->table_len; // used entry count

  t->table = mem_alloc( t->table_sz * sizeof( LkupTblEntry));
  memcpy( t->table, old_t->table, t->table_len * sizeof( LkupTblEntry));

  return t;
  
}

// allocates an empty lookup table
static LkupTbl *alloc_lkup_tbl() {
  LkupTbl *t = mem_alloc( sizeof( LkupTbl));

  t->table_sz = DEF_LKUP_BLK_SIZE;
  t->table_len = 0; // used entry count

  int tbl_sz = t->table_sz * sizeof( LkupTblEntry);
  // fprintf( stderr, "table size = %d [bytes]\n", tbl_sz);

  t->table = mem_alloc( tbl_sz);
 
  return t;
}

// copying [from..to] from origin table into a newly allocated table
static LkupTbl *copy_lkup_tbl_range( LkupTbl *origin, int from_idx, int to_idx) {
  
  if ( to_idx - 1 <= from_idx) {
    return NULL;
  }

  LkupTbl *t = mem_alloc( sizeof( LkupTbl));

  t->table_sz = to_idx - from_idx + 1;
  t->table_len = t->table_sz;

  int tbl_sz = t->table_sz * sizeof( LkupTblEntry);
  // fprintf( stderr, "table size = %d [bytes]\n", tbl_sz);

  t->table = mem_alloc( tbl_sz);
  
  int i = 0;
  for ( i = from_idx; i <= to_idx; i++) {
    t->table[i-from_idx] = origin->table[i];
  }

  return t;
}

static void grow_table( LkupTbl *t) {

  assert( t->table_sz <= t->table_len);

  int tbl_sz = (t->table_sz + DEF_LKUP_BLK_SIZE) * sizeof( LkupTblEntry); // bytes
  int old_tbl_sz = t->table_sz * sizeof( LkupTblEntry);
  LkupTblEntry *nt = mem_alloc( tbl_sz);

  // copy old table
  if ( t->table_sz > 0) {
    memcpy( nt, t->table, old_tbl_sz);
  }

  // clear out upper part of new table
  unsigned char *cp = (unsigned char *) nt;
  cp += t->table_sz * sizeof( LkupTblEntry);  // bytes...
  memset( cp, 0, tbl_sz - old_tbl_sz);

  if ( t->table != NULL) {
    mem_free( t->table);
    t->table = NULL;
  }

  t->table = nt;
  t->table_sz = t->table_sz + DEF_LKUP_BLK_SIZE; // # of records
  
}

static void shrink_table( LkupTbl *t) {

  assert( t->table_sz >= t->table_len + DEF_LKUP_BLK_SIZE);

  int new_tbl_sz = (t->table_sz - DEF_LKUP_BLK_SIZE) * sizeof( LkupTblEntry); // bytes
  int old_tbl_len = t->table_len * sizeof( LkupTblEntry);

  LkupTblEntry *nt = mem_alloc( new_tbl_sz);

  // copy old table into new.
  memcpy( nt, t->table, old_tbl_len);

  // ensure new table is zeroed out if spare space
  if ( new_tbl_sz > old_tbl_len) {
    unsigned char *cp = (unsigned char *) nt;
    cp += old_tbl_len;
    memset( cp, 0, new_tbl_sz - old_tbl_len);
  }

  // switch tables and adjust size
  mem_free( t->table);
  t->table = nt;
  t->table_sz -= DEF_LKUP_BLK_SIZE;

}

static void shift_table_up( LkupTbl *t, int idx) {
  assert( t->table_sz > t->table_len);
  // if idx is at end of table, nothing to do...
  if ( idx >= t->table_len) return;
  int i = 0;
  for ( i = t->table_len; i > idx; i--) {
    t->table[i] = t->table[i-1];
  }
}

static void shift_table_down( LkupTbl *t, int idx) {
  if ( idx >= t->table_len-1) return;
  int i = 0;
  for ( i = idx; i < t->table_len-1; i++) {
    t->table[i] = t->table[i+1];
  }
}

void free_lkup_tbl( LkupTbl *t) {
  if ( t->table != NULL) {
    memset( t->table, 0, t->table_sz * sizeof( LkupTblEntry));
    mem_free( t->table);
    t->table = NULL;
  }
  memset( t, 0, sizeof( LkupTbl));
  mem_free( t);
}

static int alloc_lkup_tbl_in_index( IdxTblEntry index_table[], int idx) {

  assert( idx >= 0 && idx <= INDEX_SIZE - INDEX_OFFSET);
  if ( index_table[idx].table != NULL) {
    return 0;
  }
  if ( ( index_table[idx].table = alloc_lkup_tbl()) == NULL) {
    return -1;
  }
  return 0;
}

static int free_lkup_tbl_in_index( IdxTblEntry index_table[], int idx) {
  assert( idx >= 0 && idx <= INDEX_SIZE - INDEX_OFFSET);
  if ( index_table[idx].table == NULL) {
    return 0;
  }
  free_lkup_tbl( index_table[idx].table);
  index_table[idx].table = NULL;
  return 0;
}

static int compare_entry( LkupTblEntry *e1, LkupTblEntry *e2) {
  assert( e1 != NULL && e2 != NULL);

  if ( e1 == e2) return 0;
  
  unsigned char *p1 = e1->postfix;
  unsigned char *p2 = e2->postfix;
  int i = 0;
  for ( i = 0; i < POSTFIX_LENGTH; i++) {
    if ( *p1 < *p2) return -1;
    if ( *p1 > *p2) return +1;
    p1++; p2++;
  }
  return 0;
}

// returns the first 6 digits of number as integer
static int get_index( const unsigned char *nbr) {

  if ( nbr == NULL || strlen( nbr) < PREFIX_LENGTH) {
    log_msg( ERR, "get_index: string too short or null\n");
    return NBR_TOO_SHORT;
  }

  unsigned char pfx[PREFIX_LENGTH+1];
  memcpy( pfx, nbr, PREFIX_LENGTH);
  pfx[PREFIX_LENGTH] = 0;

  long idx = atol( pfx);
  if ( idx <= 0) {
    log_msg( ERR, "get_index: negative or illegal conversion %s\n", nbr);
    return ILLEGAL_NUMBER;
  }
  assert( idx >= INDEX_OFFSET);
  return (int) idx - INDEX_OFFSET;
}

// binary search
// returns index (>= 0) if found or -insertion_point-1 if not found
static int search_entry_in_table( LkupTblPtr tbl, LkupTblEntry *e) {

  assert( tbl != NULL && e != NULL);

  if ( tbl->table_len == 0) return -1;
  
  // binary search
  int left = 0;
  int right = tbl->table_len - 1;
  int mid = 0;
  int cmp = 0;

  while ( left <= right) {

    mid = (left + right)/2;

    cmp = compare_entry( &tbl->table[mid], e);
    log_msg( DEBUG, "%d %d %d %d\n", left, right, mid, cmp);

    if ( cmp < 0) {
      left = mid + 1;
    } else if ( cmp > 0) {
      right = mid - 1;
    } else {
      return mid;
    }
  }

  // we return -insertion point - 1
  int ret_val = -mid - 1;
  if ( cmp < 0)
    ret_val -= 1;

  return ret_val;

}

// enters a new entry. if duplicate, overwrites the old alias
int enter_entry( IdxTblEntry index_table[], const unsigned char *nbr, const unsigned char *alias) {

  int idx = get_index( nbr);
  if ( idx < 0) {
    log_msg( ERR, "enter_entry: bad index %d: %s\n", idx, nbr);
    return FAILURE;
  }

  lock_table( index_table, idx);

  if ( index_table[idx].table == NULL) {
    index_table[idx].table = alloc_lkup_tbl();
  }

  LkupTblEntry key;
  memset( &key, 0, sizeof( LkupTblEntry));

  if ( compress_to_buf( nbr, PREFIX_LENGTH, strlen( nbr)-PREFIX_LENGTH, key.postfix, POSTFIX_LENGTH) < 0) {
    log_msg( ERR, "enter_entry: failure to compress %s\n", nbr);

    unlock_table( index_table, idx);
    return FAILURE;
  }

  LkupTbl *t = index_table[idx].table;

  int e_idx = search_entry_in_table( t, &key);
  log_msg( DEBUG, "key = %s idx = %d\n", nbr, e_idx);

  if ( e_idx < 0) { // key not found
    // e_idx is -insertion_point - 1
    int i_idx = -(e_idx+1);

    if ( t->table_len >= t->table_sz) { // table full
      grow_table( t);
    }

    assert( t->table_len < t->table_sz);
    shift_table_up( t, i_idx);
    
    LkupTblEntry *e = &(t->table[i_idx]);
    memcpy( e->postfix, key.postfix, POSTFIX_LENGTH);
    if ( compress_to_buf( alias, 0, strlen( alias), e->alias, ALIAS_LENGTH) < 0) {
      log_msg( ERR, "enter_entry: failure to compress %s\n", alias);

      unlock_table( index_table, idx);
      return FAILURE;
    }

    t->table_len++; // bump up counter of used entries

  } else { // found key

    // fprintf( stderr, "duplicate nbr: %s\n", nbr);

    LkupTblEntry *e = &(t->table[e_idx]);
    assert( compare_entry( e, &key) == 0);
    // overwrite alias
    if ( compress_to_buf( alias, 0, strlen( alias), e->alias, ALIAS_LENGTH) < 0) {
      log_msg( ERR, "enter_entry: failure to compress %s\n", alias);

      unlock_table( index_table, idx);
      return FAILURE;
    }
  }

  unlock_table( index_table, idx);

  return SUCCESS;  
}

static int search_entry_with_buffer(IdxTblEntry index_table[], const unsigned char *nbr, 
				    unsigned char alias[], const int alias_sz) 
{

  int idx = get_index( nbr);
  if ( idx < 0) {
    log_msg( ERR, "search_entry: bad index %d: %s\n", idx, nbr);
    return FAILURE;
  }

  lock_table( index_table, idx);

  LkupTbl *t = index_table[idx].table;

  if ( t == NULL) {
    unlock_table( index_table, idx);
    return NO_SUCH_ENTRY;
  }

  // allocate search key
  LkupTblEntry key;
  memset( &key, 0, sizeof( LkupTblEntry));

  if ( compress_to_buf( nbr, PREFIX_LENGTH, strlen( nbr)-PREFIX_LENGTH, key.postfix, POSTFIX_LENGTH) < 0) {
    log_msg( ERR, "search_entry: failure to compress %s\n", nbr);

    unlock_table( index_table, idx);
    return FAILURE;
  }

  // do the search
  int e_idx = search_entry_in_table( t, &key);
  log_msg( DEBUG, "key = %s idx = %d\n", nbr, e_idx);

  if ( e_idx < 0) {
    unlock_table( index_table, idx);
    return NO_SUCH_ENTRY;
  }

  // decompress into buffer
  LkupTblEntry *e = &(t->table[e_idx]);
  decompress_to_buf( e->alias, alias, alias_sz);

  unlock_table( index_table, idx);
  return SUCCESS;

}


static int set_up_search_key( LkupTblEntry *key, const unsigned char *nbr) {

  memset( key, 0, sizeof( LkupTblEntry));

  if ( compress_to_buf( nbr, PREFIX_LENGTH, strlen( nbr)-PREFIX_LENGTH, key->postfix, POSTFIX_LENGTH) < 0) {
    log_msg( ERR, "set_up_search_key: failure to compress %s\n", nbr);
    return FAILURE;
  }
  return SUCCESS;
}

// searches entry and set alias if found. 
// returns negative value if entry not found
int search_entry( IdxTblEntry index_table[], const unsigned char *nbr, unsigned char **alias) {

  *alias = mem_alloc( MAX_NBR_LENGTH + 1);
  int s = search_entry_with_buffer( index_table, nbr, *alias, MAX_NBR_LENGTH+1);
  if ( s < 0) {
    mem_free( *alias);
    *alias = NULL;
  }
  return s;
}

// deletes the given entry if present. no-op otherwise.
int delete_entry( IdxTblEntry index_table[], const unsigned char *nbr) {

  int status = SUCCESS;

  int idx = get_index( nbr);
  if ( idx < 0) {
    log_msg( ERR, "delete_entry: bad index %d: %s\n", idx, nbr);
    return FAILURE;
  }

  lock_table( index_table, idx);

  // no table, nothing to delete....
  if ( index_table[idx].table == NULL) {
    goto out;
  }

  // allocate search key
  LkupTblEntry key;

  if ( set_up_search_key( &key, nbr) != SUCCESS) {
    log_msg( ERR, "delete_entry: failure to set up search key %s\n", nbr);
    status = FAILURE;
    goto out;
  }

  LkupTbl *t = index_table[idx].table;

  // do the search
  int e_idx = search_entry_in_table( t, &key);
  log_msg( DEBUG, "key = %s idx = %d\n", nbr, e_idx);

  if ( e_idx < 0) { // no such entry. we're done
    status = SUCCESS;
    goto out;
  }
  // found the entry

  if ( t->table_len == 1) { // last entry

    mem_free( t);

    index_table[idx].table = NULL;
    t->table_len = 0;
    t->table_sz = 0;

    status = SUCCESS;
    goto out;
  }

  shift_table_down( t, e_idx);
  t->table_len--;
  // table size unchanged...

  if ( t->table_sz - t->table_len >= DEF_LKUP_BLK_SIZE) {
    shrink_table( t);
  }
  
  status = SUCCESS;

 out:

  unlock_table( index_table, idx);
  return status;

}

static void test_compression( unsigned char *s) {

  unsigned char *cs = compress( s, 0, strlen( s));
  if ( cs == NULL) return;

  unsigned char *dcs = decompress( cs);
  if ( dcs == NULL) return;

  fprintf( stderr, "%s %s\n", s, dcs);

  mem_free( dcs);
  mem_free( cs);
}

// allocate index table with 1 million entries... of which we keep 100'000 empty
static IdxTblEntry index_table[INDEX_SIZE-INDEX_OFFSET];

static void test_search( unsigned char *nbr) {
  unsigned char *alias = NULL;
  if ( search_entry( index_table, nbr, &alias) < 0) {
    fprintf( stderr, "nbr not found\n");
  } else {
    fprintf( stderr, "nbr alias = %s\n", alias);
    mem_free( alias);
  }
}

// initializing index table
static void init_index( IdxTblEntry index_table[]) {
  int i = 0;
  for ( i = 0; i < INDEX_SIZE - INDEX_OFFSET; i++) {
    IdxTblEntry *e = &(index_table[i]);

    pthread_mutex_init( &(e->mutex), NULL);

  }
}

/*
  Copyright (c) 2017, Carl Binding, LI-9494 Schaan

  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 The views and conclusions contained in the software and documentation are those
 of the authors and should not be interpreted as representing official policies,
 either expressed or implied, of the FreeBSD Project.
*/


// init the module
int nlkup_init() {

  init_index( index_table);

  if ( restore_all_fn( index_table, "dump.bin") != SUCCESS) {
    log_msg( ERR, "nlkup_init: restore_all_fn() failed");
    return -1;
  }

  return 0;
}

int nlkup_enter_entry( const unsigned char *nbr, const unsigned char *alias) {
  return enter_entry( index_table, nbr, alias);
}

int nlkup_delete_entry( const unsigned char *nbr) {
  return delete_entry( index_table, nbr);
}

// looks up the given number and returns the alias which must be mem_freed() if non NULL
int nlkup_search_entry( const unsigned char *nbr, unsigned char **alias) {
  return search_entry( index_table, nbr, alias);
}

// address of an entry. indexed into two levels.
typedef struct {
  int idx_tbl_idx;  // index into index table
  int lkup_tbl_idx; // index into entry table
} EntryAddressStruct;

#define CHECK_IDX_TBL_IDX( eas)   assert( eas->idx_tbl_idx >= 0 && eas->idx_tbl_idx < (INDEX_SIZE-INDEX_OFFSET));

// starting at the nearest_entry find the address of the nbr_before-th entry
static int find_start_entry( const EntryAddressStruct *nearest_entry, const int nbr_before, EntryAddressStruct *start_entry) {

  memset( start_entry, 0, sizeof( EntryAddressStruct));
  if ( nbr_before <= 0) {
    return FAILURE;
  }

  CHECK_IDX_TBL_IDX( nearest_entry);

  // copy nearest entry as it is modified
  EntryAddressStruct ne = *nearest_entry;
  int last_non_empty_tbl_idx = 0;

  int cnt = 0;

  // lock current table and see if we can have the neeeded nbr entries before
  LkupTbl *t = index_table[ne.idx_tbl_idx].table;

  lock_table( index_table, ne.idx_tbl_idx);

  if ( ne.lkup_tbl_idx >= nbr_before-1) { // satisfy request with current table

    start_entry->idx_tbl_idx = ne.idx_tbl_idx;
    start_entry->lkup_tbl_idx = ne.lkup_tbl_idx - (nbr_before-1);

    unlock_table( index_table, ne.idx_tbl_idx);

    assert( start_entry->lkup_tbl_idx >= 0);
    return SUCCESS;
  } else { // need entire current table
    cnt += ne.lkup_tbl_idx;
    last_non_empty_tbl_idx = ne.idx_tbl_idx;
  }

  unlock_table( index_table, ne.idx_tbl_idx);

  // no. iterate over tables before current table. keeping track of # of entries
  while ( cnt <= nbr_before && ne.idx_tbl_idx > 0) {

    ne.idx_tbl_idx--;

    t = index_table[ne.idx_tbl_idx].table;

    if ( t == NULL || t->table_len == 0) {
      continue;
    }

    last_non_empty_tbl_idx = ne.idx_tbl_idx;

    lock_table( index_table, ne.idx_tbl_idx);

    if (( nbr_before - cnt) <= t->table_len) { // table has enough entries
      start_entry->idx_tbl_idx = ne.idx_tbl_idx;
      start_entry->lkup_tbl_idx = t->table_len - (nbr_before-cnt-1);

      unlock_table( index_table, ne.idx_tbl_idx);

      assert( start_entry->lkup_tbl_idx >= 0);
      return SUCCESS;
    } else { // use the entire table and go one table back
      cnt += t->table_len; 
    }

    unlock_table( index_table, ne.idx_tbl_idx);

  }


  start_entry->idx_tbl_idx = last_non_empty_tbl_idx;
  start_entry->lkup_tbl_idx = 0;

  return NOT_ENOUGH_DATA;
}

// starting at the nearest_entry, find the address of the nbr_after-th entry 
static int find_end_entry( const EntryAddressStruct *nearest_entry, const int nbr_after, EntryAddressStruct *end_entry) {
  memset( end_entry, 0, sizeof( EntryAddressStruct));
  if ( nbr_after <= 0) {
    return -1;
  }

  CHECK_IDX_TBL_IDX( nearest_entry);

  // copy to modify
  EntryAddressStruct ne = *nearest_entry;
  int last_non_empty_tbl_idx = 0;

  int cnt = 0;

  // lock current table and see if we can have the needed nbr entries after
  LkupTbl *t = index_table[ne.idx_tbl_idx].table;

  lock_table( index_table, ne.idx_tbl_idx);

  if ( t->table_len - (ne.lkup_tbl_idx+1) >= nbr_after) { // satisfy request with current table
    end_entry->idx_tbl_idx = ne.idx_tbl_idx;
    end_entry->lkup_tbl_idx = ne.lkup_tbl_idx + nbr_after;

    unlock_table( index_table, ne.idx_tbl_idx);

    assert( end_entry->lkup_tbl_idx < t->table_len);
    return SUCCESS;
  } else { // need entire current table
    cnt += t->table_len - (ne.lkup_tbl_idx+1);
    last_non_empty_tbl_idx = ne.idx_tbl_idx;
  }

  unlock_table( index_table, ne.idx_tbl_idx);

  // no. iterate over tables after current table. keeping track of # of entries
  while ( cnt < nbr_after && ne.idx_tbl_idx < ((INDEX_SIZE-INDEX_OFFSET)-1)) {

    ne.idx_tbl_idx++;

    t = index_table[ne.idx_tbl_idx].table;
    
    if ( t == NULL || t->table_len == 0) {
      continue;
    }

    last_non_empty_tbl_idx = ne.idx_tbl_idx;

    lock_table( index_table, ne.idx_tbl_idx);

    if (( nbr_after - cnt) <= t->table_len) { // table has enough entries
      end_entry->idx_tbl_idx = ne.idx_tbl_idx;
      end_entry->lkup_tbl_idx = (nbr_after-cnt-1);

      unlock_table( index_table, ne.idx_tbl_idx);

      assert( end_entry->lkup_tbl_idx < t->table_len);
      return SUCCESS;

    } else { // use the entire table and go one table back
      cnt += t->table_len; 
    }

    unlock_table( index_table, ne.idx_tbl_idx);
  }

  end_entry->idx_tbl_idx = last_non_empty_tbl_idx;
  end_entry->lkup_tbl_idx = index_table[ last_non_empty_tbl_idx].table->table_len-1;

  return NOT_ENOUGH_DATA;

}

// find the next entry up or down around lkup_tbl_idx if it is >= 0. 
// if lkup_tlb_idx == -1 we are looking at a new table.
static int find_nearest_table_entry( LkupTbl *t, const int up, int lkup_tbl_idx) {

  // table is locked

  if ( t->table_len <= 0) // empty table...
    return -1;
  
  // table not empty...

  if ( lkup_tbl_idx < 0) { // looking into a "new" table..
    // start at bottom or top, depending on direction of search
    if ( up) {
      return 0; // first entry in table
    } else {
      return t->table_len-1; // last entry in table
    }
  } else { // looking into the table where we started search
    // must look around index
    if ( up) {
      if ( lkup_tbl_idx + 1 <= t->table_len - 1) { // next entry up
	return lkup_tbl_idx+1;
      } else { // no next entry up
	return -1;
      }
    } else {
      if ( lkup_tbl_idx > 0) { // next entry down
	return lkup_tbl_idx-1;
      } else { // no next entry down
	return -1;
      }
    }
  }

  assert( FALSE);
  return -1;
}

// move along the search tables up or down and find nearest non-empty entry.
// in case the initial search for a number did not find the nbr.
// in case of success, the table with nearest entry is locked.
static int find_nearest_entry( int idx_tbl_idx, int lkup_tbl_idx, const int up, EntryAddressStruct *nearest_entry) {

  memset( nearest_entry, 0, sizeof( EntryAddressStruct));

  int found = FALSE;

  while ( !found) {

    lock_table( index_table, idx_tbl_idx);

    // current lookup table
    LkupTbl *t = index_table[idx_tbl_idx].table;
    int e_idx = find_nearest_table_entry( t, up, lkup_tbl_idx);

    if ( e_idx >= 0) {
      found = TRUE;
      nearest_entry->idx_tbl_idx = idx_tbl_idx;
      nearest_entry->lkup_tbl_idx = e_idx;
      // keep lock on that table....
      return SUCCESS;
    }

    unlock_table( index_table, idx_tbl_idx);
  
    // inc/dec index into index-table
    if ( up) {
      if ( idx_tbl_idx >= INDEX_SIZE - INDEX_OFFSET) {
	return -1;
      }
      idx_tbl_idx++;
    } else {
      if ( idx_tbl_idx <= 0) {
	return -1;
      }
      idx_tbl_idx--;
    }

    // we switched lookup-table, thus the lkup table entry index is no longer valid...
    lkup_tbl_idx = -1;

  }
  
  return -1;
}

// copy data from one table between [start .. end] indices into data[] array
// returns index into next empty slot, i.e. the current length used (data_offset)
static int copy_table_data( int tbl_idx,              // table index into index_table array
			    int start_idx,            // start & end indices into table
			    int end_idx, 
			    int data_offset,          // offset into data array
			    NumberAliasStruct data[], // data array
			    int data_sz,              // size of data array
			    int lock_table_flag       // need to lock table?
) {

  if ( start_idx > end_idx) { // nothing to copy
    return data_offset;
  }

  char prefix[7]; 

  memset( prefix, 0, sizeof( prefix));
  snprintf( prefix, sizeof( prefix), "%ld", (long) (tbl_idx+INDEX_OFFSET));

  int prefix_len = strlen( prefix);

  LkupTbl *t = index_table[tbl_idx].table;
  if ( lock_table_flag)
    lock_table( index_table, tbl_idx);

  // table size can have changed....

  if ( t == NULL || t->table_len == 0) { // table is empty

    if ( lock_table_flag)
      unlock_table( index_table, tbl_idx);

    return data_offset;
  }

  // table shrank...
  if ( start_idx >= t->table_len-1) 
    start_idx = t->table_len-1;
  if ( end_idx >= t->table_len-1) 
    end_idx = t->table_len-1;

  // copy whatever is left
  while ( start_idx <= end_idx && data_offset < data_sz) {

    // get the entry
    LkupTblEntry *e = &(t->table[start_idx]);

    // buffers for decompressed string data
    char alias[MAX_NBR_LENGTH+1];
    char postfix[MAX_NBR_LENGTH+1];

    int decompression_ok = TRUE;

    // decompress the data
    if ( decompress_to_buf( e->postfix, postfix, sizeof( postfix)) < 0) {
      log_msg( ERR, "copy_table_data: decompress postfix failed\n");
      decompression_ok = FALSE;
    }
    if ( decompress_to_buf( e->alias, alias, sizeof( alias)) < 0) {
      log_msg( ERR, "copy_table_data: decompress alias failed\n");
      decompression_ok = FALSE;
    }

    if ( decompression_ok) {
      // destination
      NumberAliasStruct *na = &(data[data_offset]);
      memset( na, 0, sizeof( NumberAliasStruct));

      strncpy( na->nbr, prefix, sizeof( na->nbr));
      strncpy( na->nbr+prefix_len, postfix, sizeof( na->nbr)+prefix_len);
    
      strncpy( na->alias, alias, sizeof( alias));

      data_offset++;
    } // else decompression failed, skip entry

    start_idx++;
    
  }

  if ( lock_table_flag)
    unlock_table( index_table, tbl_idx);

  // next empty slot, viz. the length
  return data_offset;
}

// given two addresses copy the range between into data array
static int copy_numbers( EntryAddressStruct *start, EntryAddressStruct *end, int *data_len, NumberAliasStruct *data[], int data_sz) {

  *data = calloc( sizeof( NumberAliasStruct), data_sz);
  int data_idx = 0;
  
  int cur_idx = start->idx_tbl_idx; // current lookup table index
  while ( (cur_idx <= end->idx_tbl_idx) && (*data_len < data_sz)) {

    LkupTbl *t = index_table[cur_idx].table;

    if ( t == NULL || t->table_len == 0) {
      cur_idx++;
      continue;
    }

    if ( cur_idx == start->idx_tbl_idx) {    // first block
      int start_lkup_idx = start->lkup_tbl_idx;
      int end_lkup_idx = t->table_len-1;
      if ( cur_idx == end->idx_tbl_idx) { // end is in first block, only copy until end->lkup_tbl_idx
	end_lkup_idx = end->lkup_tbl_idx;
	data_idx = copy_table_data( cur_idx, start_lkup_idx, end_lkup_idx, data_idx, *data, data_sz, TRUE);
	*data_len = data_idx;
	return SUCCESS;
      } else { // copy upper part of table
	data_idx = copy_table_data( cur_idx, start_lkup_idx, end_lkup_idx, data_idx, *data, data_sz, TRUE);
	*data_len = data_idx;
      }
    } else if ( cur_idx == end->idx_tbl_idx) { // current block is last block
      int start_lkup_idx = 0;
      int end_lkup_idx = end->lkup_tbl_idx;

      data_idx = copy_table_data( cur_idx, start_lkup_idx, end_lkup_idx, data_idx, *data, data_sz, TRUE);
      *data_len = data_idx;
      return SUCCESS;

    } else { // block in middle
      data_idx = copy_table_data( cur_idx, 0, t->table_len-1, data_idx, *data, data_sz, TRUE);
      *data_len = data_idx;
    }

    cur_idx++;
  }

  return SUCCESS;

}

// data structure to hold a range of number-aliases
typedef struct {
  int data_sz;
  int data_len;
  NumberAliasStruct *data;
} RangeDataStruct;

static int cmp_number_alias_struct ( const void *s1, const void *s2) {

  if ( s1 == s2) 
    return 0;

  const NumberAliasStruct *n1 = (NumberAliasStruct *) s1;
  const NumberAliasStruct *n2 = (NumberAliasStruct *) s2;

  return strcasecmp( n1->nbr, n2->nbr);
}

static void sort_range_data( RangeDataStruct *range) {
  qsort( range->data, range->data_len, sizeof( NumberAliasStruct), cmp_number_alias_struct);
}

// starting with start we copy data before and after into range data buffer
// the data we copy out is not sorted.
static int copy_range_data( RangeDataStruct *range, EntryAddressStruct *start, int nbr_before, int nbr_after) {

  int end_idx = 0;
  int start_idx = 0;

  assert( range->data_sz > 0);
  assert( range->data_len == 0);

  // allocate data array
  range->data = calloc( sizeof( NumberAliasStruct), range->data_sz);

  // starting table is locked. copy data before & after

  // before, including nearest-entry. from start of block or nbr_before
  start_idx = 0;
  end_idx = start->lkup_tbl_idx-1; // excluding nearest-entry
  if ( nbr_before < (end_idx+1) - start_idx) { // truncate start
    start_idx = (end_idx+1) - nbr_before;
  }

  range->data_len = copy_table_data( start->idx_tbl_idx, 
				     start_idx, end_idx+1,  // include nearest entry!
				     0, range->data, range->data_sz, 
				     FALSE);
  nbr_before -= (end_idx+1) - start_idx; // don't count nearest-entry here...

  LkupTbl *t = index_table[start->idx_tbl_idx].table; // alias

  // after nearest entry til end of table or nbr_after
  start_idx = start->lkup_tbl_idx+1; // after nearest entry
  end_idx = t->table_len-1; // max end idx
  if ( nbr_after < (end_idx+1) - start_idx) { // truncate end
    end_idx = start_idx + (nbr_after - 1); // indices...
  }

  range->data_len = copy_table_data( start->idx_tbl_idx, 
				     start_idx, end_idx,
				     range->data_len, range->data, range->data_sz, 
				     FALSE);
  nbr_after -= (end_idx+1) - start_idx;

  // starting table is still locked....

  // if necessary, copy from block tables before
  int tbl_idx = start->idx_tbl_idx - 1;

  while ( nbr_before > 0 && tbl_idx >= 0) {

    t = index_table[tbl_idx].table; // alias

    if ( t == NULL || t->table_len == 0) { // empty block
      tbl_idx--;
      continue;
    }

    lock_table( index_table, tbl_idx);

    end_idx = t->table_len-1;
    start_idx = t->table_len - nbr_before;
    if ( start_idx < 0) // truncate
      start_idx = 0;

    range->data_len = copy_table_data( tbl_idx, 
				       start_idx, end_idx, 
				       range->data_len, range->data, range->data_sz, 
				       FALSE);

    unlock_table( index_table, tbl_idx);

    // adjust the count before nearest entry
    nbr_before -= (end_idx + 1) - start_idx;

    tbl_idx--;  // go back one block
  }

  // if necessary, copy from block tables after
  tbl_idx = start->idx_tbl_idx + 1;
  while ( nbr_after > 0 && tbl_idx < (INDEX_SIZE-INDEX_OFFSET)) {

    t = index_table[tbl_idx].table; // alias

    if ( t == NULL || t->table_len == 0) { // empty block
      tbl_idx++;
      continue;
    }

    lock_table( index_table, tbl_idx);

    start_idx = 0;
    end_idx = nbr_after-1;
    if ( end_idx >= t->table_len)
      end_idx = t->table_len-1;

    range->data_len = copy_table_data( tbl_idx, 
				       start_idx, end_idx, 
				       range->data_len, range->data, range->data_sz, 
				       FALSE);

    unlock_table( index_table, tbl_idx);

    // adjust count after
    nbr_after -= (end_idx + 1) - start_idx;

    tbl_idx++;  // go forward one block

  }

  // the starting table is locked!

  return SUCCESS;
}


// data is allocated and must be freed after use.
int nlkup_get_range_around( const unsigned char *nbr, const int nbr_before, const int nbr_after, 
			    int *data_len, NumberAliasStruct *data[]) {

  *data_len = 0;
  *data = NULL;

  if ( nbr_before + nbr_after == 0) {
    return FAILURE;
  }

  int idx = get_index( nbr);
  if ( idx < 0) {
    log_msg( ERR, "nlkup_get_range_around: bad index %d: %s\n", idx, nbr);
    return FAILURE;
  }

  LkupTblEntry key;
  memset( &key, 0, sizeof( LkupTblEntry));

  if ( compress_to_buf( nbr, PREFIX_LENGTH, strlen( nbr)-PREFIX_LENGTH, key.postfix, POSTFIX_LENGTH) < 0) {
    log_msg( ERR, "nlkup_get_range_around: failure to compress %s\n", nbr);

    unlock_table( index_table, idx);
    return FAILURE;
  }

  EntryAddressStruct nearest_entry;

  LkupTbl *t = index_table[idx].table;

  lock_table( index_table, idx);

  int e_idx = search_entry_in_table( t, &key);

  if ( e_idx < 0) { // no such entry. take nearest neighbor

    unlock_table( index_table, idx);

    int i_idx = -e_idx-1; // insertion point

    // find_nearest_entry if successful locks the relevant table...
    if ( find_nearest_entry( idx, i_idx, TRUE, &nearest_entry) < 0) {
      if ( find_nearest_entry( idx, i_idx, FALSE, &nearest_entry) < 0) {
	log_msg( WARN, "no nearest entry for %s\n", nbr);
	return FAILURE;
      }
    } 

  } else { // found the nbr
    nearest_entry.lkup_tbl_idx = e_idx;
    nearest_entry.idx_tbl_idx = idx;
  }

  // table with nearest entry is locked...
  assert( nearest_entry.lkup_tbl_idx >= 0 && nearest_entry.idx_tbl_idx >= 0);

  // allocate storage to copy out numbers and move along the needed blocks, locking
  // blocks as we move along up and down the set of blocks. This avoids need for global
  // locking. Need to sort copied out numbers since ordering is random - which is the price
  // we pay for locking table per table and not using a global lock.
  RangeDataStruct range;
  memset( &range, 0, sizeof( range));
  range.data_sz = nbr_before + nbr_after + 1;

  if ( copy_range_data( &range, &nearest_entry, nbr_before, nbr_after) < 0) {
    log_msg( WARN, "copy_range_data failure\n", nbr);
    return FAILURE;
  }

  // unlock table with nearest matching entry
  unlock_table( index_table, nearest_entry.idx_tbl_idx);

  sort_range_data( &range);

  // set the out parameters...
  *data = range.data;
  *data_len = range.data_len;

  // indicate if not enough data has been copied out...
  if ( range.data_len < nbr_before + nbr_after + 1) {
    return NOT_ENOUGH_DATA;
  }

#if 0

  EntryAddressStruct start_entry;
  EntryAddressStruct end_entry;
  
  memset( &start_entry, 0, sizeof( start_entry));
  memset( &end_entry, 0, sizeof( end_entry));

  log_msg( DEBUG, "nearest_entry: lkup_tbl_idx %d idx_tbl_idx %d\n", nearest_entry.lkup_tbl_idx, nearest_entry.idx_tbl_idx);

  int rc = 0;
  if (( rc = find_start_entry( &nearest_entry, nbr_before, &start_entry)) < 0) {
    if ( rc != NOT_ENOUGH_DATA) {
      log_msg( ERR, "no starting entry found for %s\n", nbr);
      return FAILURE;
    }
  } 
  
  if (( rc = find_end_entry( &nearest_entry, nbr_after, &end_entry)) < 0) {
    if ( rc != NOT_ENOUGH_DATA) {
      log_msg( ERR, "no ending entry found for %s\n", nbr);
      return FAILURE;
    }
  }

  log_msg( DEBUG, "start_entry: lkup_tbl_idx %d idx_tbl_idx %d\n", start_entry.lkup_tbl_idx, start_entry.idx_tbl_idx);
  log_msg( DEBUG, "end_entry: lkup_tbl_idx %d idx_tbl_idx %d\n", end_entry.lkup_tbl_idx, end_entry.idx_tbl_idx);

  if ( copy_numbers( &start_entry, &end_entry, data_len, data, nbr_before+nbr_after+1) < 0) {
    *data_len = 0;
    *data = NULL;
    return FAILURE;
  }

#endif

  return SUCCESS;
}

int nlkup_get_range( const unsigned char *nbr, const unsigned char *postfix_range_len, LkupTblPtr *table) {

  *table = NULL;

  if ( nbr == NULL || strlen( nbr) < PREFIX_LENGTH || postfix_range_len == 0 || strlen( postfix_range_len) == 0) {
    return FAILURE;
  }

  int pfx_len = atoi( postfix_range_len);
  if ( pfx_len < 0) {
    log_msg( ERR, "nlkup_get_range: negative postfix range %s\n", postfix_range_len);
    return FAILURE;
  }

  // expand the nbr with pfx_lens "0" and "9": this gives us a range.
  unsigned char from_nbr[MAX_NBR_LENGTH+1];
  unsigned char to_nbr[MAX_NBR_LENGTH+1];
  
  memset( from_nbr, 0, sizeof( from_nbr));
  memset( to_nbr, 0, sizeof( to_nbr));

  strncpy( from_nbr, nbr, sizeof( from_nbr));
  strncpy( to_nbr, nbr, sizeof( to_nbr));

  int i = 0;
  int nbr_len = strlen( nbr);
  for ( i = nbr_len; (i < nbr_len + pfx_len) && (i <= MAX_NBR_LENGTH); i++) {
    from_nbr[i] = '0';
    to_nbr[i] = '9';
  }

  log_msg( DEBUG, "nlkup_get_range: from_nbr %s to_nbr %s\n", from_nbr, to_nbr);

  // find the indices of nbr+"00000" and nbr+"99999"
  int idx = get_index( nbr);
  if ( idx < 0) {
    log_msg( ERR, "nlkup_get_range: bad index %d: %s\n", idx, nbr);
    return FAILURE;
  }

  int status = SUCCESS;
  lock_table( index_table, idx);

  LkupTbl *t = index_table[idx].table;

  // no table, no data...
  if ( t == NULL) {
    goto out;
  }

  LkupTblEntry from_key;
  LkupTblEntry to_key;

  if ( set_up_search_key( &from_key, from_nbr) != SUCCESS || 
       set_up_search_key( &to_key, to_nbr) != SUCCESS) {
    log_msg( ERR, "nlkup_get_range: setting up search keys %s %s\n", from_nbr, to_nbr);
    status = FAILURE;
    goto out;
  }

  int from_idx = search_entry_in_table( t, &from_key);
  int to_idx = search_entry_in_table( t, &to_key);

  // log_msg( DEBUG, "nlkup_get_range: from_idx %d to_idx %d\n", from_idx, to_idx);

  // idx < 0: not found. index = -insertion_point - 1
  if ( from_idx < 0) { // insertion point is *after* next smaller entry
    from_idx = -from_idx-1;
  }
  if ( to_idx < 0) { // insertion point is *after* last largest entry. hence -1 once more
    to_idx = -to_idx-1-1;
  }

  log_msg( DEBUG, "nlkup_get_range: from_idx %d to_idx %d\n", from_idx, to_idx);

  assert( from_idx >= 0 && from_idx < t->table_len);
  assert( to_idx >= 0 && to_idx < t->table_len);
  assert( from_idx <= to_idx);

  // copy data into a newly allocated table, [from_idx..to_idx]
  LkupTbl *new_table = copy_lkup_tbl_range( t, from_idx, to_idx);

  *table = new_table;

 out:
  unlock_table( index_table, idx);
  return status;

}


JSON_Buffer number_aliases_to_json( const int data_len, const NumberAliasStruct *data) {

  if ( data_len == 0) {
    return NULL;
  }

  JSON_Buffer json = json_new();

  int rc = 0;
  int i = 0;

  // the JSON we generate here is possibly specific for datatables-dataeditor usage
  // we return an array of objects rather than an array of arrays.
  // an object fields are labelled.
  rc = json_begin_obj( json, NULL);

  rc = json_begin_arr( json, "data");

  for ( i = 0; i < data_len; i++) {
    rc = json_begin_obj( json, NULL);
    rc = json_append_str( json, "number", data[i].nbr);
    rc = json_append_str( json, "alias", data[i].alias);
    rc = json_end_obj( json);
  }
  
  rc = json_end_arr( json);

  rc = json_end_obj( json);
  return json;
}

// attempts to retrieve the block of given number. must be mem_freed() if non NULL
int nlkup_get_block( const unsigned char *nbr, LkupTblPtr *table) {
  int idx = get_index( nbr);
  if ( idx < 0) {
    log_msg( ERR, "nlkup_get_block: bad index %d: %s\n", idx, nbr);
    return FAILURE;
  }

  lock_table( index_table, idx);
  LkupTbl *t = index_table[idx].table;
  if ( t == NULL) {
    *table = NULL;
  } else {
    *table = copy_lkup_tbl( t);
  }
  unlock_table( index_table, idx);
  return SUCCESS;
}

int nlkup_dump_file( const unsigned char *fn, int binary) {
  return dump_all_fn( index_table, fn, binary);
}

int nlkup_restore_file( const unsigned char *fn, int binary) {
  return restore_all_fn( index_table, fn);
}

static int check_nbr( const char *nbr) {
  if ( !all_digits( nbr) || strlen( nbr) >= MAX_NBR_LENGTH) {
    log_msg( ERR, "process_file: illegal number %s\n", nbr);
    return 0;
  }
  return 1;
}

static int process_file( IdxTblEntry index_table[], FILE *f) {

  int s = 0;

  char str_buf[1024];

  while ( fgets( str_buf, sizeof( str_buf), f) != NULL) {
    
    if ( strlen( str_buf) == 0 || all_spaces( str_buf) || str_buf[0] == '#') {
      continue;
    }

    int nbr_tokens;
    char **tokens = str_split( str_buf, '=', &nbr_tokens);
    if ( tokens == NULL) {
      log_msg( ERR, "process_file: str_split returned NULL\n");
      s = -1;
      continue;
    }
    if ( nbr_tokens < 2 || tokens[0] == NULL || tokens[1] == NULL) {
      log_msg( ERR, "process_file: str_split returned not enough tokens\n");      
      s = -1;
      continue;
    }

    if ( strcasecmp( tokens[0], "add") == 0) {

      if ( nbr_tokens < 3 || IS_NULL( tokens[1]) || IS_NULL( tokens[2])) {
	log_msg( ERR, "process_file: illegal add %s\n", str_buf);
	s = -1;
      } else {
	char *nbr = tokens[1];
	char *alias = tokens[2];

	if ( check_nbr( nbr) && check_nbr( alias)) {
	  enter_entry( index_table, nbr, alias);
	} else {
	  log_msg( ERR, "process_file: bad number or alias %s\n", str_buf);
	  s = -1;
	}

      }

    } else if ( strcasecmp( tokens[0], "del") == 0) {

	char *nbr = tokens[1];

	if ( check_nbr( nbr)) {
	  delete_entry( index_table, nbr);
	} else {
	  log_msg( ERR, "process_file: bad number %s\n", str_buf);
	}


    } else {
      log_msg( ERR, "process_file: unhandled command %s\n", str_buf);
      s = -1;
    }
    
    free_tokens( tokens);
  }
  
  return s;

}

// processing a file of add or del commands
int nlkup_process_file( const unsigned char *fn) {

  if ( IS_NULL( fn))
    return FAILURE;

  FILE *f = fopen( fn, "r");
  if ( f == NULL) {
    log_msg( ERR, "failure to read-open %s\n", fn);
    return FAILURE;
  }

  log_msg( INFO, "starting process file from %s\n", fn);

  int s = process_file( index_table, f);

  log_msg( INFO, "processing done\n");
  
  if ( fclose( f) < 0) {
    log_msg( ERR, "failure to close %s\n", fn);
    return FAILURE;
  }

  return s;

}


#ifdef _NLKUP_MAIN_

int main( int argc, char **argv) {

#if 0

  void *pp = mem_alloc( 100);
  mem_free( pp);
  exit( 0);

  LkupTblEntry *e = alloc_lkup_tbl_entry();
  free_lkup_tbl_entry( e);

  LkupTbl *t = alloc_lkup_tbl();
  free_lkup_tbl( t);

#endif

#if 0

  fprintf( stderr, "sizeof( LkupTblEntry) = %ld\n", sizeof( LkupTblEntry));
  alloc_lkup_tbl_in_index( index_table, 100);
  alloc_lkup_tbl_in_index( index_table, 200);
  alloc_lkup_tbl_in_index( index_table, 12345);
  alloc_lkup_tbl_in_index( index_table, 12346);

  free_lkup_tbl_in_index( index_table, 12346);

#endif

#if 0

  {
    test_compression( "12345678901234");
    test_compression( "123");
    test_compression( "1232341204812034810239481023948123");
    test_compression( "12345AA");

  }

  enter_entry( index_table, "1234561000", "1234562000");
  enter_entry( index_table, "1234561005", "1234562005");
  enter_entry( index_table, "1234561010", "1234562010");
  enter_entry( index_table, "1234561015", "1234562015");
  enter_entry( index_table, "1234561020", "1234562020");
  enter_entry( index_table, "1234561025", "1234562025");
  enter_entry( index_table, "1234561030", "1234562030");
  enter_entry( index_table, "1234561035", "1234562035");
  enter_entry( index_table, "1234561012", "1234562012");
  enter_entry( index_table, "1234561033", "1234562033");
  enter_entry( index_table, "1234561003", "1234562003");
  enter_entry( index_table, "1234561002", "1234562002");

  // enter_entry( index_table, "1234561012", "1234562112");
  // enter_entry( index_table, "1234561033", "1234562133");
  // enter_entry( index_table, "1234561003", "1234562103");
  // enter_entry( index_table, "1234561002", "1234562102");

  dump_table( index_table, 123456, stderr, FALSE);

  test_search( "1234561000");
  test_search( "1234561012");
  test_search( "1234561003");
  test_search( "1234561002");
  test_search( "1234561033");
  test_search( "1234561014");

  delete_entry( index_table, "1234561012");
  delete_entry( index_table, "1234561033");
  delete_entry( index_table, "1234561003");
  delete_entry( index_table, "1234561002");

  dump_table( index_table, 123456, stderr, FALSE);

  dump_all_fn( index_table, "dump.txt", FALSE);

#endif

  test_index( index_table);
  dump_all_fn( index_table, "dump.txt", FALSE);
  dump_all_fn( index_table, "dump.bin", TRUE);

  restore_all_fn( index_table, "dump.bin");
  dump_all_fn( index_table, "dump2.txt", FALSE);

  fprintf( stderr, "done\n");

}

#endif
