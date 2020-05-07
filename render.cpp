#include <Bela.h>
#include <cmath>
#include <memory>
#include <set>
#include <libraries/ne10/NE10.h> // NEON FFT library
#include <libraries/Midi/Midi.h>
#include <numeric>
#include <atomic>
#include <libraries/Gui/Gui.h>
#include <libraries/GuiController/GuiController.h>

// Definition for global variables so as to avoid cluttering the render.cpp code
#include "Globals.h"
// #include "CircularBuffer.h"
#include "SampleData.h"
#include "Voice.h"

// Audio channels
int gAudioChannelNum;

// Buffers --------------------------------------------------
float gInputBuffer[CB_LENGTH];
int gInputBufferPointer = 0;
float gOutputBuffer[CB_LENGTH];
int gOutputBufferWritePointer = 0;
int gOutputBufferReadPointer = 0;
int gSampleCount = 0;

float *gWindowBuffer;

// FFT params -----------------------------------------------
float gFFTScaleFactor = 0;

// Neon params
ne10_fft_cpx_float32_t* timeDomainIn;
ne10_fft_cpx_float32_t* timeDomainOut;
ne10_fft_cpx_float32_t* frequencyDomain;
ne10_fft_cfg_float32_t cfg;

// Grain src time domain input
ne10_fft_cpx_float32_t* grainSrcTimeDomainIn;
// Final frequency domain representation of current range with max size GRAIN_FFT_INTERVAL
std::array<ne10_fft_cpx_float32_t*, GRAIN_FFT_INTERVAL> grainSrcFrequencyDomain = {};

// Large buffer that's updated every ~1s given that no keys are pressed
// Once a key is pressed, this buffer will be used as source material
// to extract grains from
std::array<ne10_fft_cpx_float32_t*, GRAIN_FFT_INTERVAL> frequencyDomainGrainBuffer = {};
std::array<ne10_fft_cpx_float32_t*, GRAIN_FFT_INTERVAL> frequencyDomainGrainBufferSwap = {};

// Pointer to current grain buffer
std::array<ne10_fft_cpx_float32_t*, GRAIN_FFT_INTERVAL>* currentGrainBuffer = nullptr;

// Counter to 10 to check when grain buffer is ready
int grainBufferIdx = 0;

// Flag to indicate which buffer is currently safe to read from
// If true, it is safe to read from frequencyDomainGrainBuffer, otherwise it is safe to read from frequencyDomainGrainBufferSwap
bool grainBufferSafeToRead = false;

// Sample info
// Data structure for complex data
SampleData gSampleData;
// Position of last read sample from audio file
int gReadPtr = 0;

// Auxiliary task for calculating FFT
AuxiliaryTask gFFTTask;
AuxiliaryTask updateGrainSrcBufferTask;
int gFFTInputBufferPointer = 0;
int gFFTOutputBufferPointer = 0;

// Necessary function definition for running an auxiliary task later
void process_fft_background(void*);
void process_grain_src_buffer_update_background(void*);

float *gInputAudio = NULL;

// MIDI object for receiving MIDI data
Midi midi;

// Voices: An array indicating which voice (default number of voices: 16) currently plays which frequency
// All voices are initially "not playing" =^= -1
float voices[NUM_VOICES] = { NOT_PLAYING };
std::vector<Voice> voiceObjects = {};

// Expose sample rate
int gSampleRate = 0;

// Browser-based GUI to adjust parameters
Gui gui;
GuiController controller;
// Slider indices
unsigned int positionSlider;
unsigned int rangeSlider;

int currentSourcePosition = 0;
int currentRange = 0;
int prevSourcePosition = 0;
int prevSourceRange = 0;

void midiCallback(MidiChannelMessage message, void* arg){
	// Note on event: Find the next free voice and assign frequency coming from MIDI note
	if(message.getType() == kmmNoteOn){
		// Get grain buffer
		currentGrainBuffer = grainBufferSafeToRead ? &frequencyDomainGrainBufferSwap : &frequencyDomainGrainBuffer;

		if(message.getDataByte(1) > 0){
			int note = message.getDataByte(0);
			float frequency = powf(2, (note-69)/12.f)*440;
			
			// Find free voice and assign
			for (int i = 0; i < NUM_VOICES; i++){
				if (voices[i] == NOT_PLAYING) {
					// Assign frequency of incoming MIDI note
					voices[i] = frequency;
					// Trigger note on event
					voiceObjects[i].noteOn(grainSrcFrequencyDomain, frequency);
					
					// Print note info
					rt_printf("\nnote: %d, frequency: %f \n", note, frequency);
					
					// Break loop
					break;
				}
			}
		}
	}
	// Note off event: Find voice for incoming frequency and set to NOT_PLAYING
	if(message.getType() == kmmNoteOff){
		// Get frequency of incoming note off 
		int note = message.getDataByte(0);
		float frequency = powf(2, (note-69)/12.f)*440;
		
		// Find the voice that plays this note, remove assignment
		for (int i = 0; i < NUM_VOICES; i++){
			if (voices[i] == frequency) {
				// Reset voice
				voices[i] = NOT_PLAYING;
				
				// Trigger note off event
				voiceObjects[i].noteOff();
				
				// Break loop
				break;
			}
		}
	}
}

bool setup(BelaContext *context, void *userData)
{
	// If the amout of audio input and output channels is not the same
	// we will use the minimum between input and output
	gAudioChannelNum = std::min(context->audioInChannels, context->audioOutChannels);
	
	// Check that we have the same number of inputs and outputs.
	if(context->audioInChannels != context->audioOutChannels){
		printf("Different number of audio outputs and inputs available. Using %d channels.\n", gAudioChannelNum);
	}
	
	// Retrieve a parameter passed in from the initAudio() call
	gSampleData = *(SampleData *)userData;
	rt_printf("Sample data length: %4.2f seconds \n", (gSampleData.sampleLen / context->audioSampleRate));

	gFFTScaleFactor = 1.0f / (float)N_FFT;
	gOutputBufferWritePointer += FFT_HOP_SIZE;

	// Allocate memory for time domain inputs and outputs
	// frequency domain representation and the configuration for NEON
	timeDomainIn = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));
	timeDomainOut = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));
	frequencyDomain = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));
	cfg = ne10_fft_alloc_c2c_float32_neon (N_FFT);
	
	// NEWWWWWWWWWWWW ---------------------------------------------------------------------------------------------------------
	grainSrcTimeDomainIn = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));
	// Initialise grain buffers
	for (int i = 0; i < GRAIN_FFT_INTERVAL; i++){
		grainSrcFrequencyDomain[i] = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));

		for (int k = 0; k < N_FFT; k++){
			grainSrcFrequencyDomain[i][k].r = 0.0f;
			grainSrcFrequencyDomain[i][k].i = 0.0f;
		}
	}
	// END NEW ----------------------------------------------------------------------------------------------------------------
	
	// Initialise grain buffers
	for (int i = 0; i < GRAIN_FFT_INTERVAL; i++){
		frequencyDomainGrainBuffer[i] = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));
		frequencyDomainGrainBufferSwap[i] = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));
		
		for (int k = 0; k < N_FFT; k++){
			frequencyDomainGrainBuffer[i][k].r = 0.0f;
			frequencyDomainGrainBuffer[i][k].i = 0.0f;
			
			frequencyDomainGrainBufferSwap[i][k].r = 0.0f;
			frequencyDomainGrainBufferSwap[i][k].i = 0.0f;
		}
	}
	
	
	memset(timeDomainOut, 0, N_FFT * sizeof (ne10_fft_cpx_float32_t));
	memset(gOutputBuffer, 0, CB_LENGTH * sizeof(float));

	// Allocate buffer to mirror and modify the input
	gInputAudio = (float *)malloc(context->audioFrames * gAudioChannelNum * sizeof(float));
	if(gInputAudio == 0)
		return false;

	// Allocate the window buffer based on the FFT size
	gWindowBuffer = (float *)malloc(N_FFT * sizeof(float));
	if(gWindowBuffer == 0)
		return false;

	// Calculate a Hann window
	for(int n = 0; n < N_FFT; n++) {
		gWindowBuffer[n] = 0.5f * (1.0f - cosf(2.0f * M_PI * n / (float)(N_FFT - 1)));
	}

	// Initialise auxiliary tasks
	if((gFFTTask = Bela_createAuxiliaryTask(&process_fft_background, 90, "fft-calculation")) == 0)
		return false;
		
	if((updateGrainSrcBufferTask = Bela_createAuxiliaryTask(&process_grain_src_buffer_update_background, 90, "grain-src-update")) == 0)
		return false;
		
	// Setup MIDI
	midi.readFrom(0);
	midi.setParserCallback(midiCallback);
	
	// Expose audio sample rate
	gSampleRate = context->audioSampleRate;
	
	// Initialise voices array 
	for (auto& voice : voices) {
		voice = NOT_PLAYING;
	}
	
	// Initialise voice voice objects
	for (int i = 0; i < NUM_VOICES; i++){
		Voice* voice = new Voice(gSampleRate);
		voiceObjects.push_back(*voice);
	}
	
	rt_printf("Project name is: %s\n", context->projectName); 
	// Set up the GUI
	// Project name hardcoded here until the empty string for projectName 
	// problem is fixed
	gui.setup("pitch-aware-granular-synth");
	controller.setup(&gui, "Controls");	
	
	// Arguments: name, default value, minimum, maximum, increment
	positionSlider = controller.addSlider("Source position", 0, 0, FILE_LENGTH, 1000);
	rangeSlider = controller.addSlider("Source range", int(gSampleRate), int(gSampleRate), MAX_GRAIN_SAMPLES, 1000);
	
	return true;
}

// This function handles the FFT processing in this example once the buffer has
// been assembled.
void process_fft(float *inBuffer, int inWritePointer, float *outBuffer, int outWritePointer)
{
	// Copy buffer into FFT input
	int pointer = (inWritePointer - N_FFT + CB_LENGTH) % CB_LENGTH;
	for(int n = 0; n < N_FFT; n++) {
		timeDomainIn[n].r = (ne10_float32_t) inBuffer[pointer] * gWindowBuffer[n];
		timeDomainIn[n].i = 0;

		pointer++;
		if(pointer >= CB_LENGTH)
			pointer = 0;
	}

	// Run the FFT -> indicated by the "0" for the last function parameter 
	ne10_fft_c2c_1d_float32_neon (frequencyDomain, timeDomainIn, cfg, 0);
	
	// Update pointer to currently readable grain buffer
	currentGrainBuffer = grainBufferSafeToRead ? &frequencyDomainGrainBufferSwap : &frequencyDomainGrainBuffer;
	auto grainBuffer = *currentGrainBuffer;

	// Add the current frequency domain representation to the grain buffer
	for (int k = 0; k < N_FFT; k++){
		grainBuffer[grainBufferIdx][k].r = frequencyDomain[k].r;
		grainBuffer[grainBufferIdx][k].i = frequencyDomain[k].i;
	}
	
	// Increment grain buffer idx
	grainBufferIdx++;
	
	// Swap buffers if GRAIN_FFT_INTERVAL x hop size was filled
	if(grainBufferIdx >= GRAIN_FFT_INTERVAL){
		// Reset grain buffer index
		grainBufferIdx = 0;
		frequencyDomainGrainBuffer.swap(frequencyDomainGrainBufferSwap);
		grainBufferSafeToRead = !grainBufferSafeToRead;
		rt_printf("Swapping grain source buffers \n");
	}

	// Run the inverse FFT -> indicated by the "1" for the last function parameter 
	ne10_fft_c2c_1d_float32_neon (timeDomainOut, frequencyDomain, cfg, 1);
	
	// Overlap-and-add timeDomainOut into the output buffer
	pointer = outWritePointer;
	for(int n = 0; n < N_FFT; n++) {
		outBuffer[pointer] += (timeDomainOut[n].r);// * gFFTScaleFactor;
		if(std::isnan(outBuffer[pointer]))
			rt_printf("outBuffer OLA \n");
		pointer++;
		if(pointer >= CB_LENGTH)
			pointer = 0;
	}
}

void process_grain_src_buffer_update(int rangeStart, int range){
	// Calculate number of hops we need for the current range
	int nHops = range / FFT_HOP_SIZE - 1;
	rt_printf("Total number of hops for grain src buffer update: %i \n", nHops);

	// Copy sample buffer into FFT input until range is reached
	for (int hop = 0; hop < nHops; hop++){
		int currentStart = hop * FFT_HOP_SIZE;
		for(int n = 0; n < N_FFT; n++) {
			grainSrcTimeDomainIn[n].r = (ne10_float32_t) gSampleData.samples[rangeStart + currentStart + n] * gWindowBuffer[n];
			grainSrcTimeDomainIn[n].i = 0;
		}
		
		// Perform FFT -> indicated by the "0" for the last function parameter 
		ne10_fft_c2c_1d_float32_neon (grainSrcFrequencyDomain[hop], grainSrcTimeDomainIn, cfg, 0);
	}
	
	rt_printf("Done updating grain src buffer \n");
}

// Function to process the FFT in a thread at lower priority
void process_fft_background(void*) {
	process_fft(gInputBuffer, gFFTInputBufferPointer, gOutputBuffer, gFFTOutputBufferPointer);
}

void process_grain_src_buffer_update_background(void *){
	process_grain_src_buffer_update(currentSourcePosition, currentRange);
}

void render(BelaContext *context, void *userData)
{
	// Get slider values
	// Where we are in the sample
	currentSourcePosition = int(controller.getSliderValue(positionSlider));
	// How far we can look for sample positions
	currentRange = int(controller.getSliderValue(rangeSlider));	
	
	// Get number of audio frames
	int numAudioFrames = context->audioFrames;
	
	// If source position or source range changed update the grain source buffer
	if (currentSourcePosition != prevSourcePosition || currentRange != prevSourceRange){
		Bela_scheduleAuxiliaryTask(updateGrainSrcBufferTask);
	}

	// Prep the "input" to be the sound file played in a loop
	for(int n = 0; n < numAudioFrames; n++) {
		if(gReadPtr < gSampleData.sampleLen)
			gInputAudio[2 * n] = gInputAudio[2 * n + 1] = gSampleData.samples[gReadPtr];
		else
			gInputAudio[2 * n] = gInputAudio[2 * n + 1] = 0;
		if(++gReadPtr >= gSampleData.sampleLen)
			gReadPtr = 0;
	}

	for(int n = 0; n < numAudioFrames; n++) {
		// Get input buffer, currently from sample
		gInputBuffer[gInputBufferPointer] = ((gInputAudio[n*gAudioChannelNum] + gInputAudio[n*gAudioChannelNum+1]) * 0.5);

		// Write output buffer to sound output
		for(int channel = 0; channel < gAudioChannelNum; channel++){
			audioWrite(context, n, channel, gOutputBuffer[gOutputBufferReadPointer]);
		}
		
		// Check if any voice is currently playing
		int sum = std::accumulate(voices, voices + NUM_VOICES, 0);
		bool allVoicesOff = sum == -1 * NUM_VOICES;
		
		gOutputBuffer[gOutputBufferReadPointer] = 0;
		gOutputBufferReadPointer++;
		if(gOutputBufferReadPointer >= CB_LENGTH)
			gOutputBufferReadPointer = 0;
		gOutputBufferWritePointer++;
		if(gOutputBufferWritePointer >= CB_LENGTH)
			gOutputBufferWritePointer = 0;
		
		// If no voices are playing copy frequency domain data into grain buffer
		gInputBufferPointer++;
		if(gInputBufferPointer >= CB_LENGTH)
			gInputBufferPointer = 0;

		gSampleCount++;
		if(gSampleCount >= FFT_HOP_SIZE) {
			gFFTInputBufferPointer = gInputBufferPointer;
			gFFTOutputBufferPointer = gOutputBufferWritePointer;
			
			// Bela_scheduleAuxiliaryTask(gFFTTask);
			
			gSampleCount = 0;
		}
		
		// Get grain audio data from voices
		if(!allVoicesOff){
			gOutputBuffer[gOutputBufferWritePointer] = 0.0f;
			for(int voiceIdx = 0; voiceIdx < NUM_VOICES; voiceIdx++){
				if(voices[voiceIdx] > NOT_PLAYING){
					gOutputBuffer[gOutputBufferWritePointer] += voiceObjects[voiceIdx].play();
				}
			}
		} 
	}
	
	// Update previous source position and range
	prevSourcePosition = currentSourcePosition;
	prevSourceRange = currentRange;
}

void cleanup(BelaContext *context, void *userData)
{
	NE10_FREE(timeDomainIn);
	NE10_FREE(timeDomainOut);
	NE10_FREE(frequencyDomain);
	NE10_FREE(cfg);
	free(gInputAudio);
	free(gWindowBuffer);
	
	NE10_FREE(grainSrcTimeDomainIn);
	// Memory for frequency domain mask
	for (int i = 0; i < GRAIN_FFT_INTERVAL; i++){
		NE10_FREE(frequencyDomainGrainBuffer[i]);
		NE10_FREE(frequencyDomainGrainBufferSwap[i]);
		NE10_FREE(grainSrcFrequencyDomain[i]);
	}
}