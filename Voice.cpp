/***** Voice.cpp *****/
#include "Voice.h"

Voice::Voice(float sampleRate) {
	this->sampleRate = sampleRate;
	cfg = ne10_fft_alloc_c2c_float32_neon (N_FFT);
	
	// Initialise frequency representation grain buffer
	// This will be used to mask the current buffer coming from the main loop
	currentMask = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof(ne10_fft_cpx_float32_t));
	
	// Initialise time representation grain buffer
	timeDomainGrainBuffer = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof(ne10_fft_cpx_float32_t));
	
	// Initialise grain buffers
	// Vectors are used to allow for dynamic adjustment of the number of grains that
	// can be synthesised
	grains = {};
	grainPositions = {};
	for (int i = 0; i < numberOfGrains; i++){
		// Create grain
		Grain* grain = new Grain(MAX_GRAIN_LENGTH / 3);
		grains.push_back(*grain);
		
		// Initialise grain buffer position as well
		grainPositions.push_back(NOT_PLAYING_I);
	}
	
	// Initialise random generator
	srand (time(NULL));
}

void Voice::noteOn(std::array<ne10_fft_cpx_float32_t*, GRAIN_FFT_INTERVAL>& grainSrcBuffer, float frequency){
	this->frequency = frequency;
	this->bufferPosition = 0;
	
	// Clear main buffer
	for (int i = 0; i < MAX_GRAIN_SAMPLES; i++){
		buffer[i] = 0.0f;
	}
	
	// Extract frequencies from grain source buffer
	int binF0 = int(map(frequency, 0.0f, float(sampleRate) / 2.0f, 0.0f, float(N_FFT)));
	// Define overtone bins
	std::set<int> overtones;
	int nOvertones = 20;
	for (int i = 0; i < nOvertones; i++){
		auto current = binF0 * (i + 1);
		if(current >= N_FFT)
			break;
		overtones.insert(current);
	}

	// Create a mask for the frequency domain representation based on the current fundamental frequency
	for (int hop = 0; hop < GRAIN_FFT_INTERVAL; hop++){
		for (int k = 0; k < N_FFT; k++){
			// Check if current bin should be included
			auto search = overtones.find(k);
			if (search == overtones.end()) {
				// Current k is not in bins -> exclude
    			currentMask[k].r = 0.0f;
				currentMask[k].i = 0.0f;
			} else {
				// Include current bin in mask
				currentMask[k].r = grainSrcBuffer[hop][k].r * 10;
				currentMask[k].i = grainSrcBuffer[hop][k].i;
			}
		}
		
		// Run the inverse FFT -> indicated by the "1" for the last function parameter 
		ne10_fft_c2c_1d_float32_neon (timeDomainGrainBuffer, currentMask, cfg, 1);
		
		// Copy current timeDomainGrainBuffer into final time-domain grain buffer
		// using overlap-and-add
		for (int i = 0; i < N_FFT; i++){
			if(bufferPosition + i + 1 >= MAX_GRAIN_SAMPLES){
				break;
			}
			buffer[bufferPosition + i] += timeDomainGrainBuffer[i].r;
		}
		bufferPosition += FFT_HOP_SIZE;
		if (bufferPosition >= MAX_GRAIN_SAMPLES){
			break;
		}
	}
	
	// Reset buffer position for grain slicing
	bufferPosition = 0;
	
	for (int i = 0; i < numberOfGrains; i++){
		// Get grain length
		auto length = grains[i].length;
		// Check if grain would go out of bounds
		if (bufferPosition + length > MAX_GRAIN_SAMPLES){
			rt_printf("Faulty grain start position/length combination! Resetting it... \n");
			bufferPosition = bufferPosition + length - MAX_GRAIN_SAMPLES;
		}
		// Assign grain start idx
		grains[i].bufferStartIdx = bufferPosition;
		
		// Increment buffer position by current grain length
		bufferPosition += length;
	}
	
	// Start playing numberOfGrainsPlayback grains
	for (int i = 0; i < numberOfGrainsPlayback; i++){
		grainPositions[i] = 0;
	}
}

float Voice::play(){
	// Output
	float mix = 0.0f;
	
	waitCounter++;
	
	int currentlyPlaying = 0;
	
	// Iterate over the grains currently playing and add their
	// sample values to the mix
	for (int grainIdx = 0; grainIdx < numberOfGrains; grainIdx++){
		if(grainPositions[grainIdx] > NOT_PLAYING_I){
			currentlyPlaying++;
			// Get current sample for grain
			int grainStartIdx = grains[grainIdx].bufferStartIdx;
			auto currentGrainPos = grainPositions[grainIdx];
			auto currentSample = buffer[grainStartIdx + currentGrainPos] * grains[grainIdx].window[currentGrainPos];
			// Add current sample to mix
			mix += currentSample;
			// Update sample position for grain buffer
			grainPositions[grainIdx]++;
			
			// Reset sample position if over grain buffer length
			if(grainPositions[grainIdx] >= grains[grainIdx].length){
				// Stop playback for this grain
				grainPositions[grainIdx] = NOT_PLAYING_I;
			}
		}
	}

	if(waitCounter > waitLimit && currentlyPlaying < numberOfGrainsPlayback){
		// Find new free grain and playyy
		int nextFreeGrainIdx = findNextFreeGrainIdx();
		grainPositions[nextFreeGrainIdx] = 0;
		waitCounter = waitCounter / (getRandomInRange(10) + 1);
	}
	//mix = buffer[bufferPosition++];
	//if(bufferPosition >= sizeof(buffer) / sizeof(*buffer))
	//	bufferPosition = 0;
		
	return mix;
}

void Voice::noteOff(){
	this->frequency = NOT_PLAYING;

	// Reset grain positions
	for (auto& pos : grainPositions){
		pos = NOT_PLAYING_I;
	}
}

int Voice::findNextFreeGrainIdx(){
	for (int i = 0; i < numberOfGrains; i++){
		if(grainPositions[i] == NOT_PLAYING_I){
			return i;
		}
	}
	// If no grain is found return NOT_PLAYING_I
	return NOT_PLAYING_I;
}

int Voice::getRandomInRange(int upperLimit){
	return rand() % upperLimit + 1;
}

bool Voice::getRandomBool(){
	return rand() % 2 == 0;
}

Voice::~Voice(){
	//grains.clear();
}