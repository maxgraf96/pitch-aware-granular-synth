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
#include "SampleData.h"
#include "Voice.h"

// Audio channels
int numAudioChannels;

// Main output buffer
float gOutputBuffer[MAIN_BUFFER_LENGTH];
int gOutputBufferWritePointer = 0;
int gOutputBufferReadPointer = 0;

// Window for the main FFT that creates the grain source frequency domain buffer
float *gWindowBuffer;

// Neon params
ne10_fft_cfg_float32_t cfg;

// Grain src time domain input
ne10_fft_cpx_float32_t* grainSrcTimeDomainIn;

// Final frequency domain representation of current slice of GRAIN_FFT_INTERVAL FFT hops
std::array<ne10_fft_cpx_float32_t*, GRAIN_FFT_INTERVAL> grainSrcFrequencyDomain = {};

// Sample info
SampleData gSampleData;

// Position of last read sample from audio file
int gReadPtr = 0;

// Auxiliary task for calculating FFT
AuxiliaryTask updateGrainSrcBufferTask;
// For updating the grain window asnchronously
AuxiliaryTask updateGrainWindowTask;

int gFFTInputBufferPointer = 0;
int gFFTOutputBufferPointer = 0;

// Necessary function definition for running an auxiliary task later
void processGrainSrcBufferUpdateBackground(void*);
void processGrainWindowUpdateBackground(void *);

float *gInputAudio = NULL;

// MIDI object for receiving MIDI data
Midi midi;

// Voice indices: An array indicating which voice (default number of voices: 16) currently plays which frequency
// All voices are initially "not playing", indicated by -1.0f
float voiceIndices[NUM_VOICES] = { NOT_PLAYING };
std::vector<Voice> voiceObjects = {};

// Expose sample rate
int gSampleRate = 0;

// The one grain window to rule them all
Window* grainWindow = nullptr;
float guiWindowBuffer[MAX_GRAIN_LENGTH] = {};

// Browser-based GUI to adjust parameters
Gui gui;

// Current selected position in source file
// Can be changed via GUI
int currentSourcePosition = 0;
// Current scatter of grains in Voices (if greater than 0, will shift start positions of grains)
int currentScatter = 0;
// Current grain length in ms (global for all grains)
int currentGrainLength = 0;
// Current grain frequency (how often to trigger a grain per second)
int currentGrainFrequency = 0;
// Current grain window type (0 = Hann, 1 = Tukey, 2 = Gaussian or 3 = trapezoid)
int currentWindowType = 0;
float currentWindowModifier = 0.0f;

// Main output gain
float mainOutputGain = 0.0f;

// Previous values for GUI parameters
int prevSourcePosition = 0;
int prevScatter = 0;
int prevGrainLength = 0;
int prevGrainFrequency = 0;
int prevWindowType = 0;
float prevWindowModifier = 0.0f;

// Flag to set the file length to the gui once at startup
bool fileLengthSent = false;

void midiCallback(MidiChannelMessage message, void* arg);

bool setup(BelaContext *context, void *userData)
{
	// If the amount of audio input and output channels is not the same
	// use the minimum between number of input and output channels
	numAudioChannels = std::min(context->audioInChannels, context->audioOutChannels);
	
	// Check that we have the same number of inputs and outputs.
	if(context->audioInChannels != context->audioOutChannels){
		printf("Different number of audio outputs and inputs available. Using %d channels.\n", numAudioChannels);
	}
	
	// Retrieve a parameter passed in from the initAudio() call
	gSampleData = *(SampleData *)userData;
	rt_printf("Sample data length: %4.2f seconds \n", (gSampleData.sampleLen / context->audioSampleRate));

	gOutputBufferWritePointer += FFT_HOP_SIZE;

	// Allocate memory for FFT config
	cfg = ne10_fft_alloc_c2c_float32_neon (N_FFT);
	
	grainSrcTimeDomainIn = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));
	// Initialise grain buffers
	for (int i = 0; i < GRAIN_FFT_INTERVAL; i++){
		grainSrcFrequencyDomain[i] = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));

		for (int k = 0; k < N_FFT; k++){
			grainSrcFrequencyDomain[i][k].r = 0.0f;
			grainSrcFrequencyDomain[i][k].i = 0.0f;
		}
	}
	
	memset(gOutputBuffer, 0, MAIN_BUFFER_LENGTH * sizeof(float));

	// Allocate buffer to mirror and modify the input
	gInputAudio = (float *)malloc(context->audioFrames * numAudioChannels * sizeof(float));
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
	
	// Initialise auxiliary task	
	if((updateGrainSrcBufferTask = Bela_createAuxiliaryTask(&processGrainSrcBufferUpdateBackground, 94, "grain-src-update")) == 0)
		return false;
	
	// For async grain window update
	if((updateGrainWindowTask = Bela_createAuxiliaryTask(&processGrainWindowUpdateBackground, 90, "grain-window-update")) == 0)
		return false;
		
	// Setup MIDI
	midi.readFrom(0);
	midi.setParserCallback(midiCallback);
	
	// Expose audio sample rate
	gSampleRate = context->audioSampleRate;
	
	// Initialise window for grains
	grainWindow = new Window(MAX_GRAIN_LENGTH);
	
	// Initialise voices array 
	for (auto& voice : voiceIndices) {
		voice = NOT_PLAYING;
	}
	
	// Initialise voice voice objects
	for (int i = 0; i < NUM_VOICES; i++){
		Voice* voice = new Voice(gSampleRate, *grainWindow);
		voiceObjects.push_back(*voice);
	}
	
	// Set up the GUI
	gui.setup(context->projectName);
	// controller.setup(&gui, "Controls");	
	
	// Notifiers (Bela to p5.js)
	gui.setBuffer('f', MAX_GRAIN_LENGTH); // index 0 is used to send the grain window data to the GUI for rendering
	gui.setBuffer('d', 2); // index 1 is to indicate whether the window has changed and sends 1/0 for changed and the new length of the window
	
	// Incoming values from p5.js
	gui.setBuffer('d', 1); // index 2: Source position (in samples)
	gui.setBuffer('d', 1); // index 3: Grain length (in ms)
	gui.setBuffer('d', 1); // index 4: Grain frequency - how many grains to play per second
	gui.setBuffer('d', 1); // index 5: Grain scatter [0...100]
	gui.setBuffer('f', 1); // index 6: Main output gain [0...1]
	
	// Notifier for length of file in samples (to setup source position slider range)
	gui.setBuffer('d', 1); // index 7
	
	// Window type changed listener
	// First float is the incoming window type (0 = Hann, 1 = Tukey, 2 = Gaussian, 3 = trapezoid)
	// Second float is the incoming modifier value used to tweak the Tukey, Gaussian and trapezoid windows
	gui.setBuffer('f', 2); // index 8
	
	return true;
}

void processGrainSrcBufferUpdate(int startIdx){
	// Copy part of sample buffer into FFT input from given start index
	for (int hop = 0; hop < GRAIN_FFT_INTERVAL; hop++){
		int currentStart = hop * FFT_HOP_SIZE;
		for(int n = 0; n < N_FFT; n++) {
			grainSrcTimeDomainIn[n].r = (ne10_float32_t) (gSampleData.samples[startIdx + currentStart + n] * gWindowBuffer[n]);
			grainSrcTimeDomainIn[n].i = 0;
		}
		
		// Perform FFT -> indicated by the "0" for the last function parameter 
		ne10_fft_c2c_1d_float32_neon (grainSrcFrequencyDomain[hop], grainSrcTimeDomainIn, cfg, 0);
	}
	
	// Update grain source buffer for all playing voices
	for (int i = 0; i < NUM_VOICES; i++){
		if(voiceIndices[i] > NOT_PLAYING)
			voiceObjects[i].updateGrainSrcBuffer(grainSrcFrequencyDomain);
	}
	
	rt_printf("Done updating grain source buffer \n");
}

void processGrainWindowUpdate(){
	// Update grain window type (also sets length)
	grainWindow->updateWindow(currentGrainLength, currentWindowType, currentWindowModifier);
	
	// Adjust grains to new length
	for(Voice& voice : voiceObjects){
		voice.setGrainLength(currentGrainLength);
	}
	
	for (int i = 0; i < currentGrainLength; i++){
		guiWindowBuffer[i] = grainWindow->getAt(i);
	}
	
	// Send update to GUI
	int windowChanged[2] = {1, currentGrainLength};
	gui.sendBuffer(1, windowChanged);
	gui.sendBuffer(0, guiWindowBuffer);
	
	// Turn off window length changed again
	windowChanged[0] = 0;
	gui.sendBuffer(1, windowChanged);
}

void processGrainSrcBufferUpdateBackground(void *){
	processGrainSrcBufferUpdate(currentSourcePosition);
}

void processGrainWindowUpdateBackground(void *){
	processGrainWindowUpdate();
}

void render(BelaContext *context, void *userData)
{
	// Send file length of loaded sample ONCE when connected to initialise source position slider range
	if(gui.isConnected() && !fileLengthSent){
		rt_printf("Connected to GUI, yay! \n");
		gui.sendBuffer(7, gSampleData.sampleLen);
		fileLengthSent = true;
	}
	
	// Get slider value from p5.js
	auto sourcePositionReceiver = gui.getDataBuffer(2);
	auto grainLengthReceiver = gui.getDataBuffer(3);
	auto grainFrequencyReceiver = gui.getDataBuffer(4);
	auto grainScatterReceiver = gui.getDataBuffer(5);
	auto mainOutputGainReceiver = gui.getDataBuffer(6);
	auto windowTypeReceiver = gui.getDataBuffer(8);
	int sourcePosition = *(sourcePositionReceiver.getAsInt());
	int grainLength = *(grainLengthReceiver.getAsInt());
	int grainFrequency = *(grainFrequencyReceiver.getAsInt());
	int grainScatter = *(grainScatterReceiver.getAsInt());
	float mag = *(mainOutputGainReceiver.getAsFloat());
	float* windowTypeInput = windowTypeReceiver.getAsFloat();
	
	// Where we are in the sample
	currentSourcePosition = sourcePosition;
	// Convert grain length from ms to samples and pass to voices
	currentGrainLength = grainLength == 0 ? 1 : int(float(grainLength) * 0.001f * gSampleRate);
	// Grain frequency per second
	currentGrainFrequency = grainFrequency > 0 ? gSampleRate / grainFrequency : 1;
	// How scattered the grain start positions should be 
	currentScatter = grainScatter;	
	currentWindowType = int(windowTypeInput[0]);
	currentWindowModifier = windowTypeInput[1];
	// Main output gain
	mainOutputGain = mag; 
	
	// Update voice parameters if changed
	if(currentScatter != prevScatter){
		for(Voice& voice : voiceObjects){
			voice.setScatter(currentScatter);
		}
	}
	// Update window if changed
	if(currentGrainLength != prevGrainLength 
	|| currentWindowType != prevWindowType
	|| currentWindowModifier != prevWindowModifier
	){
		// Update grain window asynchronously
		Bela_scheduleAuxiliaryTask(updateGrainWindowTask);
	}
	if(currentGrainFrequency != prevGrainFrequency){
		for(Voice& voice : voiceObjects){
			voice.setGrainFrequency(currentGrainFrequency);
		}
	}
	
	// Get number of audio frames
	int numAudioFrames = context->audioFrames;
	
	// If source position changed, update the grain source buffer
	if (currentSourcePosition != prevSourcePosition){
		Bela_scheduleAuxiliaryTask(updateGrainSrcBufferTask);
	}

	for(int n = 0; n < numAudioFrames; n++) {
		// Write output buffer to sound output
		for(int channel = 0; channel < numAudioChannels; channel++){
			audioWrite(context, n, channel, gOutputBuffer[gOutputBufferReadPointer]);
		}
		
		// Increment output buffer pointers
		gOutputBuffer[gOutputBufferReadPointer] = 0;
		gOutputBufferReadPointer++;
		if(gOutputBufferReadPointer >= MAIN_BUFFER_LENGTH)
			gOutputBufferReadPointer = 0;
		gOutputBufferWritePointer++;
		if(gOutputBufferWritePointer >= MAIN_BUFFER_LENGTH)
			gOutputBufferWritePointer = 0;
			
		// Check if any voice is currently playing
		int sum = std::accumulate(voiceIndices, voiceIndices + NUM_VOICES, 0);
		bool allVoicesOff = sum == -1 * NUM_VOICES;
		
		// Get grain audio data from voices
		if(!allVoicesOff){
			gOutputBuffer[gOutputBufferWritePointer] = 0.0f;
			for(int voiceIdx = 0; voiceIdx < NUM_VOICES; voiceIdx++){
				if(voiceIndices[voiceIdx] > NOT_PLAYING){
					gOutputBuffer[gOutputBufferWritePointer] += voiceObjects[voiceIdx].play();
				}
			}
		} 
		
		// Apply main output gain
		gOutputBuffer[gOutputBufferWritePointer] *= mainOutputGain;
	}
	
	// Update previous source position
	prevSourcePosition = currentSourcePosition;
	prevScatter = currentScatter;
	prevGrainLength = currentGrainLength;
	prevGrainFrequency = currentGrainFrequency;
	prevWindowType = currentWindowType;
	prevWindowModifier = currentWindowModifier;
}

void midiCallback(MidiChannelMessage message, void* arg){
	// Note on event: Find the next free voice and assign frequency coming from MIDI note
	if(message.getType() == kmmNoteOn){
		if(message.getDataByte(1) > 0){
			int note = message.getDataByte(0);
			float frequency = powf(2, (note-69)/12.f)*440;
			
			// Find free voice and assign
			for (int i = 0; i < NUM_VOICES; i++){
				if (voiceIndices[i] == NOT_PLAYING) {
					// Assign frequency of incoming MIDI note
					voiceIndices[i] = frequency;
					// Trigger note on event
					voiceObjects[i].noteOn(grainSrcFrequencyDomain, frequency, currentGrainLength);
					
					// Print note info
					// rt_printf("\nnote: %d, frequency: %f \n", note, frequency);
					
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
			if (voiceIndices[i] == frequency) {
				// Reset voice
				voiceIndices[i] = NOT_PLAYING;
				
				// Trigger note off event
				voiceObjects[i].noteOff();
				
				// Break loop
				break;
			}
		}
	}
}

void cleanup(BelaContext *context, void *userData)
{
	NE10_FREE(cfg);
	free(gInputAudio);
	free(gWindowBuffer);
	NE10_FREE(grainSrcTimeDomainIn);
	
	// Memory for frequency domain mask
	for (int i = 0; i < GRAIN_FFT_INTERVAL; i++){
		NE10_FREE(grainSrcFrequencyDomain[i]);
	}
	
	delete grainWindow;
}