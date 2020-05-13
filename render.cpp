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

#include "Globals.h"
#include "SampleData.h"
#include "Voice.h"
#include "Lowpass.h"
#include "Highpass.h"

// ---------------------------------- general ----------------------------------------
// Expose sample rate (in setup()
int gSampleRate = 0;
// Which song serves as the source buffer
// 0 = betti.wav, 1 = nicefornothing.wav, 2 = jazzo.wav
int currentSong = 0;

// Audio channels
int numAudioChannels;

// Main output buffer
float gOutputBuffer[MAIN_BUFFER_LENGTH];
int gOutputBufferWritePointer = 0;
int gOutputBufferReadPointer = 0;
// ---------------------------------- end general -------------------------------------
// ---------------------------------- FFT related -------------------------------------
// Window for the main FFT that creates the grain source frequency domain buffer
float *gWindowBuffer;

// Neon params
ne10_fft_cfg_float32_t cfg;

// Grain src time domain input
ne10_fft_cpx_float32_t* grainSrcTimeDomainIn;

// Final frequency domain representation of current slice of GRAIN_FFT_INTERVAL FFT hops
std::array<ne10_fft_cpx_float32_t*, GRAIN_FFT_INTERVAL> grainSrcFrequencyDomain = {};

// Sample info
SampleData* gSampleData;

// ---------------------------------- end FFT related -----------------------------------
// ---------------------------------- auxiliary tasks -----------------------------------
// Auxiliary task for calculating FFT
AuxiliaryTask updateGrainSrcBufferTask;

// Auxiliary task for updating the grain window asnchronously
AuxiliaryTask updateGrainWindowTask;

// Convenience function definitions for running an auxiliary task later
void processGrainSrcBufferUpdateBackground(void*);
void processGrainWindowUpdateBackground(void *);
// ---------------------------------- end auxiliary tasks --------------------------------
// ---------------------------------- Voices  --------------------------------------------
// MIDI object for receiving MIDI data
Midi midi;

// Voice indices: An array indicating which voice (default number of voices: 10) currently plays which frequency
// All voices are initially "not playing", indicated by -1.0f
float voiceIndices[NUM_VOICES] = { NOT_PLAYING };
// Vector containing the voice objects (i.e. instances of the Voice class) in the same order as the indices
std::vector<Voice> voiceObjects = {};
// ---------------------------------- end Voices -----------------------------------------
// ---------------------------------- Grain window ---------------------------------------
// The one grain window used for all grains in all voices (to rule them all)
Window* grainWindow = nullptr;
// A corresponding window buffer which can be sent to p5 js to display the current window in the GUI
float guiWindowBuffer[MAX_GRAIN_LENGTH] = {};
// ---------------------------------- end grain window -----------------------------------
// ---------------------------------- GUI related ----------------------------------------
// Browser-based GUI to adjust system parameters
Gui gui;

// Current selected position in source file
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

// Lowpass filter data
float currentLowpassCutoff = 20000.0f;
float currentLowpassQ = 0.707f;
float currentHighpassCutoff = 30.0f;
float currentHighpassQ = 0.707f;

// Main output gain
float mainOutputGain = 5.0f;

// Previous values for GUI parameters
int prevSourcePosition = 0;
int prevScatter = 0;
int prevGrainLength = 0;
int prevGrainFrequency = 0;
int prevWindowType = 0;
float prevWindowModifier = 0.0f;
float prevLowpassCutoff = 0.0f;
float prevLowpassQ = 0.0f;
float prevHighpassCutoff = 0.0f;
float prevHighpassQ = 0.0f;

// Flag to set the file length to the gui once at startup
bool fileLengthSent = false;

// ---------------------------------- end GUI related -------------------------------------
// ---------------------------------- Filters ---------------------------------------------
std::unique_ptr<Lowpass> lowpass;
std::unique_ptr<Highpass> highpass;
// ---------------------------------- end Filters------------------------------------------
// Function definition here to have setup() as the first method in render.cpp
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
	
	// Retrieve songs from main.cpp initAudio() call
	gSampleData = &songs[0];
	
	rt_printf("Sample data length: %4.2f seconds \n", (gSampleData->sampleLen / context->audioSampleRate));

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
	
	// Allocate output buffer memory
	memset(gOutputBuffer, 0, MAIN_BUFFER_LENGTH * sizeof(float));

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
	
	// Initialise GUI window
	int defaultWindowSize = int(100.0f * 0.001f * gSampleRate);
	for (int i = 0; i < defaultWindowSize; i++){
		guiWindowBuffer[i] = grainWindow->getAt(i);
	}
	
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
	
	// Buffer for receiving song selection signal
	gui.setBuffer('d', 1); // index 9
	
	// Buffer for lowpass filter cutoff frequency and q
	gui.setBuffer('f', 2); // index 10
	
	// Buffer for highpass filter cutoff frequency and q
	gui.setBuffer('f', 2); // index 11
	
	// Setup filters
	lowpass.reset(new Lowpass(float(gSampleRate)));
	highpass.reset(new Highpass(float(gSampleRate)));
	
	return true;
}

/* 
 * Updates the grain source buffer by creating a frequency domain representation 
 * of the current window in the source material.
 * The new window data is then passed to all playing voices (the other voices mask it dynamically on noteOn events)
*/
void processGrainSrcBufferUpdate(int startIdx){
	// Copy part of sample buffer into FFT input from given start index
	for (int hop = 0; hop < GRAIN_FFT_INTERVAL; hop++){
		int currentStart = hop * FFT_HOP_SIZE;
		for(int n = 0; n < N_FFT; n++) {
			grainSrcTimeDomainIn[n].r = (ne10_float32_t) (gSampleData->samples[startIdx + currentStart + n] * gWindowBuffer[n]);
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

/*
 * Triggered via the UI. Updates the window used to control amplitudes of all grains.
 * Sends updated window back to UI.
*/
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

// ----------------------------- Methods used by auxiliary tasks -----------------------------
void processGrainSrcBufferUpdateBackground(void *){
	processGrainSrcBufferUpdate(currentSourcePosition);
}

void processGrainWindowUpdateBackground(void *){
	processGrainWindowUpdate();
}
// ----------------------------- end methods used by auxiliary tasks -----------------------------

void render(BelaContext *context, void *userData)
{
	// Get number of audio frames
	int numAudioFrames = context->audioFrames;
	
	// Send file length of loaded sample ONCE when connected to GUI to initialise source position slider range
	if(gui.isConnected() && !fileLengthSent){
		rt_printf("Connected to GUI! \n");
		gui.sendBuffer(7, gSampleData->sampleLen);
		fileLengthSent = true;
		
		// Send gui window data once
		gui.sendBuffer(0, guiWindowBuffer);
	} else if(!gui.isConnected() && fileLengthSent){
		fileLengthSent = false;
	}
	
	// Get slider values from p5.js
	auto sourcePositionReceiver = gui.getDataBuffer(2);
	auto grainLengthReceiver = gui.getDataBuffer(3);
	auto grainFrequencyReceiver = gui.getDataBuffer(4);
	auto grainScatterReceiver = gui.getDataBuffer(5);
	auto mainOutputGainReceiver = gui.getDataBuffer(6);
	auto windowTypeReceiver = gui.getDataBuffer(8);
	auto songIDReceiver = gui.getDataBuffer(9);
	auto lowpassReceiver = gui.getDataBuffer(10);
	auto highpassReceiver = gui.getDataBuffer(11);
	
	// Unpack values
	int sourcePosition = *(sourcePositionReceiver.getAsInt());
	int grainLength = *(grainLengthReceiver.getAsInt());
	int grainFrequency = *(grainFrequencyReceiver.getAsInt());
	int grainScatter = *(grainScatterReceiver.getAsInt());
	float mag = *(mainOutputGainReceiver.getAsFloat());
	float* windowTypeInput = windowTypeReceiver.getAsFloat();
	int incomingSongID = *(songIDReceiver.getAsInt());
	
	// Set values for lowpass filter
	float* lowpassData = lowpassReceiver.getAsFloat();
	currentLowpassCutoff = lowpassData[0];
	currentLowpassQ = lowpassData[1];
	
	// Set values for highpass filter
	float* highpassData = highpassReceiver.getAsFloat();
	currentHighpassCutoff = highpassData[0];
	currentHighpassQ = highpassData[1];
	
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
	
	// Calculate new filter coefficients if changed
	if(currentLowpassCutoff != prevLowpassCutoff || currentLowpassQ != prevLowpassQ){
		lowpass->calculate_coefficients(currentLowpassCutoff, currentLowpassQ);
	}
	if(currentHighpassCutoff != prevHighpassCutoff || currentHighpassQ != prevHighpassQ){
		highpass->calculate_coefficients(currentHighpassCutoff, currentHighpassQ);
	}
	
	// Update source material if changed in the UI
	// This happens when one of the three buttons is clicked
	if(incomingSongID != currentSong){
		currentSong = incomingSongID;
		gSampleData = &songs[currentSong];
		// Reset pointers
		gOutputBufferReadPointer = 0;
		gOutputBufferWritePointer = 0;
		
		// Update file length in UI
		gui.sendBuffer(7, gSampleData->sampleLen);
		fileLengthSent = true;
		
		rt_printf("Song changed to %i \n", currentSong);
	}
	
	// Update voice parameters if changed
	if(currentScatter != prevScatter){
		for(Voice& voice : voiceObjects){
			voice.setScatter(currentScatter);
		}
	}
	// Update grain window if changed
	if(currentGrainLength != prevGrainLength 
	|| currentWindowType != prevWindowType
	|| currentWindowModifier != prevWindowModifier
	){
		// Update grain window asynchronously
		Bela_scheduleAuxiliaryTask(updateGrainWindowTask);
	}
	
	// Update grain frequency (number of grains per second) if changed
	if(currentGrainFrequency != prevGrainFrequency){
		for(Voice& voice : voiceObjects){
			voice.setGrainFrequency(currentGrainFrequency);
		}
	}
	
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
		
		// Apply filters
		float out = lowpass->processSample(gOutputBuffer[gOutputBufferWritePointer]);
		out = highpass->processSample(out);
		
		// Apply main output gain
		gOutputBuffer[gOutputBufferWritePointer] = out * mainOutputGain;
	}
	
	// Update previous state of parameters
	prevSourcePosition = currentSourcePosition;
	prevScatter = currentScatter;
	prevGrainLength = currentGrainLength;
	prevGrainFrequency = currentGrainFrequency;
	prevWindowType = currentWindowType;
	prevWindowModifier = currentWindowModifier;
	prevLowpassCutoff = currentLowpassCutoff;
	prevLowpassQ = currentLowpassQ;
	prevHighpassCutoff = currentLowpassCutoff;
	prevHighpassQ = currentHighpassQ;
}

/*
 * Handling of MIDI note-on and note-off events
*/
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

/* 
 * Release memory allocated in setup()
*/
void cleanup(BelaContext *context, void *userData)
{
	NE10_FREE(cfg);
	free(gWindowBuffer);
	NE10_FREE(grainSrcTimeDomainIn);
	
	// Memory for frequency domain mask
	for (int i = 0; i < GRAIN_FFT_INTERVAL; i++){
		NE10_FREE(grainSrcFrequencyDomain[i]);
	}
	
	delete grainWindow;
}