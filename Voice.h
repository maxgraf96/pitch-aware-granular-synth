/***** Voice.h *****/
#include <Bela.h>
#include <cmath>
#include <memory>
#include <set>
#include <stdlib.h>
#include <time.h>
#include <libraries/ne10/NE10.h> // NEON FFT library
#include <numeric>
#include <libraries/Midi/Midi.h>

#include "Constants.h"

class Voice {
	public:
		Voice(float sampleRate);
		~Voice();
		
		void noteOn(std::array<ne10_fft_cpx_float32_t*, GRAIN_SRC_BUFFER_LENGTH>& grainSrcBuffer, float frequency);
		void noteOff();
		float play();
	private:
		// Sample rate of the system
		float sampleRate = 0.0f;
		
		// Current frequency if the voice is playing
		float frequency = NOT_PLAYING;
		
		// Buffer which will hold the masked frequency domain representation
		// Filled once for each noteOn event
		//std::array<ne10_fft_cpx_float32_t*, 10> maskedGrainBuffer = {};
		ne10_fft_cpx_float32_t* currentMask;
		
		// Buffer which will hold the masked time domain representation
		ne10_fft_cpx_float32_t* timeDomainGrainBuffer;
		// FFT config
		ne10_fft_cfg_float32_t cfg;
		
		// Buffer into which will hold the IFFT of the 
		// desired frequency bands for this note
		// This buffer will be filled once for every noteOn event
		// and subsequently used to generate grains for this voice :)
		float buffer[GRAIN_SRC_BUFFER_LENGTH * FFT_HOP_SIZE] = {};
		int bufferPosition = NOT_PLAYING_I;
		
		// TODO Turn this into MAXIMUM length of each grain in samples
		const int grainLength = GRAIN_SRC_BUFFER_LENGTH * FFT_HOP_SIZE / 20;
		
		// Hann window for windowing grains
		float hannWindow[GRAIN_SRC_BUFFER_LENGTH * FFT_HOP_SIZE / 20];
		
		// Total number of grains for this voice
		int numberOfGrains = 20;
		// Current buffer positions for grains 
		std::vector<int> grainPositions;
		// Grain audio data
		// Frequency-extracted signal will go into these buffers once for each noteOn event
		std::vector<std::array<float, int(GRAIN_SRC_BUFFER_LENGTH * FFT_HOP_SIZE / 20)>> grainBuffers;
		
		// Number of grains that should play at once
		int numberOfGrainsPlayback = 10;
		
		// Helper function to find the next non-playing grain sequentially
		// I.e. always returns the next free grain with the lowest index 
		int findNextFreeGrainIdx();
		
		// Get random true/false
		bool getRandomBool();
		// Return a random number from 0 to the given upper limit
		int getRandomInRange(int upperLimit);
		
};