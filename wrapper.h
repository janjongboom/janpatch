//
// Created by alomaa on 2/1/2018.
//

#ifndef JANPATCH_MASTER_WRAPPER_H
#define JANPATCH_MASTER_WRAPPER_H


#include <limits.h>
#include <stdio.h>
#include <stdint.h>

size_t cread(void *ptr, size_t size, size_t count, FILE *stream, long *cur);
size_t cwrite(const void *ptr, size_t size, size_t count, FILE *stream, long *cur);
int    cseek(long* pos, long int offset, int origin);
    
#endif //JANPATCH_MASTER_WRAPPER_H
