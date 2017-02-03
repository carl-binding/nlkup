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

/*
  a reentrant queue. doubly linked list. insertion at tail, retrieval at head.
  insertion at head possible to take precedence
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mem.h"
#include "queue.h"

Q_QueuePtr Q_alloc() {
  Q_QueuePtr q = (Q_QueuePtr) mem_alloc( sizeof( Q_Queue));

  pthread_mutex_init( &(q->mutex), NULL);
  pthread_cond_init( &(q->not_empty), NULL);
  q->head = q->tail = NULL;

  return q;
}

void Q_free( Q_QueuePtr q) {

  pthread_mutex_lock( &q->mutex);

  Q_ElementPtr p = q->tail;
  while ( p != NULL) {
    Q_ElementPtr nxt = p->next;

    mem_free( p->data);

    p->data = p->next = p->prev = NULL;
    mem_free( p);

    p = nxt;
  }

  pthread_mutex_unlock( &q->mutex);

  pthread_cond_destroy( &(q->not_empty));
  pthread_mutex_destroy( &(q->mutex));

  q->head = q->tail = NULL;

  mem_free( q);

}


static Q_ElementPtr alloc_elem( void *data) {
  Q_ElementPtr e = mem_alloc( sizeof( Q_Element));
  e->data = data;
  e->prev = e->next = NULL;
  return e;
}

// insert data into queue at tail or at head.
int Q_put( Q_QueuePtr q, void *data, int at_head) {

  int s = 0;

  pthread_mutex_lock( &q->mutex);

  Q_ElementPtr e = alloc_elem( data);

  if ( at_head) {
    
    e->prev = q->head;
    e->next = NULL;

    if ( q->head != NULL) {
      q->head->next = e;
    } else {
      assert( q->tail == NULL);
    }
    
    q->head = e;
    
    if ( q->tail == NULL) {
      q->tail = e;
    }

  } else { // at tail

    e->next = q->tail;
    e->prev = NULL;

    if ( q->tail != NULL) {
      q->tail->prev = e;
    } else {
      assert( q->head == NULL);
    }

    q->tail = e;

    if ( q->head == NULL) {
      q->head = e;
    }
    
  }

  pthread_cond_signal( &q->not_empty);

  pthread_mutex_unlock( &q->mutex);

  return s;

}

// gets element at queue head
void *Q_get( Q_QueuePtr q) {

  pthread_mutex_lock( &q->mutex);
  
  while ( q->head == NULL) {
    pthread_cond_wait( &q->not_empty, &q->mutex);
  }

  Q_ElementPtr e = q->head;
  
  assert( e->next == NULL);

  q->head = e->prev;
  if ( e->prev != NULL) {
    e->prev->next = NULL;
  }

  if ( q->tail == e) {
    q->tail = NULL;
    assert( q->head == NULL);
  }

  pthread_mutex_unlock( &q->mutex);

  e->prev = e->next = NULL;
  void *d = e->data;
  e->data = NULL;
  mem_free( e);

  return d;
}
