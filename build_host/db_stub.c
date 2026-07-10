#include "database.h"
#include <string.h>
int database_init(void){return 0;}
void database_close(void){}
int database_insert(float t,float h,bool v){(void)t;(void)h;(void)v;return 0;}
int database_cleanup(int d){(void)d;return 0;}
int database_query_history(int h,sensor_record_t *r,int m){if(r)memset(r,0,m*sizeof(*r));return 0;}
int database_get_stats(int h,sensor_stats_t *s){(void)h;if(s)memset(s,0,sizeof(*s));return 0;}
int database_insert_device_data(const char *a,const char *b,const char *c,double d,const char *e,bool f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return 0;}
