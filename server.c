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
#include "nlkup.h"
#include "logger.h"

// file name of configs
#define CONFIG_FILE_NAME "configs.txt"

// HTTP port default value
#define HTTP_PORT 8888

// distinguish between POST requests...
#define POST_MULTI_PART 2
#define POST_URL_ENCODED 3

#define GET             0
#define POST            1

#define MAX_NBR_PARAMS  16
#define POST_BUFFER_SIZE 4096
#define POST_RESPONSE_BUFFER_SIZE 512

// expect url of form "host:port/nlkup?..."
#define SERVER_URL "/nlkup"

#define SAVED_FILE_DIRECTORY "/tmp"
#define PATH_SEPARATOR       "/"

// GET cmd=alias number=1234567890
// GET cmd=block number=1234567890
// GET cmd=range number=123456 range_postfix_length=4

// POST cmd=delete number=123456890
// POST cmd=insert number=1234567890 alias=1234567890
// POST cmd=dump_file file_name=....
// POST cmd=restore_file file_name=.... binary=true|false

#define GEN_EMPTY_RESP() MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT)

#define CHECK_ALL_DIGITS( nbr) if ( !all_digits( nbr)) { \
  log_msg( ERR, "handle_get_request: badly formatted number %s\n", nbr); \
  *http_status = MHD_HTTP_BAD_REQUEST;					\
  response = GEN_EMPTY_RESP(); \
  goto out;		       \
}

// the range postfix combined with number must not exceed number length
#define CHECK_RANGE_POSTFIX_LENGTH( pfx_len_str, nbr) { \
  if ( !all_digits( pfx_len_str)) { \
    log_msg( ERR, "handle_get_request: badly formatted range postfix length %s\n", nbr); \
    *http_status = MHD_HTTP_BAD_REQUEST;					\
    response = GEN_EMPTY_RESP(); \
    goto out;		       \
  } \
  int pfx_len = atoi( (char *) pfx_len_str);	   \
  if ( strlen( nbr) + pfx_len > MAX_NBR_LENGTH ) { \
    log_msg( ERR, "handle_get_request:  range postfix length too large %s %s\n", nbr, pfx_len_str); \
    *http_status = MHD_HTTP_BAD_REQUEST;					\
    response = GEN_EMPTY_RESP(); \
    goto out;		       \
  } \
}

static struct MHD_Response *handle_get_request( struct MHD_Connection *connection, int *http_status) {

  long start_time = get_time_micro();
  long end_time = 0;

  const char *empty_string = "{}"; // empty JSON object

  const char *cmd = MHD_lookup_connection_value( connection, MHD_GET_ARGUMENT_KIND, "cmd");
  const char *nbr = MHD_lookup_connection_value( connection, MHD_GET_ARGUMENT_KIND, "number");

  struct MHD_Response *response = NULL;

  if ( cmd == NULL || nbr == NULL) {
    log_msg( ERR, "handle_get_request: missing query parameters\n");
    *http_status = MHD_HTTP_BAD_REQUEST;
    response = GEN_EMPTY_RESP();
    goto out;
  }

  if ( strlen( nbr) < PREFIX_LENGTH) {
    log_msg( ERR, "handle_get_request: number too short: %s\n", nbr);
    *http_status = MHD_HTTP_BAD_REQUEST;
    response = GEN_EMPTY_RESP();
    goto out;
  }

  if ( strcasecmp( cmd, "alias") == 0) { // lookup the alias of the given number

    CHECK_ALL_DIGITS( nbr);

    unsigned char *alias = NULL;
    int status = nlkup_search_entry( nbr, &alias);

    // generate some JSON
    unsigned char buffer[1024];
    memset( buffer, 0, sizeof( buffer));
    snprintf( buffer, sizeof( buffer), "{ \"alias\" : \"%s\", \"status\" : %d }\n", 
	      ((alias!=NULL?((char *) alias):"")), status);

    if ( alias != NULL) {
      mem_free( alias); alias = NULL;
    }

    // set HTTP status and create response
    *http_status = MHD_HTTP_OK;
    response = MHD_create_response_from_buffer( strlen( buffer), (void *) buffer, MHD_RESPMEM_MUST_COPY);
    goto out;

  } else if ( strcasecmp( cmd, "block") == 0) {

    CHECK_ALL_DIGITS( nbr);

    LkupTblPtr table = NULL;
    int status = nlkup_get_block( nbr, &table);

    unsigned char *buffer = table_to_json( table, status, nbr);
    
    *http_status = MHD_HTTP_OK;
    response = MHD_create_response_from_buffer( strlen( buffer), (void *) buffer, MHD_RESPMEM_MUST_FREE);
    
    if ( table != NULL) {
      free_lkup_tbl( table); table = NULL;
    }

    goto out;

  } else if ( strcasecmp( cmd, "range") == 0) {

    CHECK_ALL_DIGITS( nbr);

    const char *range_postfix_length = MHD_lookup_connection_value( connection, MHD_GET_ARGUMENT_KIND, "range_postfix_length");
    CHECK_RANGE_POSTFIX_LENGTH( range_postfix_length, nbr);

    LkupTblPtr table = NULL;
    int status = nlkup_get_range( nbr, range_postfix_length, &table);

    unsigned char *buffer = table_to_json( table, status, nbr);
    
    *http_status = MHD_HTTP_OK;
    response = MHD_create_response_from_buffer( strlen( buffer), (void *) buffer, MHD_RESPMEM_MUST_FREE);
    
    if ( table != NULL) {
      free_lkup_tbl( table); table = NULL;
    }

    goto out;

    
  } else {

    log_msg( ERR, "handle_get_request: unsupported command %s\n", cmd);

    *http_status = MHD_HTTP_BAD_REQUEST;
    response = GEN_EMPTY_RESP();
    goto out;

  }

 out:
  end_time = get_time_micro();  
  log_msg( INFO, "get request: %s %ld [usec]\n", (cmd!=NULL?cmd:""), (long) (end_time-start_time));
  return response;
}

typedef struct {
  unsigned char *key;
  unsigned char *value;
} KV_KeyValue, *KV_KeyValuePtr;

static KV_KeyValuePtr alloc_key_value( const unsigned char *key, const char *value, const int val_len) {

  assert( key != NULL && strlen( key) > 0);
  assert( value != NULL && val_len > 0);

  KV_KeyValuePtr kv = calloc( 1, sizeof( KV_KeyValue));

  kv->key = strdup( key); 
  kv->value = strdup( value);
  
  return kv;
}

static void free_key_value( KV_KeyValuePtr kv) {
  if ( kv == NULL) {
    return;
  }

  free( kv->key); 
  free( kv->value); 

  memset( kv, 0, sizeof( KV_KeyValue));
  free( kv);
}

// searches array of key-values until key is found or returns NULL
static unsigned char *get_key_value( const unsigned char *key, const KV_KeyValuePtr kvs[], int kvs_len) {

  assert( key != NULL && strlen( key) > 0);
  assert( kvs != NULL && kvs_len > 0);

  int i = 0;
  for ( i = 0; i < kvs_len; i++) {
    if ( kvs[i] == NULL) {
      return NULL;
    }
    if ( strcasecmp( key, kvs[i]->key) == 0) {
      return kvs[i]->value;
    }
  }
  return NULL;
}

// per request we create one of these structures. used to collect POST parameters
struct request_info_struct
{
  int request_type; // POST or GET
  struct MHD_PostProcessor *postprocessor;  // to clean things up.

  KV_KeyValuePtr key_values[MAX_NBR_PARAMS];  // POST parameters
  int kv_len;  // usage counter

  int post_req_type;  // multi-part vs url-encoded
};

static int insert_key_value( struct request_info_struct *req_info, 
			     const unsigned char *key, 
			     const unsigned char *value, 
			     const int val_len) {

  if ( req_info->kv_len == MAX_NBR_PARAMS) {
    log_msg( CRIT, "insert_key_value: out of heap space\n");
    return -1;
  }
  req_info->key_values[req_info->kv_len++] = alloc_key_value( key, value, val_len);
  return 0;
}

static unsigned char *get_key_value_from_req_info( struct request_info_struct *req_info,
						   const unsigned char *key) {

  return get_key_value( key, req_info->key_values, req_info->kv_len);
}

const char *greetingpage = "<html><body><h1>Welcome, world!</center></h1></body></html>";

static int
send_page (struct MHD_Connection *connection, const char *page)
{
  int ret;
  struct MHD_Response *response;


  response = MHD_create_response_from_buffer (strlen (page), (void *) page,
					      MHD_RESPMEM_PERSISTENT);
  if (!response) {
    return MHD_NO;
  }

  ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
  MHD_destroy_response (response);

  return ret;
}

static int save_posted_file( const unsigned char *input_elem_name_attr,
			     const char *file_name,
			     const char *data,
			     size_t data_size,
			     uint64_t data_offset) {

  int s = 0;

  log_msg( DEBUG, "save_posted_file \"%s\" \"%s\" %d %d\n", input_elem_name_attr, file_name==NULL?"":file_name, (int) data_size, (int) data_offset);

  if ( IS_NULL( input_elem_name_attr)) { // value of HTML INPUT element attribute "name"
    return -1;
  }

  if ( IS_NULL( file_name)) { // no file-name. nothing to be saved
    // there is possibly some data present,
    return 0;
  }
  if ( data_size <= 0) {
    return 0;
  }

  const char *saved_file_directory = CFG_get_str( "saved_file_directory", SAVED_FILE_DIRECTORY);
  char *fn = str_cat( saved_file_directory, PATH_SEPARATOR, file_name, NULL);
  if ( fn == NULL) {
    log_msg( CRIT, "save_posted_file: str_cat filename\n");
    return -1;
  }

  log_msg( DEBUG, "save_posted_file: fn = %s\n", fn);

  char *file_flags = NULL;
  if ( data_offset == 0) 
    file_flags = "w";
  else
    file_flags = "a";

  FILE *f = fopen( fn, file_flags);
  if ( f == NULL) {
    log_msg( ERR, "save_posted_file: fopen failure %s\n", fn);
    s = -1;
    goto out;
  }

  int n = 0;
  if (( n = fwrite( data, sizeof( char), data_size, f)) != data_size) {
    log_msg( WARN, "save_posted_file: not all data written %d %d\n", data_size, n);
    s = -1;
  }

  if ( fclose( f) < 0) {
    log_msg( WARN, "save_posted_file: fclose failure %s\n", fn);
  }

 out:
  if ( fn != NULL) free( fn);
  return s;
}

// is called multiple times for POST requests to figure our the key-value pairs contained in the request body
static int
iterate_post (void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
              const char *filename, const char *content_type,
              const char *transfer_encoding, const char *data, uint64_t off,
              size_t size)
{
  struct request_info_struct *req_info = coninfo_cls;

  log_msg( DEBUG, "iterate_post \"%s\" \"%s\" \"%s\" %d\n", key, data, filename==NULL?"":filename, (int) size);

  if ( req_info->request_type == POST && req_info->post_req_type == POST_MULTI_PART) {
    // posting multi-part. each file is one part
    // the "name" field in content-disposition becomes the key value....
    // this is the "name" given in HTML form input element
    // furthermore we should get a non-null file-name, plus the actual data, its size and an offset
    if ( save_posted_file( key, filename, data, size, off) < 0) {
      return MHD_NO;
    }
    return MHD_YES;
  }

  if ( size == 0 || IS_NULL( data)) {
    return MHD_NO;
  }

  if ( 0 == strcasecmp( key, "cmd")) {
    if ( insert_key_value( req_info, "cmd", data, size) < 0) {
      return MHD_NO;
    }
  }
  if ( 0 == strcasecmp( key, "number")) {
    if ( insert_key_value( req_info, "number", data, size) < 0) {
      return MHD_NO;
    }
  }
  if ( 0 == strcasecmp( key, "alias")) {
    if ( insert_key_value( req_info, "alias", data, size) < 0) {
      return MHD_NO;
    }
  }
  if ( 0 == strcasecmp( key, "file_name")) {
    if ( insert_key_value( req_info, "file_name", data, size) < 0) {
      return MHD_NO;
    }
  }
  if ( 0 == strcasecmp( key, "binary")) {
    if ( insert_key_value( req_info, "binary", data, size) < 0) {
      return MHD_NO;
    }
  }

  return MHD_YES;
}

// upcall when HTTP request fully processed
static void request_completed (void *cls, struct MHD_Connection *connection,
			       void **con_cls, enum MHD_RequestTerminationCode toe)
{
  struct request_info_struct *req_info = *con_cls;

  log_msg( DEBUG, "request_completed\n");

  if (NULL == req_info) {
    return;
  }

  if (req_info->request_type == POST) {
    // destroy postprocessor
    MHD_destroy_post_processor (req_info->postprocessor);
    req_info->postprocessor = NULL;

    // free key-value pairs if any
    int i = 0;
    for ( i = 0; i < req_info->kv_len; i++) {
      free_key_value( req_info->key_values[i]);
      req_info->key_values[i] = NULL;
    }

  }

  // release memory
  free (req_info);
  *con_cls = NULL;
}

// allocates a request info structure which is mainly used for POST requests to collect various key-value pairs
static int alloc_request_info( struct MHD_Connection *connection,
			       const char *method,
			       void **con_cls,
			       int post_req_type) {

  struct request_info_struct *con_info;

  con_info = calloc (1, sizeof (struct request_info_struct));
  if (NULL == con_info) {
    return MHD_NO;
  }

  if (0 == strcasecmp (method, "POST")) {
    con_info->postprocessor = MHD_create_post_processor (connection, POST_BUFFER_SIZE,
							 iterate_post, (void *) con_info);

    if (NULL == con_info->postprocessor)  {
      log_msg( ERR, "failure to MHD_create_post_processor. content-encoding missing?\n");
      free (con_info);
      return MHD_NO;
    }

    con_info->request_type = POST;
    con_info->post_req_type = post_req_type;


  } else if ( 0 == strcasecmp( method, "GET")) {

    con_info->request_type = GET;

  } else {

    log_msg( ERR, "unsupported method: %s\n", method);

    free( con_info);
    return MHD_NO;

  }


  *con_cls = (void *) con_info;
  return MHD_YES;
 
}

// checks if a POST has a supported content-type.
static int check_post_has_content_type( struct MHD_Connection *connection, 
					const char *method) {

  if ( strcasecmp( method, "POST") != 0) { // not a post
    return 1;
  }

  const char *content_type = MHD_lookup_connection_value ( connection, MHD_HEADER_KIND, "content-type");

  log_msg( DEBUG, "content-type: %s\n", content_type == NULL ? "null":content_type);

  if ( content_type == NULL) { // no such header field...
    return 0;
  }

  // microhttpd only supports some content-types for post-processor creation...
  // content-type for multi-part encoding also includes boundary
  if ( strncasecmp( content_type, MHD_HTTP_POST_ENCODING_FORM_URLENCODED, strlen( MHD_HTTP_POST_ENCODING_FORM_URLENCODED)) == 0) {
    return POST_URL_ENCODED;
  } else if ( strncasecmp( content_type, MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA, strlen( MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA)) == 0) {
    return POST_MULTI_PART;
  }

  // wrong content type...
  return 0;
}

static int gen_json_response( unsigned char buf[], int buf_sz, int status) {
  unsigned char *cp = buf;
  int cnt = 0;

  cnt += snprintf( cp+cnt, buf_sz-cnt, "{ \"status\" : %d }\n", status);

  return 0;
}

static struct MHD_Response *handle_post_request_url_encoded( struct MHD_Connection *connection,
							     struct request_info_struct *req_info,
							     int *http_status,
							     const char *cmd) 
{

  const char *empty_string = "{}"; // empty JSON object
  unsigned char *response_buffer = calloc( POST_RESPONSE_BUFFER_SIZE, sizeof( unsigned char));
  int response_status = 0;

  struct MHD_Response *response = NULL;

  *http_status = MHD_HTTP_OK;

  if ( IS_NULL( cmd)) {
    log_msg( WARN, "no or empty command in POST request\n");
    *http_status = MHD_HTTP_BAD_REQUEST;
    response = MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
    goto out;
  }

  if ( strcasecmp( cmd, "delete") == 0) {

    const unsigned char *number = get_key_value_from_req_info( req_info, "number");

    if ( IS_NULL( number)) {
      log_msg( WARN, "missing or empty number in POST delete request\n");
      *http_status = MHD_HTTP_BAD_REQUEST;
      response = MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
      goto out;
    }

    if ( strlen( number) < PREFIX_LENGTH) {
      log_msg( WARN, "number too short in POST delete request %s\n", number);
      response_status = NBR_TOO_SHORT;
    } else if ( !all_digits( number)) {
      log_msg( WARN, "number not all digits in POST delete request %s\n", number);
      response_status = ILLEGAL_NUMBER;
    } else {
      response_status = nlkup_delete_entry( number);
    }

    gen_json_response( response_buffer, POST_RESPONSE_BUFFER_SIZE, response_status);
    response = MHD_create_response_from_buffer( strlen( response_buffer), response_buffer, MHD_RESPMEM_MUST_FREE);

    goto out;

  } else if ( strcasecmp( cmd, "insert") == 0) {

    const unsigned char *number = get_key_value_from_req_info( req_info, "number");
    const unsigned char *alias = get_key_value_from_req_info( req_info, "alias");

    if ( IS_NULL( number) || IS_NULL( alias)) {
      log_msg( WARN, "missing or empty number or alias in POST insert request\n");
      *http_status = MHD_HTTP_BAD_REQUEST;
      response = MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);

      goto out;
    }

    if ( strlen( number) < PREFIX_LENGTH || strlen( alias) < PREFIX_LENGTH) {
      log_msg( WARN, "number or alias too short in POST insert request %s %s\n", number, alias);
      response_status = NBR_TOO_SHORT;
    } else if ( !all_digits( number) || !all_digits( alias)) {
      log_msg( WARN, "number or alias not all digits in POST insert request %s %s\n", number, alias);
      response_status = ILLEGAL_NUMBER;
    } else {
      response_status = nlkup_enter_entry( number, alias);
    }

    gen_json_response( response_buffer, POST_RESPONSE_BUFFER_SIZE, response_status);
    response = MHD_create_response_from_buffer( strlen( response_buffer), response_buffer, MHD_RESPMEM_MUST_FREE);

    goto out;
  } else if ( strcasecmp( cmd, "dump_file") == 0) {
    
    const unsigned char *file_name = get_key_value_from_req_info( req_info, "file_name");
    if ( IS_NULL( file_name)) {
      log_msg( WARN, "missing or empty file_name in POST dump_file request\n");
      *http_status = MHD_HTTP_BAD_REQUEST;
      response = MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
      goto out;
    }

    gen_json_response( response_buffer, POST_RESPONSE_BUFFER_SIZE, response_status);
    response = MHD_create_response_from_buffer( strlen( response_buffer), response_buffer, MHD_RESPMEM_MUST_FREE);
    goto out;

  } else if ( strcasecmp( cmd, "restore_file") == 0) {

    const unsigned char *file_name = get_key_value_from_req_info( req_info, "file_name");
    if ( IS_NULL( file_name) ) {
      log_msg( WARN, "missing or empty file_name in POST restore_file request\n");
      *http_status = MHD_HTTP_BAD_REQUEST;
      response = MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
      goto out;
    }

    gen_json_response( response_buffer, POST_RESPONSE_BUFFER_SIZE, response_status);
    response = MHD_create_response_from_buffer( strlen( response_buffer), response_buffer, MHD_RESPMEM_MUST_FREE);
    goto out;

  } else {

    log_msg( WARN, "unknown command in POST request: %s\n", cmd);
    *http_status = MHD_HTTP_BAD_REQUEST;
    response = MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
    goto out;
  }

 out:
  return response;

}

static struct MHD_Response *handle_post_request_multi_part( struct MHD_Connection *connection,
							    struct request_info_struct *req_info,
							    int *http_status) {
  const char *response_string = "{ \"status\" : 0 }"; 
  struct MHD_Response *response = NULL;

  // the processing of the request really happens in the post-processor callback....
  *http_status = MHD_HTTP_BAD_REQUEST;
  response = MHD_create_response_from_buffer( strlen( response_string), (void*) response_string, MHD_RESPMEM_PERSISTENT);

  return response;
}


// after collecting all the parameters we handle the POST request here
static struct MHD_Response *handle_post_request( struct MHD_Connection *connection,
						 struct request_info_struct *req_info,
						 int *http_status) {

  const char *empty_string = "{}"; // empty JSON object

  *http_status = MHD_HTTP_OK;

  long start_time = get_time_micro();
  long end_time = 0;

  struct MHD_Response *response = NULL;
  char *cmd = NULL;

  if ( req_info->post_req_type == POST_MULTI_PART) {
    cmd = MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA;
    response = handle_post_request_multi_part( connection, req_info, http_status);
  } else if ( req_info->post_req_type == POST_URL_ENCODED) {
    cmd = get_key_value_from_req_info( req_info, "cmd");
    response = handle_post_request_url_encoded( connection, req_info, http_status, cmd);
  } else {
    log_msg( ERR, "unhandled post request type\n");
    *http_status = MHD_HTTP_BAD_REQUEST;
    response = MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
  }

  end_time = get_time_micro();
  log_msg( INFO, "post request: %s %ld [usec]\n", (cmd!=NULL?cmd:""), (long) (end_time-start_time));

  return response;
}

int answer_to_request (void *cls, struct MHD_Connection *connection,
			  const char *url,
                          const char *method, const char *version,
                          const char *upload_data,
                          size_t *upload_data_size, void **con_cls)
{
  const char *empty_string = "";

  struct MHD_Response *response = NULL;
  int ret = MHD_YES;
  int http_status = MHD_HTTP_OK;

  log_msg( DEBUG, "answer_to_request: %s %s %d\n", url, method, *upload_data_size);

  if ( strcasecmp( url, SERVER_URL) != 0) {
    log_msg( WARN, "incorrect url given: %s\n", url);
    response = MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
    http_status = MHD_HTTP_SERVICE_UNAVAILABLE;
    ret = MHD_NO;
    goto out;
  }

  if ( NULL == *con_cls) { // first call for a given request
    
    int post_req_type = 0;  // multi-part or url-encoded....
    if (( post_req_type = check_post_has_content_type( connection, method)) == 0) {
      log_msg( ERR, "missing or bad content-type for POST\n");

      response = MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
      http_status = MHD_HTTP_BAD_REQUEST;
      ret = MHD_NO;
      goto out;
    }

    ret = alloc_request_info( connection, method, con_cls, post_req_type);

    if ( ret != MHD_YES) { // failure
      response = MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
      http_status = MHD_HTTP_INTERNAL_SERVER_ERROR;
      ret = MHD_NO;
      goto out;
    }

    // no response sent yet. continue the processing of request....
    return MHD_YES;
  }

  if ( strcasecmp( method, "GET") == 0) {

    response = handle_get_request( connection, &http_status);

  } else if ( strcasecmp( method, "POST") == 0) {

    struct request_info_struct *req_info = *con_cls;
    assert( con_cls != NULL); // was allocated previously...
    
    log_msg( DEBUG, "POST upload_data_size == %d\n", (int) *upload_data_size);

    if (*upload_data_size != 0) {
      MHD_post_process (req_info->postprocessor, upload_data, *upload_data_size);
      *upload_data_size = 0;

      return MHD_YES;
    } else { // upload_data_size == 0. POST request is now completely processed

      response = handle_post_request( connection, req_info, &http_status);

    }

  } else {
    log_msg( ERR, "answer_to_request: unhandled method %s\n", method);
    response = MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
    http_status = MHD_HTTP_BAD_REQUEST;
    ret = MHD_NO;
    goto out;
  }

 out:
  ret = MHD_queue_response (connection, http_status, response);
  MHD_destroy_response (response);

  return ret;

}

#define CHECK_POINT_DELAY 60 // seconds
#define DEFAULT_CHECKPOINT_KEEP_TIME (5*CHECK_POINT_DELAY) // 24*3600 // seconds

#define DEFAULT_CHECKPOINT_DIR "/tmp"
#define DEFAULT_CHECKPOINT_FN "nlkup_check_point"

// save a binary dump in some directory, filename is time-stamp post-fixed
static int save_check_point_file() {

  char *check_point_dir = CFG_get_str( "check_point_directory", DEFAULT_CHECKPOINT_DIR);
  char *check_point_fn = CFG_get_str( "check_point_filename", DEFAULT_CHECKPOINT_FN);

  time_t curtime;
  struct tm *loc_time;
 
  //Getting current time of system
  curtime = time (NULL);
 
  // Converting current time to local time
  loc_time = localtime (&curtime);
 
  char date_str[512];
  memset( date_str, 0, sizeof( date_str));

  // Displaying date and time in standard format
  strftime (date_str, sizeof( date_str), "%y%m%d_%H%M%S", loc_time);

  char *fn = str_cat( check_point_dir, PATH_SEPARATOR, check_point_fn, "_", date_str, ".bin", NULL);
  if ( IS_NULL( fn)) {
    log_msg( ERR, "save_check_point_file: failure to generate file name\n");
    return -1;
  }

  log_msg( INFO, "checkpointing into %s\n", fn);

  int s = nlkup_dump_file( fn, 1);

  free( fn);

  return s;

}

// callback for directory traversal to delete old check point files
static int ftw_call_back( const char *fpath, const struct stat *sb, int typeflag) {

  char *check_point_fn = CFG_get_str( "check_point_filename", DEFAULT_CHECKPOINT_FN);
  int keep_time = CFG_get_int( "check_point_keep_time", DEFAULT_CHECKPOINT_KEEP_TIME);

  // log_msg( DEBUG, "ftw_call_back: %s\n", fpath);

  if ( typeflag != FTW_F) { // not a regular file...
    return 0;
  }

  int s = 0;

  char *cp = strstr( fpath, check_point_fn);
  if ( cp != NULL) {

    time_t now;
    time( &now);

    long delta_t = difftime( now, sb->st_mtime);

    log_msg( DEBUG, "ftw_call_back: %s %d\n", fpath, delta_t);

    if ( keep_time < delta_t) { // file is too old to be kept
      log_msg( DEBUG, "ftw_call_back: deleting %s\n", fpath);
      if ( remove( fpath) < 0) {
	log_msg( WARN, "ftw_call_back: failure to remove %s\n", fpath);
	s = -1;
      }
    }
  }

  return s;
}

// traverse directory of checkpoint files and remove old ones.
static int remove_old_check_point_files() {

  char *check_point_dir = CFG_get_str( "check_point_directory", DEFAULT_CHECKPOINT_DIR);
  // char *check_point_fn = CFG_get_str( "check_point_filename", DEFAULT_CHECKPOINT_FN);

  if ( ftw( check_point_dir, ftw_call_back, 1) != 0) {
    log_msg( ERR, "remove_old_check_point_files: nftw() failure\n");
    return -1;
  }

  return 0;
}

static void *check_point_thread_body( void *arg) {

  log_msg( INFO, "check point thread started...\n");

  int delay = CFG_get_int( "check_point_delay", CHECK_POINT_DELAY);
  while ( 1) {

    sleep( delay);

    // remove old files
    remove_old_check_point_files();

    // save new checkpoint file
    save_check_point_file();
  }

}

static pthread_t check_point_thread;

static int start_checkpointer() {
  int s = pthread_create( &check_point_thread, NULL, check_point_thread_body, NULL);
  if ( s != 0) {
    char err_buf[512];
    memset( err_buf, 0, sizeof( err_buf));
    strerror_r( s, err_buf, sizeof( err_buf));
    log_msg( ERR, "start_checkpointer: pthread_create failed %s\n", err_buf);
    return -1;
  }
  return 0;
}

int main ( int argc, char **argv)
{
  struct MHD_Daemon *daemon;

  if ( CFG_init( CONFIG_FILE_NAME) < 0) {
    fprintf( stderr, "CFG_init failure\n");
    return -1;
  }

  log_open( CFG_get_str( "log_file_name", "log_file.txt"));
  log_msg( INFO, "started server...\n");

  if ( nlkup_init() < 0) {
    log_msg( CRIT, "nlkup_init() failure\n");
    return -1;
  }

  start_checkpointer();

  int http_port = CFG_get_int( "http_port", HTTP_PORT);

  // we use one thread per connection - on the premiss that not too many clients are connecting
  daemon = MHD_start_daemon ( MHD_USE_THREAD_PER_CONNECTION,
			      // MHD_USE_SELECT_INTERNALLY, 
			      http_port, NULL, NULL,
			      &answer_to_request, NULL, 
			      MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL, // this option has two arguments....
			      MHD_OPTION_END);
  if (NULL == daemon) 
    return 1;

  getchar ();

  MHD_stop_daemon (daemon);

  log_close();

  return 0;

}
