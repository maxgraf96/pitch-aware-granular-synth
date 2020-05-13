/***** 
 * Constants.h
 * Definition of system-wide constants
*****/
#ifndef MY_CONSTANTS_H
#define MY_CONSTANTS_H

// Buffer length for the main output buffer
const int MAIN_BUFFER_LENGTH = 16384;

// FFT params
const int N_FFT = 4096;
const int FFT_HOP_SIZE = 1024;

// System-wide indicator for "not playing"
const float NOT_PLAYING = -1.0f;
// int version
const int NOT_PLAYING_I = -1;

// Number of voices
const int NUM_VOICES = 10;

// Expressed in factors of FFT_HOP_SIZE
// 100 will lead to a src grain buffer of 100 * 512 = 51200 samples (~1.25s at 44.1kHz)
const int GRAIN_FFT_INTERVAL = 100;

// The final length of the grain source buffer
// i.e. the buffer that is passed to voices on noteOn events
const int MAX_GRAIN_SAMPLES = FFT_HOP_SIZE * GRAIN_FFT_INTERVAL;

// Maximum allowed grain length (22050 = 500ms at 44.1kHz)
const int MAX_GRAIN_LENGTH = 22050;

#endif