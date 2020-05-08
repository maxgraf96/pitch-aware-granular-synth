var guiSketch = new p5(function( sketch ) {

    let canvas_dimensions = [sketch.windowWidth, sketch.windowHeight];

	// Array containing the current grain window data
    let windowData = 0;
    // And its length
    let windowLength = 0;
    // Whether the window changed, if it changed, redraw it
    let windowBufferChanged = 0;
    
    // Positioning helpers
    let marginTop = 32;
    
    // x positions
    const labelX = 24;
    const sliderX = 224;
    let sliderValuesX = 500;
    
    // y positions
    let sourcePosY = 0;
    let grainLengthY = 0;
    let grainFrequencyY = 0;
    let grainScatterY = 0;
    let mainOutputGainY = 0;
    let windowSelY = 0;
    let windowTypeModSliderY = 0;
    const headerY = 32;
    
    // Height
	const sliderHeight = 15;
	
	// Grain window attributes
	const grainWindowWidth = 600;
	const grainWindowHeight = 400;
	const grainWindowX = 600;
	const grainWindowY = marginTop;
	
	// Flag for checking if length of source file was already set
	let isSrcFileLengthSet = false;
	
    sketch.setup = function() {
        sketch.createCanvas(canvas_dimensions[0], canvas_dimensions[1]);
        
        // Create sliders
        sourcePosY = headerY + marginTop;
        sourcePosSlider = undefined;
		
		grainLengthSlider = sketch.createSlider(0, 500, 100, 1);
        grainLengthSlider.position(sliderX, grainLengthY = sourcePosY + marginTop);
		grainLengthSlider.style('width', '240px');
		
		grainFrequencySlider = sketch.createSlider(1, 30, 1, 1);
        grainFrequencySlider.position(sliderX, grainFrequencyY = grainLengthY + marginTop);
		grainFrequencySlider.style('width', '240px');
		
		grainScatterSlider = sketch.createSlider(0, 100, 0, 1);
        grainScatterSlider.position(sliderX, grainScatterY = grainFrequencyY + marginTop);
		grainScatterSlider.style('width', '240px');
		
		mainOutputGainSlider = sketch.createSlider(0, 1, 0.5, 0.01);
        mainOutputGainSlider.position(sliderX, mainOutputGainY = grainScatterY + marginTop);
		mainOutputGainSlider.style('width', '240px');
		
		// Create select for window
		windowSel = sketch.createSelect();
		windowSel.position(sliderX, windowSelY = mainOutputGainY + marginTop);
		windowSel.option('Hann');
		windowSel.option('Tukey');
		windowSel.option('Gaussian');
		windowSel.option('Trapezoidal');
		windowSel.selected('Hann');
		windowSel.changed(windowTypeSend);
		
		windowTypeModSlider = sketch.createSlider(0.01, 1, 0.5, 0.01);
        windowTypeModSlider.position(sliderX, windowTypeModSliderY = windowSelY + marginTop);
		windowTypeModSlider.style('width', '240px');
		let windowTypeMod = 0;
		
    };

    sketch.draw = function() {
    	
    	// In order to set the range of the source position slider correctly
    	// we wait until render.cpp sends us the file length in samples
    	// (this happens during the first render() call)
    	if(!isSrcFileLengthSet && Bela.data.buffers[7] > 0){
    		// Get file length
    		let fileLengthSamples = Bela.data.buffers[7];
    		// Then add the slider to the UI
    		sourcePosSlider = sketch.createSlider(0, fileLengthSamples, fileLengthSamples / 2, 1000);
	        sourcePosSlider.position(sliderX, sourcePosY);
			sourcePosSlider.style('width', '240px');
			// Update the flag
			isSrcFileLengthSet = true;
    	}
        
        // Clear canvas
        sketch.clear();
        
        // Draw a white background
        sketch.background(255, 0);
        
        // Draw text and labels
        sketch.textSize(24);
        sketch.textStyle(sketch.NORMAL);
        
        // Draw header
		sketch.text('pitch-aware-granular-synthesis', 24, headerY);
		
		// Draw slider labels
		sketch.textSize(14);
		sketch.text('Source position (samples)', labelX, sourcePosY + sliderHeight);
		sketch.text('Grain length (ms)', labelX, grainLengthY + sliderHeight);
		sketch.text('Number of grains / s', labelX, grainFrequencyY + sliderHeight);
		sketch.text('Grain Scatter', labelX, grainScatterY + sliderHeight);
		sketch.text('Main Output Gain', labelX, mainOutputGainY + sliderHeight);
		sketch.text('Window Type', labelX, windowSelY + sliderHeight);
		sketch.text('Window Modifier', labelX, windowTypeModSliderY + sliderHeight);
		
        // Get values from sliders
        let sourcePosition = 0;
        if(sourcePosSlider !== undefined){
        	sourcePosition = sourcePosSlider.value();
        }
        let grainLength = grainLengthSlider.value();
        let grainFrequency = grainFrequencySlider.value();
        let grainScatter = grainScatterSlider.value();
        let mainOutputGain = mainOutputGainSlider.value();
        windowTypeMod = windowTypeModSlider.value();
        
        // Draw slider values
        sketch.text(sourcePosition, sliderValuesX, sourcePosY + sliderHeight);
        sketch.text(grainLength, sliderValuesX, grainLengthY + sliderHeight);
		sketch.text(grainFrequency, sliderValuesX, grainFrequencyY + sliderHeight);
		sketch.text(grainScatter, sliderValuesX, grainScatterY + sliderHeight);
		sketch.text(mainOutputGain.toFixed(2), sliderValuesX, mainOutputGainY + sliderHeight);
		
		sketch.text(windowTypeMod.toFixed(2), sliderValuesX, windowTypeModSliderY + sliderHeight);
		
		// Update the grain window rendering if it changed
		windowBufferChanged = Bela.data.buffers[1];
		if(windowBufferChanged !== undefined && windowBufferChanged[0] == 1){
			// Update window length 
			windowLength = windowBufferChanged[1];
			
			if (Bela.data.buffers[0] !== undefined){
				// Update window data
				// Slice used here to only include the relevant part of the window
				// as the window array in C++ is fixed size
				windowData = Bela.data.buffers[0].slice(0, windowLength);
			}
		}
		
		// Draw the current grain window
		drawWindow();
		
		// Send values from sliders back to render.cpp
		Bela.data.sendBuffer(2, 'int', sourcePosition);
		Bela.data.sendBuffer(3, 'int', grainLength);
		Bela.data.sendBuffer(4, 'int', grainFrequency);
		Bela.data.sendBuffer(5, 'int', grainScatter);
		Bela.data.sendBuffer(6, 'float', mainOutputGain);
		
		windowTypeSend();
    };
    
    // Helper function to draw the current grain window representation
    function drawWindow(){
    	sketch.stroke(0);
    	// draw lines
    	let px = grainWindowX;
    	let py = grainWindowHeight - (windowData[0] * grainWindowHeight);
    	for(let i = 0; i < windowData.length; i++){
	    	let x = grainWindowX + i * (grainWindowWidth / (windowData.length - 1));
	    	let y = grainWindowHeight - (windowData[i] * grainWindowHeight);
	    	sketch.line(px, py, x, y);
	    	// Update last position
	    	px = x;
	    	py = y;
    	} 
	}
	
	function windowTypeSend() {
		let item = windowSel.value();
		let coded = 0;
		switch(item){
			case "Hann":
				coded = 0;
				break;
			case "Tukey":
				coded = 1;
				break;
			case "Gaussian":
				coded = 2;
				break;
			case "Trapezoidal":
				coded = 3;
				break;
		}
		Bela.data.sendBuffer(8, 'float', [coded, windowTypeMod]);
	}
    
}, 'gui');