#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>

// set include-path!
#include <microhttpd.h>

#include "nlkup.h"

#define PORT 8888

// GET cmd=alias number=1234567890
// GET cmd=block number=1234567890

// POST cmd=delete number=123456890
// POST cmd=insert_NUMBER number=1234567890 alias=1234567890
// POST cmd=dump_file file_name=....
// POST cmd=restore_file file_name=....


#define JSON_BUFR_TABLE_HEADER_SIZE 2048

static unsigned char *table_to_json( LkupTblPtr table, const unsigned char *nbr) {
  
  unsigned char prefix[10]; 
  memset( prefix, 0, sizeof( prefix));
  strncpy( prefix, nbr, PREFIX_LENGTH);

  int buf_sz = JSON_BUFR_TABLE_HEADER_SIZE;
  if ( table != NULL && table->table_len > 0) {
    buf_sz += table->table_len * ( PREFIX_LENGTH + MAX_NBR_LENGTH + 32);
  }

  unsigned char *buf = malloc( buf_sz);
  memset( buf, 0, buf_sz);

  if ( table == NULL) {
    snprintf( buf, buf_sz, "{ \"idx\" : \"%s\", \"sz\" : %d, \"len\" : %d , \"data\" : [] }\n", prefix, 0, 0);
    return buf;
  }

  unsigned char *cp = buf;
  unsigned long cnt = 0;
  int i = 0;

  cnt += snprintf( cp+cnt, buf_sz-cnt, "{ \"idx\" : \"%s\", \"sz\" : %ld, \"len\" : %ld, \"data\" : [ \n", prefix, table->table_sz, table->table_len);

  for ( i = 0; i < table->table_len; i++) {
    LkupTblEntry *e = &(table->table[i]);

    unsigned char postfix[16];
    unsigned char alias[32];

    decompress_to_buf( e->postfix, postfix, sizeof( postfix));
    decompress_to_buf( e->alias, alias, sizeof( alias));    

    cnt += snprintf( cp+cnt, buf_sz-cnt, "[ \"%s\", \"%s\" ]", nbr, alias);

  }

  cnt += snprintf( cp+cnt, buf_sz-cnt, "]}\n");

  return buf;
}

static struct MHD_Response *handle_get_request( struct MHD_Connection *connection, int *http_status) {

  const char *empty_string = "";

  const char *cmd = MHD_lookup_connection_value( connection, MHD_GET_ARGUMENT_KIND, "cmd");
  const char *nbr = MHD_lookup_connection_value( connection, MHD_GET_ARGUMENT_KIND, "number");

  if ( cmd == NULL || nbr == NULL) {
    fprintf( stderr, "handle_get_request: missing query parameters\n");
    *http_status = MHD_HTTP_BAD_REQUEST;
    return MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
  }

  if ( strlen( nbr) < PREFIX_LENGTH) {
    fprintf( stderr, "handle_get_request: number too short: %s\n", nbr);
    *http_status = MHD_HTTP_BAD_REQUEST;
    return MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
  }

  if ( strcasecmp( cmd, "alias") == 0) {

    char *alias = "1234";

    // generate some JSON
    unsigned char buffer[1024];
    memset( buffer, 0, sizeof( buffer));
    snprintf( buffer, sizeof( buffer), "{ \"alias\" : \"%s\" }\n", (alias!=NULL?alias:""));

    // set HTTP status and create response
    *http_status = MHD_HTTP_OK;
    return MHD_create_response_from_buffer( strlen( buffer), (void *) buffer, MHD_RESPMEM_MUST_COPY);

  } else if ( strcasecmp( cmd, "block") == 0) {

    LkupTblPtr table = NULL;
    unsigned char *buffer = table_to_json( table, nbr);
    
    *http_status = MHD_HTTP_OK;
    return MHD_create_response_from_buffer( strlen( buffer), (void *) buffer, MHD_RESPMEM_MUST_FREE);
    
  } else {
    fprintf( stderr, "handle_get_request: unsupported command %s\n", cmd);
    *http_status = MHD_HTTP_BAD_REQUEST;
    return MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
  }
  
}

#define MAXANSWERSIZE   512
#define MAXNAMESIZE   512
#define POSTBUFFERSIZE 512

#define GET             0
#define POST            1

struct connection_info_struct
{
  int connectiontype;
  char *answerstring;
  struct MHD_PostProcessor *postprocessor;
};

const char *greetingpage =
  "<html><body><h1>Welcome, %s!</center></h1></body></html>";

static int
send_page (struct MHD_Connection *connection, const char *page)
{
  int ret;
  struct MHD_Response *response;


  response =
    MHD_create_response_from_buffer (strlen (page), (void *) page,
				     MHD_RESPMEM_PERSISTENT);
  if (!response)
    return MHD_NO;

  ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
  MHD_destroy_response (response);

  return ret;
}

static int
iterate_post (void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
              const char *filename, const char *content_type,
              const char *transfer_encoding, const char *data, uint64_t off,
              size_t size)
{
  struct connection_info_struct *con_info = coninfo_cls;

  if (0 == strcmp (key, "name"))
    {
      if ((size > 0) && (size <= MAXNAMESIZE))
        {
          char *answerstring;
          answerstring = malloc (MAXANSWERSIZE);
          if (!answerstring)
            return MHD_NO;

          snprintf (answerstring, MAXANSWERSIZE, greetingpage, data);
          con_info->answerstring = answerstring;
        }
      else
        con_info->answerstring = NULL;

      return MHD_NO;
    }

  return MHD_YES;
}

static void
request_completed (void *cls, struct MHD_Connection *connection,
                   void **con_cls, enum MHD_RequestTerminationCode toe)
{
  struct connection_info_struct *con_info = *con_cls;

  if (NULL == con_info)
    return;

  if (con_info->connectiontype == POST)
    {
      MHD_destroy_post_processor (con_info->postprocessor);
      if (con_info->answerstring)
        free (con_info->answerstring);
    }

  free (con_info);
  *con_cls = NULL;
}


int answer_to_connection (void *cls, struct MHD_Connection *connection,
			  const char *url,
                          const char *method, const char *version,
                          const char *upload_data,
                          size_t *upload_data_size, void **con_cls)
{
  const char *empty_string = "";

  struct MHD_Response *response = NULL;
  int ret;
  int http_status = MHD_HTTP_OK;

  fprintf( stderr, "answer_to_connection: %s %s\n", url, method);

  if (NULL == *con_cls)
    {
      struct connection_info_struct *con_info;

      con_info = malloc (sizeof (struct connection_info_struct));
      if (NULL == con_info)
        return MHD_NO;
      con_info->answerstring = NULL;

      if (0 == strcmp (method, "POST"))
        {
          con_info->postprocessor =
            MHD_create_post_processor (connection, POSTBUFFERSIZE,
                                       iterate_post, (void *) con_info);

          if (NULL == con_info->postprocessor)
            {
              free (con_info);
              return MHD_NO;
            }

          con_info->connectiontype = POST;
        }
      else
        con_info->connectiontype = GET;

      *con_cls = (void *) con_info;

      return MHD_YES;
    }

  if ( strcasecmp( method, "GET") == 0) {

    response = handle_get_request( connection, &http_status);

  } else if ( strcasecmp( method, "POST") == 0) {

    {
      struct connection_info_struct *con_info = *con_cls;

      if (*upload_data_size != 0)
        {
          MHD_post_process (con_info->postprocessor, upload_data,
                            *upload_data_size);
          *upload_data_size = 0;

          return MHD_YES;
        }
      else if (NULL != con_info->answerstring)
        return send_page (connection, con_info->answerstring);
    }

  } else {
    fprintf( stderr, "answer_to_connection: unhandled method\n");
    response = MHD_create_response_from_buffer( strlen( empty_string), (void*) empty_string, MHD_RESPMEM_PERSISTENT);
    http_status = MHD_HTTP_BAD_REQUEST;
  }

  ret = MHD_queue_response (connection, http_status, response);
  MHD_destroy_response (response);

  return ret;

}

int main ()
{
  struct MHD_Daemon *daemon;

  daemon = MHD_start_daemon (MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL,
                             &answer_to_connection, NULL, 
			     MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
			     MHD_OPTION_END);
  if (NULL == daemon) 
    return 1;

  getchar ();

  MHD_stop_daemon (daemon);

  return 0;

}
