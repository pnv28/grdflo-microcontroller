#ifndef STAT_H
#define STAT_H

#include "config.h"

void statHealth();
void statAck(const char *originTopic, bool ok);
void statAck(char *segment[], size_t seg_len, bool ok);
void statState();

#endif
