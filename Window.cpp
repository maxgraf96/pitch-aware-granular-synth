/***** Window.cpp *****/
#include "Window.h"

Window::Window(){
	Window(MAX_GRAIN_LENGTH);
}

// Create a hann window
Window::Window(int length){
	this->length = length;
	
	// Calculate hann window for given length
	for(int i = 0; i < length; i++) {
		window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (float)(length - 1)));
	}
}

void Window::setLength(int length){
	this->length = length;
	// Update window 
	switch (windowType){
		case hann: {
			for(int i = 0; i < length; i++) {
				window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / float(length - 1)));
			}
			break;	
		}
		case tukey: {
			// For a tukey window the modifier specifies the truncation height and must be in the range [0...1]
			float truncationHeight = modifier;
    		float f = 0.0;
    		for(int i = 0; i < length; i++) {
    			f = 1.0f / (2.0f * truncationHeight) * (1 - cos(2 * M_PI * i / float(length)));
    			window[i] = f < 1.0f ? f : 1.0f;
    		}
			break;
		}
		case gaussian: {
			// For a gaussian window the modifier specifies the sigma value in the range [0...1]
			float sigma = modifier;
			for(int i = 0; i < length; i++) {
    			window[i] = pow(exp(1), -0.5f * pow((( i - length / 2) / (sigma * length / 2.0f)), 2.0f)); 
			}
			break;
		}
		case trapezoidal: {
			float slope = modifier;
    		float x = 0.0f;
    		float f1 = 0.0f;
    		float f2 = 0.0f;
    		for (int i = 0; i < length; i++){
    			x = float(i) / float(length);
				f1 = slope * x;
    			f2 = -1.0f * slope * (x - (slope - 1.0f) / slope) + 1.0f;
    			window[i] = x < 0.5 ? (f1 < 1 ? f1 : 1) : (f2 < 1 ? f2 : 1);
    		}
			break;
		}
		
		default:
			break;
	}	
}

void Window::updateWindow(int length, int type, float modifier){
	this->windowType = static_cast<WindowType>(type);
	this->modifier = modifier;
	
	// Recalculate window
	setLength(length);
}

float Window::getAt(int index){
	return window[index];
}

std::array<float, MAX_GRAIN_LENGTH> Window::getFullData(){
	return window;
}

static Window::WindowType parseWindowType(int input){
	return static_cast<Window::WindowType>(input);
};

Window::~Window(){};