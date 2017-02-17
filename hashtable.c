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

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <pthread.h>

#include "hashtable.h"

#define LOCK( t)         pthread_mutex_lock( &(t)->mutex)
#define UNLOCK( t)       pthread_mutex_unlock( &(t)->mutex)
#define DESTROY_LOCK( t) pthread_mutex_destroy( &(t)->mutex)
#define CREATE_LOCK( t)  pthread_mutex_init( &(t)->mutex, NULL)

typedef struct _ht_elem {
  struct _ht_elem *next;
  void *key;
  void *data;
} HT_ElemStruct, *HT_Elem;

typedef struct _ht_table {
  HT_Elem *table;
  int size;
  HT_CompareTo compare_callback;
  HT_Hash hash_callback;
  pthread_mutex_t mutex;
} HT_TableStruct;

HT_Table HT_new( unsigned int size,
		 HT_CompareTo compare_to_callback,
		 HT_Hash hash_callback) {

  assert( size > 0 && compare_to_callback != NULL && hash_callback != NULL);

  HT_TableStruct *t = calloc( 1, sizeof( HT_TableStruct));

  if ( t == NULL) {
    return NULL;
  }
  
  t->table = calloc( size, sizeof( HT_Elem));
  if ( t->table == NULL) {
    free( t);
    return NULL;
  }

  t->size = size;
  t->compare_callback = compare_to_callback;
  t->hash_callback = hash_callback;

  if ( CREATE_LOCK( t) != 0) {
    free( t->table);
    free( t);
    t->table = NULL; 
    t = NULL;
  }

  return t;
}

void HT_free( HT_Table t) {
  
  if ( t == NULL)
    return;
  
  HT_TableStruct *table = (HT_TableStruct *) t;

  int i = 0;

  for ( i = 0; i < table->size; i++) {
    if ( table->table[i] == NULL) {
      continue;
    }
    
    HT_Elem e_ptr = table->table[i];
    while ( e_ptr != NULL) {
      HT_Elem cur = e_ptr;
      e_ptr = cur->next;

      free( cur->key); cur->key = NULL;
      free( cur->data); cur->data = NULL;
      
      free( cur);
    }
    table->table[i] = NULL;
  }
  
  free( table->table);
  table->table = NULL;
  table->size = 0;

  DESTROY_LOCK( table);

  free( table);
  
}

static HT_Elem alloc_elem( const void *key, const void *data) {
  
  assert( key != NULL && data != NULL);

  HT_Elem e = calloc( 1, sizeof( HT_ElemStruct));
  if ( e == NULL) 
    return NULL;

  e->next = NULL;
  e->key = ( void * ) key;
  e->data = ( void * ) data;
  return e;
}

static void free_elem( HT_Elem e) {
  e->next = NULL;
  free( e->key); e->key = NULL;
  free( e->data); e->data = NULL;
  free( e);
}

int HT_insert( HT_Table t, const void *key, const void *data, const int overwrite) {

  assert( t != NULL && key != NULL && data != NULL);

  HT_TableStruct *table = (HT_TableStruct *) t;

  LOCK( table);

  unsigned long h_idx = (table->hash_callback) (key);
  h_idx = h_idx % table->size;
  assert( h_idx >= 0 && h_idx < table->size);

  if ( table->table[h_idx] == NULL) { // first entry
    int s = 1;
    if ( (table->table[h_idx] = alloc_elem( key, data)) == NULL) {
      s = -1;
    }
    UNLOCK( table);
    return s;
  }

  // check for duplicates by chasing along the linked list
  HT_Elem cur = table->table[h_idx];
  while ( cur != NULL) {
    if ( (table->compare_callback)( cur->key, key) == 0) { // duplicate
      int s = 1;

      if ( overwrite == 0) {
	s = -1;
      } else {
	free( cur->data);
	cur->data = (void *) data;
      }

      UNLOCK( table);
      return s;
    }
    cur = cur->next;
  }

  // no duplicate: allocate new element and insert into list
  cur = alloc_elem( key, data);
  cur->next = table->table[h_idx];
  table->table[h_idx] = cur;
  
  UNLOCK( table);

  return 1;
}

int HT_delete( HT_Table t, const void *key) {
  assert( t != NULL && key != NULL);

  HT_TableStruct *table = (HT_TableStruct *) t;

  LOCK( table);

  unsigned long h_idx = (table->hash_callback) (key);
  h_idx = h_idx % table->size;
  assert( h_idx >= 0 && h_idx < table->size);

  if ( table->table[h_idx] == NULL) { // no entry
    UNLOCK( table);
    return 1;
  }

  // chase along the list
  HT_Elem cur = table->table[h_idx];
  HT_Elem prev = NULL;

  while ( cur != NULL) {
    
    HT_Elem next = cur->next;

    if ( (table->compare_callback)( cur->key, key) == 0) { // found
      
      if ( prev == NULL) { // at head
	table->table[h_idx] = next;
      } else {
	prev->next = next;
      }
      
      free_elem( cur);
	
      UNLOCK( table);

      return 1;
    }

    prev = cur;
    cur = next;
  }

  UNLOCK( table);
  return 1;

}

void *HT_lookup( HT_Table t, const void *key) {
  assert( t != NULL && key != NULL);

  HT_TableStruct *table = (HT_TableStruct *) t;

  LOCK( table);

  unsigned long h_idx = (table->hash_callback) (key);
  h_idx = h_idx % table->size;
  assert( h_idx >= 0 && h_idx < table->size);

  if ( table->table[h_idx] == NULL) { // no entry
    UNLOCK( table);
    return NULL;
  }

  // chase along the list
  HT_Elem cur = table->table[h_idx];
  while ( cur != NULL) {
    if ( (table->compare_callback)( cur->key, key) == 0) { // found
      UNLOCK( table);
      return cur->data;
    }
    cur = cur->next;
  }

  UNLOCK( table);
  return NULL;
}

/* D. J. Bernstein hash function */
unsigned long HT_DJB_hash(const char* cp)
{
    unsigned long hash = 5381;
    while (*cp) {
        hash = 33 * hash ^ (unsigned char) *cp++;
    }
    return hash;
}

int HT_iterate( HT_Table t, HT_IteratorCallback callback, void *arg) {
  HT_TableStruct *table = (HT_TableStruct *) t;
  int s = 0;

  LOCK( table);

  int i = 0;
  int quit = 0;

  for ( i = 0; (i < table->size) && !quit; i++) {

    HT_Elem e_ptr = table->table[i];
    HT_Elem prev = NULL;

    while ( e_ptr != NULL) {

      int rc = (callback) (e_ptr->key, e_ptr->data, arg);

      if ( rc == HT_STOP_ITERATION) {
	quit = 1;
	break;
      } else if ( rc == HT_DELETE_KEY) {

	HT_Elem next = e_ptr->next;
	
	if ( prev == NULL) {
	  table->table[i] = next;
	} else {
	  prev->next = next;
	}

	free_elem( e_ptr);

	// prev unchanged!
	e_ptr = next;

      } else { // else we continue
	prev = e_ptr;
	e_ptr = e_ptr->next;
      }
    }

  }
  UNLOCK( table);
  return s;
}


#if 0
int main( int argc, char **argv) {

  HT_Table table = HT_new( 257, (HT_Compare_To) strcmp, (HT_Hash) HT_DJB_hash);

  unsigned char *key = calloc( sizeof( unsigned char), 64);
  unsigned char *data = calloc( sizeof( unsigned char), 64);

  strncpy( key, "the key", strlen( "the key"));

  if ( HT_lookup( table, key) != NULL) {
    fprintf( stderr, "lookup of non existing key failed\n");
  }

  if ( HT_insert( table, key, data, 0) < 0) {
    fprintf( stderr, "insert failed\n");
  }

  if ( HT_insert( table, key, data, 0) >= 0) {
    fprintf( stderr, "duplicate insert without overwrite failed\n");
  }

  if ( HT_insert( table, key, data, 1) < 0) {
    fprintf( stderr, "duplicate insert with overwrite failed\n");
  }

  if ( HT_lookup( table, key) == NULL) {
    fprintf( stderr, "lookup of existing key failed\n");
  }

  if ( HT_delete( table, key) < 0) {
    fprintf( stderr, "delete failed\n");
  }

  if ( HT_delete( table, key) < 0) {
    fprintf( stderr, "delete failed\n");
  }

  HT_free( table);
}
#endif
