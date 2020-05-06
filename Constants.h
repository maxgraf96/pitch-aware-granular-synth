/***** 
 * Constants.h
 * Definition of system-wide constants
*****/

// Buffer length for circular buffer
const int CB_LENGTH = 16384;

// FFT params
const int N_FFT = 2048;
const int FFT_HOP_SIZE = 512;

// System-wide indicator for "not playing"
const float NOT_PLAYING = -1.0f;
const int NOT_PLAYING_I = -1;

// Number of voices
const int NUM_VOICES = 16;

// Expressed in factors of FFT_HOP_SIZE
// So 100 will lead to a src grain buffer of 100 * 512 = 51200 samples (~1.25s)
const int GRAIN_SRC_BUFFER_LENGTH = 100;