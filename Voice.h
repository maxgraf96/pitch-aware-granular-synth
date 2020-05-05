/***** Voice.h *****/
#include <Bela.h>
#include <cmath>
#include <memory>
#include <set>
#include <libraries/ne10/NE10.h> // NEON FFT library
#include <numeric>
#include <libraries/Midi/Midi.h>

#include "Constants.h"

class Voice {
	public:
		Voice(float sampleRate);
		~Voice();
		
		void noteOn(std::array<ne10_fft_cpx_float32_t*, 10>& grainSrcBuffer, float frequency);
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
		float buffer[10 * FFT_HOP_SIZE] = {};
		int bufferPosition = int(NOT_PLAYING);
		
		// Current buffer positions for grains 
		int grainPositions[20] = {0};
		// Length of each grain in samples
		const int grainLength = int(10 * FFT_HOP_SIZE / 20);
		// Grain audio data
		// Frequency-extracted signal will go into this buffer once for each noteOn event
		float grainBuffers[20][int(10 * FFT_HOP_SIZE / 20)] = {{0}};
};