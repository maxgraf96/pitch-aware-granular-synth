/***** Voice.cpp *****/
#include "Voice.h"

Voice::Voice(float sampleRate, Window& window) 
	: window (window) {
	
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
		Grain* grain = new Grain(MAX_GRAIN_SAMPLES);
		grains.push_back(*grain);
		
		// Initialise grain buffer position as well
		grainPositions.push_back(NOT_PLAYING_I);
	}
	
	// Initialise random generator
	srand (time(NULL));
}

void Voice::noteOn(std::array<ne10_fft_cpx_float32_t*, GRAIN_FFT_INTERVAL>& grainSrcBuffer, float frequency, int grainLength){
	this->frequency = frequency;
	this->bufferPosition = 0;
	
	// Clear main buffer
	for (int i = 0; i < MAX_GRAIN_SAMPLES; i++){
		buffer[i] = 0.0f;
	}
	
	// Extract frequencies from grain source buffer
	int binF0 = int(map(frequency, 0.0f, float(sampleRate) / 2.0f, 0.0f, float(N_FFT / 2 + 1)));
	
	// Clear overtone bins
	overtones.clear();
	for (int i = 0; i < nOvertones; i++){
		auto current = binF0 * (i + 1);
		if(current >= N_FFT)
			break;
		overtones.insert(current);
	}

	// Update grain source buffer
	updateGrainSrcBuffer(grainSrcBuffer);
	
	// Default starting position is buffer start
	int grainStartPosition = 0;
	
	for (int i = 0; i < numberOfGrains; i++){
		if(scatter == 0){
			grainStartPosition = 0;
		}
		// If scatter > 0 pseudorandomly spread out the grain start positions
		else {
			int random = getRandomInRange(MAX_GRAIN_SAMPLES);
			grainStartPosition = int(0.01 * scatter * random);
		}
		// Check if length would go past buffer limit and wrap around if necessary (start from the beginning)
		if(grainStartPosition + grainLength >= MAX_GRAIN_SAMPLES){
			grainStartPosition = grainStartPosition + grainLength - MAX_GRAIN_SAMPLES;
		}
		// Update grain length (can be set dynamically in the user interface)
		grains[i].updateLength(grainLength);
		
		// Assign grain start idx
		grains[i].bufferStartIdx = grainStartPosition;
	}
	
	// Start playing first grain
	grainPositions[0] = 0;
}

float Voice::play(){
	// Output
	float mix = 0.0f;
	// Number of currently playing grains
	int currentlyPlaying = 0;
	
	for (int grainIdx = 0; grainIdx < numberOfGrains; grainIdx++){
		if(grainPositions[grainIdx] > NOT_PLAYING_I){
			currentlyPlaying++;
		}
	}
	
	// Iterate over the grains currently playing and add their
	// sample values to the mix
	for (int grainIdx = 0; grainIdx < numberOfGrains; grainIdx++){
		if(grainPositions[grainIdx] > NOT_PLAYING_I){
			// Get current sample for grain
			int grainStartIdx = grains[grainIdx].bufferStartIdx;
			auto currentGrainPos = grainPositions[grainIdx];
			auto currentSample = buffer[grainStartIdx + currentGrainPos] * window.getAt(currentGrainPos);
			
			// Add current sample to mix
			mix += currentSample / float(currentlyPlaying);
			
			// Update sample position for grain buffer
			grainPositions[grainIdx]++;
	
			// Reset sample position if over grain buffer length and stop playing
			if(grainPositions[grainIdx] >= grains[grainIdx].length){
				grainPositions[grainIdx] = NOT_PLAYING_I;
			}
		}
	}
	
	// Check if a new grain should be triggered, i.e. if grainFrequency samples elapsed
	if(sampleCounter >= grainFrequency){
		// Trigger new grain
		int nextFree = findNextFreeGrainIdx();
		if(scatter > 0){
			// Add randomness to the next grain triggered
			nextFree += getRandomInRange(numberOfGrains);
			if(nextFree > numberOfGrains)
				nextFree -= numberOfGrains + 1;
		}
		grainPositions[nextFree] = 0;
		
		// Reset sample counter
		sampleCounter = 0;
	}
	
	// Update sample counter
	sampleCounter++;
	
	// Attenuate mix by number of currently playing grains
	
	return mix;
}

void Voice::updateGrainSrcBuffer(std::array<ne10_fft_cpx_float32_t*, GRAIN_FFT_INTERVAL>& grainSrcBuffer){
	bufferPosition = 0;
	// Clear buffer
	for (int i = 0; i < MAX_GRAIN_SAMPLES; i++){
		buffer[i] = 0.0f;
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
				currentMask[k].r = grainSrcBuffer[hop][k].r;
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
	
	// Normalise buffer level
	/*float max = *std::max_element(buffer, buffer + MAX_GRAIN_SAMPLES);
	float min = *std::min_element(buffer, buffer + MAX_GRAIN_SAMPLES);
	for (int i = 0; i < MAX_GRAIN_SAMPLES; i++){
		buffer[i] = map(buffer[i], min, max, -0.5f, 0.5f);
	}*/
}

void Voice::noteOff(){
	this->frequency = NOT_PLAYING;

	// Reset grain positions
	for (auto& pos : grainPositions){
		pos = NOT_PLAYING_I;
	}
}

void Voice::setGrainLength(int grainLengthSamples){
	this->grainLength = grainLengthSamples;
	
	for(auto& grain : grains){
		auto grainStartPosition = grain.bufferStartIdx;
		// Check if length would go past buffer limit and adjust accordingly
		if(grainStartPosition + grainLength >= MAX_GRAIN_SAMPLES){
			grainStartPosition = grainStartPosition + grainLength - MAX_GRAIN_SAMPLES;
		}
		// Update grain length (can be set dynamically in the user interface)
		grain.bufferStartIdx = grainStartPosition;
		grain.updateLength(grainLength);
	}
}

void Voice::setGrainFrequency(int grainFrequencySamples){
	this->grainFrequency = grainFrequencySamples;
}

void Voice::setScatter(int scatter){
	this->scatter = scatter;
	
	for(auto& grain : grains){
		auto grainStartPosition = 0;
		// If scatter > 0 pseudorandomly spread out the grain start positions
		if(scatter > 0) {
			int random = getRandomInRange(MAX_GRAIN_SAMPLES);
			grainStartPosition = int(0.01 * scatter * random);
		}
		// Check if length would go past buffer limit and wrap around if necessary (start from the beginning)
		if(grainStartPosition + grainLength >= MAX_GRAIN_SAMPLES){
			grainStartPosition = grainStartPosition + grainLength - MAX_GRAIN_SAMPLES;
		}
		grain.bufferStartIdx = grainStartPosition;
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