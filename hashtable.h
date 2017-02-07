#ifndef _HASH_TABLE_H_
#define _HASH_TABLE_H_

typedef void *HT_Table;

typedef int (*HT_CompareTo)(const void *key1, const void *key2);
typedef long (*HT_Hash)(const void *key);

HT_Table HT_new( const unsigned int size,
		 HT_CompareTo compare_to_callback,
		 HT_Hash hash_callback);
void HT_free( HT_Table table);

int HT_insert( HT_Table table, const void *key, const void *data, const int overwrite);
int HT_delete( HT_Table table, const void *key);
void *HT_lookup( HT_Table table, const void *key);

unsigned long HT_DJB_hash(const char* cp);

#endif
