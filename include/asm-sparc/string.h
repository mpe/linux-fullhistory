/* string.h: External definitions for optimized assembly string
             routines for the Linux Kernel.

   Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
*/

extern int strlen(char* str);
extern int strcmp(char* str1, char* str2);
extern int strncmp(char* str1, char* str2, int strlen);
extern int strcpy(char* dest, char* source);
extern int strncpy(char* dest, char* source, int cpylen);
