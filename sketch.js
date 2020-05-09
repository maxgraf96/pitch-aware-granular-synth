var guiSketch = new p5(function( sketch ) {
    let canvas_dimensions = [sketch.windowWidth, sketch.windowHeight];

	// Array containing the current grain window data
    let windowData = 0;
    // And its length
    let windowLength = 0;
    // Whether the window changed, if it changed, redraw it
    let windowBufferChanged = 0;
    
    let grainLength = 100;
	let grainFrequency = 1;
	let grainScatter = 0;
	let windowTypeMod = 0;
    let mainOutputGain = 0.5;
    
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
    const headerY = 48;
    
    // Height
	const sliderHeight = 27;
	
	// Grain window attributes
	const grainWindowSize = 75; // => 75 px square

	// Flag for checking if length of source file was already set
	let isSrcFileLengthSet = false;
	let windowChanged = true;
	let modChanged = false;
	
	// Whether it's the first run
	let firstRun = true;

    sketch.setup = function() {
    	p5.disableFriendlyErrors = true;
    	// Limit fps
    	sketch.setFrameRate(30);
        sketch.createCanvas(canvas_dimensions[0], canvas_dimensions[1]);
  
        // Create sliders
        sourcePosY = headerY + marginTop;
        sourcePosSlider = undefined;
        // The value
        sourcePosition = 0;
		
		grainLengthSlider = sketch.createSlider(0, 500, 100, 1);
        grainLengthSlider.position(sliderX, grainLengthY = sourcePosY + marginTop);
		grainLengthSlider.style('width', '240px');
		grainLengthSlider.input(grainLengthChanged);
		grainLengthChanged();
		
		grainFrequencySlider = sketch.createSlider(1, 30, 1, 1);
        grainFrequencySlider.position(sliderX, grainFrequencyY = grainLengthY + marginTop);
		grainFrequencySlider.style('width', '240px');
		grainFrequencySlider.input(grainFrequencyChanged);
		grainFrequencyChanged();
		
		grainScatterSlider = sketch.createSlider(0, 100, 0, 1);
        grainScatterSlider.position(sliderX, grainScatterY = grainFrequencyY + marginTop);
		grainScatterSlider.style('width', '240px');
		grainScatterSlider.input(grainScatterChanged);
		grainScatterChanged();
		
		mainOutputGainSlider = sketch.createSlider(0, 1, 0.5, 0.01);
        mainOutputGainSlider.position(sliderX, mainOutputGainY = grainScatterY + marginTop);
		mainOutputGainSlider.style('width', '240px');
		mainOutputGainSlider.input(mainOutputGainChanged);
		mainOutputGainChanged();
		
		// Create select for window
		windowSel = sketch.createSelect();
		windowSel.position(sliderX, windowSelY = mainOutputGainY + marginTop + 14);
		windowSel.option('Hann');
		windowSel.option('Tukey');
		windowSel.option('Gaussian');
		windowSel.option('Trapezoidal');
		windowSel.selected('Hann');
		windowSel.changed(windowTypeSend);

		windowTypeModSlider = sketch.createSlider(0.01, 1, 0.5, 0.01);
        windowTypeModSlider.position(sliderX, windowTypeModSliderY = windowSelY + marginTop + 48);
		windowTypeModSlider.style('width', '240px');
		windowTypeModSlider.input(windowTypeModChanged);
		
		 // Buttons for song selection
        button = sketch.createButton('betti');
		button.position(sliderX, windowTypeModSliderY + 2 * marginTop);
		button.mousePressed(loadSong0);
		
		button = sketch.createButton('hawt');
		button.position(sliderX + 48, windowTypeModSliderY + 2 * marginTop);
		button.mousePressed(loadSong1);
		
		button = sketch.createButton('oneplustwo');
		button.position(sliderX + 96, windowTypeModSliderY + 2 * marginTop);
		button.mousePressed(loadSong2);
    };

    sketch.draw = function() {
    	// In order to set the range of the source position slider correctly
    	// we wait until render.cpp sends us the file length in samples
    	// (this happens during the first render() call and later if the song is changed in the UI)
    	if(!isSrcFileLengthSet && Bela.data.buffers[7] > 0){
    		// Get file length
    		let fileLengthSamples = Bela.data.buffers[7];
    		
    		// On song changes the source position slider needs to be removed and then re-added
    		if(!firstRun){
    			sourcePosSlider.remove();
    		}
    		
    		// Then add the slider to the UI
    		sourcePosSlider = sketch.createSlider(0, fileLengthSamples, fileLengthSamples / 2, 1000);
	        sourcePosSlider.position(sliderX, sourcePosY);
			sourcePosSlider.style('width', '240px');
			sourcePosSlider.input(sourcePositionChanged);
			firstRun = false;
		
			// Send updated src position once
			sourcePosition = fileLengthSamples / 2;
			sourcePositionChanged();
			
			// Update the flag
			isSrcFileLengthSet = true;
    	}
        
        // Draw a white background
        sketch.background(255);
        
        sketch.noStroke();
        
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
		sketch.text('Window Type', labelX, windowSelY + sliderHeight - 14);
		sketch.text('Window Modifier', labelX, windowTypeModSliderY + sliderHeight);
		sketch.text('Source Audio Data', labelX, windowTypeModSliderY + 2 * marginTop + 15);
		
        // Get values from sliders
        if(sourcePosSlider !== undefined){
        	sourcePosition = sourcePosSlider.value();
        }
        grainLength = grainLengthSlider.value();
        grainFrequency = grainFrequencySlider.value();
        grainScatter = grainScatterSlider.value();
        mainOutputGain = mainOutputGainSlider.value();
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
		}
		
		if (Bela.data.buffers[0].length > 0 && windowChanged){
			windowData = Bela.data.buffers[0].slice(0, windowLength);
			setTimeout(() => {
				// Update window data
				// Slice used here to only include the relevant part of the window
				// as the window array in C++ is fixed size
				windowData = Bela.data.buffers[0].slice(0, windowLength);

				// Draw the current grain window
				drawWindow();
				
				windowChanged = false;
				
				// Disable looping
				console.log("Loop off");
				sketch.noLoop();
				
				
			}, 300);
		}
		
		if(!windowChanged){
			// Draw the current grain window
			drawWindow();
		}
    };
    
    function sourcePositionChanged(){
		Bela.data.sendBuffer(2, 'int', sourcePosition);
    	sketch.redraw();
    }
    
    function grainLengthChanged(){
    	Bela.data.sendBuffer(3, 'int', grainLength);
    	sketch.redraw();
    }
    
    function grainFrequencyChanged(){
    	Bela.data.sendBuffer(4, 'int', grainFrequency);
    	sketch.redraw();
    }
    
    function grainScatterChanged(){
    	Bela.data.sendBuffer(5, 'int', grainScatter);
    	sketch.redraw();
    }
    
    function windowTypeModChanged(){
    	modChanged = true;
    	windowTypeSend();
    	modChanged = false;
    }
    
    function mainOutputGainChanged(){
    	Bela.data.sendBuffer(6, 'float', mainOutputGain);
    	sketch.redraw();
    }
    
    // Helper function to draw the current grain window representation
    function drawWindow(){
    	sketch.stroke(0);
    	const rootX = sliderX + 124;
    	const rootY = windowSelY + grainWindowSize;
    	// draw lines
    	let px = rootX;
    	let py = rootY - (windowData[0] * grainWindowSize);
    	for(let i = 0; i < windowData.length; i++){
	    	let x = rootX + i * (grainWindowSize / (windowData.length - 1));
	    	let y = rootY - (windowData[i] * grainWindowSize);
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
				// For the trapezoidal window the modifier value is in the range [0...10]
				windowTypeMod = windowTypeMod * 10;
				break;
		}
		
		Bela.data.sendBuffer(8, 'float', [coded, windowTypeMod]);

		windowChanged = true;
		// Only loop on select change as mod change calls redraw by itself
		if(!modChanged){
			sketch.loop();
		} else {
			sketch.redraw();
		}
	}
	
	function loadSong0(){
		isSrcFileLengthSet = false;
		// Loop until new file length is received
		sketch.loop();
		
		Bela.data.sendBuffer(9, 'int', 0);
	}
	
	function loadSong1(){
		isSrcFileLengthSet = false;
		// Loop until new file length is received
		sketch.loop();
		
		Bela.data.sendBuffer(9, 'int', 1);
	}
	
	function loadSong2(){
		isSrcFileLengthSet = false;
		// Loop until new file length is received
		sketch.loop();
		
		Bela.data.sendBuffer(9, 'int', 2);
	}
    
}, 'gui');