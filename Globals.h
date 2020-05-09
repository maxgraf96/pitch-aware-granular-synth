/***** 
 * Globals.h
 * Definition of global variables so as to avoid cluttering the render.cpp code
 * 
*****/
#ifndef GLOBALS_H
#define GLOBALS_H
#include "SampleData.h"

// Whether the system is currently running
static bool IS_PLAYING = false;

// Lengths of the files loaded in main.cpp in samples
extern int FILE_LENGTH1;
extern int FILE_LENGTH2;
extern int FILE_LENGTH3;

// Songs
extern SampleData songs[3];

#endif