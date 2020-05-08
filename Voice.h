/***** Voice.h *****/
#include <Bela.h>
#include <cmath>
#include <memory>
#include <set>
#include <stdlib.h>
#include <algorithm>
#include <time.h>
#include <libraries/ne10/NE10.h> // NEON FFT library
#include <numeric>
#include <libraries/Midi/Midi.h>
#include "Constants.h"
#include "Grain.h"
#include "Window.h"

class Voice {
	public:
		Voice(float sampleRate, Window& window);
		~Voice();
		
		// Trigger a voice with specified frequency and grain length
		void noteOn(std::array<ne10_fft_cpx_float32_t*, GRAIN_FFT_INTERVAL>& grainSrcBuffer, float frequency, int grainLength);
		void noteOff();
		float play();
		void updateGrainSrcBuffer(std::array<ne10_fft_cpx_float32_t*, GRAIN_FFT_INTERVAL>& grainSrcBuffer);
		void setGrainLength(int grainLengthSamples);
		void setGrainFrequency(int grainFrequencySamples);
		void setScatter(int scatter);
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
		float buffer[MAX_GRAIN_SAMPLES];
		int bufferPosition = NOT_PLAYING_I;
		
		// Reference to window from render.cpp
		Window& window;
		
		// Total number of grains for this voice
		int numberOfGrains = 30;
		
		// Current grains
		std::vector<Grain> grains;

		// Current buffer positions for grains 
		std::vector<int> grainPositions;
		
		// Counter to check whether or not to start a new grain
		// (depending on the grainFrequency)
		int sampleCounter = 0;
		
		// Helper function to find the next non-playing grain sequentially
		// I.e. always returns the next free grain with the lowest index 
		int findNextFreeGrainIdx();
		
		// Get random true/false
		bool getRandomBool();
		// Return a random number from 0 to the given upper limit
		int getRandomInRange(int upperLimit);
		
		// Grain length in samples
		int grainLength = 0;
		// How often to trigger a grain in samples
		int grainFrequency = 0;
		// Scatter: [0...100], will pseudorandomly change grain start positions
		int scatter = 0;
		
		// Timbral stuff
		// Set of fft bins used in frequency extraction
		std::set<int> overtones;
		// Number of overtones to include in frequency extraction process
		int nOvertones = 20;
		
};