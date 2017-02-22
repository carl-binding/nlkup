#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "json.h"

#define JSON_OBJECT 1
#define JSON_ARRAY 2

typedef struct {
  char type;
  char first_value;
} JSON_LevelStruct;

#define MAX_NBR_LEVELS 16
#define JSON_BUFR_SZ 512

#define FALSE 0
#define TRUE 1

typedef struct {

  char *buf;
  int buf_sz;
  int buf_cnt;

  JSON_LevelStruct levels[MAX_NBR_LEVELS];
  char level_top;

} JSON_BufferStruct;

JSON_Buffer json_new() {
  JSON_BufferStruct *j = calloc( sizeof( JSON_BufferStruct), 1);
  j->buf = calloc( sizeof( char), JSON_BUFR_SZ);
  j->buf_sz = JSON_BUFR_SZ;
  j->level_top = j->buf_cnt = 0;
  return j;
}

// free storage. if with_buffer the string buffer is freed.
void json_free( JSON_Buffer b, int with_buffer) {
  JSON_BufferStruct *j = (JSON_BufferStruct *) b;
  assert( j->level_top == 0);
  if ( with_buffer) {
    free( j->buf);
    j->buf = NULL;
  }
  free( j);
}

char *json_get( JSON_Buffer b) {
  JSON_BufferStruct *j = (JSON_BufferStruct *) b;
  return j->buf;
}

int json_get_length( JSON_Buffer b) {
  JSON_BufferStruct *j = (JSON_BufferStruct *) b;
  return j->buf_cnt;
}

static int grow_bufr( JSON_BufferStruct *j) {
    char *nb = calloc( sizeof( char), j->buf_sz + JSON_BUFR_SZ);
    if ( nb == NULL)
      return -1;
    memcpy( nb, j->buf, j->buf_cnt);
    free( j->buf);
    j->buf = nb;
    j->buf_sz += JSON_BUFR_SZ;
    return 0;
}

static int append_str( JSON_BufferStruct *j, const char *str) {

  if ( str == NULL) return 0;
  int s_len = strlen( str);
  if ( s_len == 0) return 0;

  if ( j->buf_cnt + s_len >= j->buf_sz) { // grow buffer
    if ( grow_bufr( j) < 0) {
      return -1;
    }
  }

  int cnt = snprintf( j->buf+j->buf_cnt, j->buf_sz-j->buf_cnt, "%s", str);

  if ( cnt < 0) 
    return -1;

  j->buf_cnt += cnt;

  return cnt;
}

static int push_level( JSON_Buffer b, char type, const char *name) {
  JSON_BufferStruct *j = (JSON_BufferStruct *) b;

  if ( j->level_top >= MAX_NBR_LEVELS) {
    return -1;
  }

  // level_top points to next free slot...
  j->levels[j->level_top].type = type;
  j->levels[j->level_top].first_value = TRUE;

  // get current level
  int idx = j->level_top-1;

  if ( idx >= 0) {
    if ( j->levels[idx].first_value == TRUE) {
      j->levels[idx].first_value = FALSE;
    } else { // append comma
      append_str( j, ", ");
    }
  } // else it's first push_level call, no comma.

  if ( name != NULL) { // prepend quoted name if name non-NULL
    append_str( j, "\"");
    append_str( j, name);
    append_str( j, "\": ");
  }

  char *delim = NULL;

  if ( type == JSON_OBJECT) {
    delim = "{ ";
  } else {
    delim = "[ ";
  }

  append_str( j, delim);

  j->level_top++;

  return 0;
}


static int pop_level( JSON_Buffer b, char type) {
  JSON_BufferStruct *j = (JSON_BufferStruct *) b;

  int idx = j->level_top-1;
  if ( j->levels[idx].type != type) {
    fprintf( stderr, "mismatching level pop\n");
    assert( 0);
    return -1;
  }

  char *delim = NULL;

  if ( type == JSON_OBJECT) 
    delim = " }";
  else
    delim = " ]";

  append_str( j, delim);

  j->level_top--;

  return 0;
}

int json_begin_arr( JSON_Buffer b, const char *name) {
  return push_level( b, JSON_ARRAY, name);
}

int json_end_arr( JSON_Buffer b) {
  return pop_level( b, JSON_ARRAY);
}

int json_begin_obj( JSON_Buffer b, const char *name) {
  return push_level( b, JSON_OBJECT, name);
}

int json_end_obj( JSON_Buffer b) {
  return pop_level( b, JSON_OBJECT);
}

int json_append_int( JSON_Buffer b, const char *name, const int val) {

  // turn integer into a string
  char i_buf[64];
  memset( i_buf, 0, sizeof( i_buf));
  if ( snprintf( i_buf, sizeof( i_buf), "%d", val) < 0) {
    return -1;
  }

  // and append string
  return json_append_str( b, name, i_buf);

}

int json_append_str( JSON_Buffer b, const char *name, const char *val) {
  JSON_BufferStruct *j = (JSON_BufferStruct *) b;

  int idx = j->level_top-1;

  assert( idx >= 0); // must have had a push_level first...

  if ( j->levels[idx].first_value == TRUE) {
    j->levels[idx].first_value = FALSE;
  } else { // append comma
    if ( append_str( j, ", ") < 0) {
      return -1;
    }
  }

  if ( name != NULL) { // append quoted field name
    if ( append_str( j, "\"") < 0 ||
	 append_str( j, name) < 0 ||
	 append_str( j, "\": ") < 0) {
      return -1;
    }
  }

  if ( append_str( j, val) < 0) {
    return -1;
  }

  return 0;

}
