#include "data_recorder.h"
int data_recorder_init(void){return 0;}
void data_recorder_close(void){}
void data_recorder_record(float t,float h,bool v){(void)t;(void)h;(void)v;}
void data_recorder_flush(void){}
int data_recorder_get_buffered_count(void){return 0;}
void data_recorder_tick(void){}
void data_recorder_cleanup(int d){(void)d;}
