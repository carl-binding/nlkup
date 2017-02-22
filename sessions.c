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
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <ftw.h>

// set include-path!
#include <microhttpd.h>

#include "config.h"
#include "utils.h"
#include "json.h"
#include "nlkup.h"
#include "sessions.h"
#include "logger.h"
#include "hashtable.h"

/**
 * Name of our cookie.
 */
#define COOKIE_NAME "session_id"

// lookup table for sessions
static HT_Table sessions_table = NULL;

int sessions_init() {

  sessions_table = HT_new( 257, (HT_CompareTo) strcmp, (HT_Hash) HT_DJB_hash);
  if ( sessions_table == NULL) {
    return -1;
  }
  return 0;
}

Session sessions_get( struct MHD_Connection *connection) {

  const char *cookie = MHD_lookup_connection_value (connection,
						    MHD_COOKIE_KIND,
						    COOKIE_NAME);
  Session session = NULL;
  if (cookie != NULL) {
    /* find existing session */
    session = (Session) HT_lookup( sessions_table, cookie);

    if ( session != NULL) {
      session->ref_cnt++;
      return session;
    }

  }

  /* create fresh session */
  session = calloc (1, sizeof (SessionStruct));
  if (NULL == session) {
    log_msg( INFO, "sessions_get: calloc error\n");
    return NULL;
  }

  /* not a super-secure way to generate a random session ID,
     but should do for a simple example... */
  snprintf (session->session_id,
	    sizeof (session->session_id),
	    "%lX%X%X%X%X",
	    (long) time( NULL),
	    (unsigned int) rand (),
	    (unsigned int) rand (),
	    (unsigned int) rand (),
	    (unsigned int) rand ());
  session->ref_cnt++;
  time ( &session->last_access);

  HT_insert( sessions_table, strdup( session->session_id), session, 0);

  return session;
}

// returns cookie expiration date some time from now into the future.
static int get_expiration_date( unsigned char *buf, int buf_sz) {
  time_t expire_time;
  struct tm *gmt_time;
  int cnt = 0;
 
  int session_time_out = CFG_get_int( "session_time_out", DEFAULT_SESSION_TIME_OUT);

  // Getting current time of system
  expire_time = time (NULL) + session_time_out;
 
  // Converting current time to GMT time
  gmt_time = gmtime (&expire_time);
 
  // assuming locale is US we generate cookie expiration string which
  // is formatted its own weird way.
  cnt += strftime (buf, buf_sz, "%a, %d %b %Y %H:%M:%S GMT", gmt_time);

  return cnt;

}

int sessions_add_cookie( Session session, struct MHD_Response *response) {

  int cnt = 0;

  char cstr[512];
  memset( cstr, 0, sizeof( cstr));

  cnt += snprintf( cstr+cnt, sizeof (cstr) - cnt, "%s=%s", COOKIE_NAME, session->session_id);

#if 0
  char date_str[256];
  memset( date_str, 0, sizeof( date_str));
  get_expiration_date( date_str, sizeof( date_str));
  cnt += snprintf( cstr+cnt, sizeof (cstr) - cnt, "; Expires=%s", date_str);
#endif

  cnt += snprintf( cstr+cnt, sizeof( cstr) - cnt, "; Path=/");
  // cnt += snprintf( cstr+cnt, sizeof( cstr) - cnt, "; SameSite=Lax");
  // cnt += snprintf( cstr+cnt, sizeof( cstr) - cnt, "; Domain=localhost");

  if (MHD_NO == MHD_add_response_header (response, MHD_HTTP_HEADER_SET_COOKIE, cstr)) {
    log_msg( ERR, "Failed to set session cookie header!\n");
    return -1;
  }

  session->cookie_sent = TRUE;

  return 0;
}

int sessions_has_cookie( Session session) {
  return session->cookie_sent;
}

// packing up some data to determine session expiration.
typedef struct {
  int time_out;
  time_t now;
} ExpirationCallbackArgStruct;

// callback for hash-table traversal to remove expired sessions
static int expiration_call_back( const void *key, const void *data, void *arg) {

  char *k = (char *) key;
  Session s = (Session) data;

  log_msg( DEBUG, "expiration_call_back %s\n", key);

  ExpirationCallbackArgStruct *cb_arg = (ExpirationCallbackArgStruct *) arg;

  if ( difftime( cb_arg->now, s->last_access) > cb_arg->time_out) {
    // an expired session
    log_msg( DEBUG, "deleting session %s\n", s->session_id);
    return HT_DELETE_KEY;
  }

  return 0;
}

// called periodically to clean out expired sessions
int sessions_expire( int time_out) {

  ExpirationCallbackArgStruct arg;

  arg.time_out = time_out;
  time( &arg.now);
  
  HT_iterate( sessions_table, expiration_call_back, &arg);
}
