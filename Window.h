/***** 
 * 
 * Window.h 
 * Abstraction of the window used for the grains
 * Only one instance of this will be created in render.cpp 
 * and subsequently passed to all the Voices/Grains
*****/
#include <cmath>
#include <array>
#include "Constants.h"

class Window {
	public:
		// Create a hann window of max length
		Window();
		// Create a Hann window
		Window(int length);
		
		// Set length
		void setLength(int length);
		
		// Window types
		enum WindowType {
			hann = 0,
			tukey,
			gaussian,
			trapezoidal
		};
		
		// Update window type
		void updateWindow(int length, int type, float modifier);
		
		// Getter for window array data at index
		float getAt(int index);
		std::array<float, MAX_GRAIN_LENGTH> getFullData();
		
		static WindowType parseWindowType(int input);
		
		
		~Window();
		
	private:
		// Length in samples
		int length = 0;
		
		// Current window type
		WindowType windowType = hann;
		
		// Modifier for tukey, gaussian and trapezoidal windows
		float modifier = 0.0f;
		
		// Array for windowData
		std::array<float, MAX_GRAIN_LENGTH> window = {};
};
