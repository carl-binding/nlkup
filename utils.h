#ifndef _UTILS_H_
#define _UTILS_H_

int decompress_to_buf( const unsigned char data[], unsigned char dest[], const int dest_sz);
unsigned char *decompress( const unsigned char data[]);
int compress_to_buf( const unsigned char nbr[], const int from, const int nbr_len, unsigned char *dest, const int dest_sz);
unsigned char *compress( const unsigned char nbr[], const int from, const int nbr_len);

void *mem_alloc( size_t sz);
void mem_free( void *ptr);

// returns time in microseconds
long get_time_micro();

int all_digits( const unsigned char *s);

// returns allocated string s trimmed of leading and trailing white spaces.
char *str_trim( const char *s);

// to free the result of a str_split
void free_tokens( char **tokens);

// splitting a string using a single character
// the result must be freed....
char** str_split(const char* a_str, const char a_delim, int *nbr_tokens);

// concatenates list of strings. list is terminated with NULL
char *str_cat( const char *s, ...);

#define IS_NULL(s) ((s) == NULL || strlen(s) == 0)


#endif
