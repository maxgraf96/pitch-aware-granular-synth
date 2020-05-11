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
	// Filter data
	let lowpassCutoff = 20000.0;
	let lowpassQ = 0.707;
	let highpassCutoff = 30.0;
	let highpassQ = 0.707;
	
    let mainOutputGain = 0.5;
    
    // Positioning helpers
    let marginTop = 32;
    
    // x positions
    const labelX = 24;
    const sliderX = 224;
    let sliderValuesX = 500;
    
    const labelColumn2X = sliderX * 3;
    let sliderColumn2X = sliderX * 4 - 48;
    const sliderValuesColumn2X = sliderColumn2X + 284;
    
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
		
		mainOutputGainSlider = sketch.createSlider(0, 10, 5, 0.01);
        mainOutputGainSlider.position(sliderX, mainOutputGainY = grainScatterY + marginTop);
		mainOutputGainSlider.style('width', '240px');
		mainOutputGainSlider.input(mainOutputGainChanged);
		mainOutputGainSlider.addClass("main_output_gain");
		
		// Create select for window
		windowSel = sketch.createSelect();
		windowSel.position(sliderX, windowSelY = mainOutputGainY + marginTop + 52);
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
		windowTypeModSlider.addClass("window_type_mod");
		
		 // Buttons for song selection
        button = sketch.createButton('Song 1');
		button.position(sliderX, windowTypeModSliderY + 2 * marginTop);
		button.mousePressed(loadSong0);
		
		button = sketch.createButton('Song 2');
		button.position(sliderX + 72, windowTypeModSliderY + 2 * marginTop);
		button.mousePressed(loadSong1);
		
		button = sketch.createButton('Song 3');
		button.position(sliderX + 144, windowTypeModSliderY + 2 * marginTop);
		button.mousePressed(loadSong2);
		
		// Filter controls
		// Lowpass
		lowpassCutoffSlider = sketch.createSlider(1, 20000.0, 20000.0, 1);
        lowpassCutoffSlider.position(sliderColumn2X, grainLengthY);
		lowpassCutoffSlider.style('width', '240px');
		lowpassCutoffSlider.input(lowpassDataChanged);
		lowpassCutoffSlider.addClass("lp_filter");
		
		lowpassQSlider = sketch.createSlider(0.1, 5, 0.707, 0.1);
        lowpassQSlider.position(sliderColumn2X, grainLengthY + marginTop);
		lowpassQSlider.style('width', '240px');
		lowpassQSlider.input(lowpassDataChanged);
		lowpassQSlider.addClass("lp_filter");
		lowpassDataChanged();
		
		// Highpass
		highpassCutoffSlider = sketch.createSlider(1, 5000.0, 20.0, 1);
        highpassCutoffSlider.position(sliderColumn2X, mainOutputGainY);
		highpassCutoffSlider.style('width', '240px');
		highpassCutoffSlider.input(highpassDataChanged);
		highpassCutoffSlider.addClass("hp_filter");
		
		highpassQSlider = sketch.createSlider(0.1, 5, 0.707, 0.1);
        highpassQSlider.position(sliderColumn2X, mainOutputGainY + marginTop);
		highpassQSlider.style('width', '240px');
		highpassQSlider.input(highpassDataChanged);
		highpassQSlider.addClass("hp_filter");
		
		highpassDataChanged();
		
		mainOutputGainChanged();
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
		sketch.textStyle(sketch.BOLD);
		sketch.text('Main Output Gain', labelX, mainOutputGainY + sliderHeight);
		sketch.textStyle(sketch.NORMAL);
		
		// Window related
		sketch.text('Window Type', labelX, windowSelY + 14);
		sketch.text('Window Modifier', labelX, windowTypeModSliderY + sliderHeight);
		sketch.text('Source Audio Data', labelX, windowTypeModSliderY + 2 * marginTop + 15);
		
		// Filter slider labels
		sketch.text('Cutoff frequency (Hz)', labelColumn2X, grainLengthY + sliderHeight);
		sketch.text('Filter Q', labelColumn2X, grainFrequencyY + sliderHeight);
		// Highpass
		sketch.text('Cutoff frequency (Hz)', labelColumn2X, mainOutputGainY + sliderHeight);
		sketch.text('Filter Q', labelColumn2X, mainOutputGainY + marginTop + sliderHeight);
		
        // Get values from sliders
        if(sourcePosSlider !== undefined){
        	sourcePosition = sourcePosSlider.value();
        }
        grainLength = grainLengthSlider.value();
        grainFrequency = grainFrequencySlider.value();
        grainScatter = grainScatterSlider.value();
        mainOutputGain = mainOutputGainSlider.value();
        windowTypeMod = windowTypeModSlider.value();
        lowpassCutoff = lowpassCutoffSlider.value();
        lowpassQ = lowpassQSlider.value();
        highpassCutoff = highpassCutoffSlider.value();
        highpassQ = highpassQSlider.value();
        
        // Draw slider values
        sketch.text(sourcePosition, sliderValuesX, sourcePosY + sliderHeight);
        sketch.text(grainLength, sliderValuesX, grainLengthY + sliderHeight);
		sketch.text(grainFrequency, sliderValuesX, grainFrequencyY + sliderHeight);
		sketch.text(grainScatter, sliderValuesX, grainScatterY + sliderHeight);
		sketch.text(mainOutputGain.toFixed(2), sliderValuesX, mainOutputGainY + sliderHeight);
		sketch.text(windowTypeMod.toFixed(2), sliderValuesX, windowTypeModSliderY + sliderHeight);
		
		// Filter slider values
		sketch.text(lowpassCutoff, sliderValuesColumn2X, grainLengthY + sliderHeight);
		sketch.text(lowpassQ, sliderValuesColumn2X, grainFrequencyY + sliderHeight);
		sketch.text(highpassCutoff, sliderValuesColumn2X, mainOutputGainY + sliderHeight);
		sketch.text(highpassQ, sliderValuesColumn2X, mainOutputGainY + marginTop + sliderHeight);
		
		// Draw filter headers
		sketch.textStyle(sketch.BOLD);
		sketch.textSize(16);
		sketch.text("Lowpass", labelColumn2X, sourcePosY + sliderHeight);
		sketch.text("Highpass", labelColumn2X, grainScatterY + sliderHeight);
		
		// Update the grain window rendering if it changed
		windowBufferChanged = Bela.data.buffers[1];
		if(windowBufferChanged !== undefined && windowBufferChanged[0] == 1){
			// Update window length 
			windowLength = windowBufferChanged[1];
		}
		
		// Draw filter header
		sketch.textSize(20);
		sketch.textStyle(sketch.BOLD);
		sketch.push();
	    sketch.translate(620, 176);
	    sketch.rotate(sketch.radians(-90) );
	    sketch.text("FILTERS", 0,0);
		sketch.pop();
		
		if (Bela.data.buffers[0].length > 0 && windowChanged){
			windowData = Bela.data.buffers[0].slice(0, windowLength);
			setTimeout(() => {
				// Update window data
				// Slice used here to only include the relevant part of the window
				// as the window array in C++ is fixed size
				windowData = Bela.data.buffers[0].slice(0, windowLength);
				windowLength = Bela.data.buffers[1][1];

				// Draw the current grain window
				drawWindow();
				
				windowChanged = false;
				
				// Disable looping
				console.log(windowLength);
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
    
    function lowpassDataChanged(){
    	Bela.data.sendBuffer(10, 'float', [lowpassCutoff, lowpassQ]);
    	sketch.redraw();
    }
    
    function highpassDataChanged(){
    	Bela.data.sendBuffer(11, 'float', [highpassCutoff, highpassQ]);
    	sketch.redraw();
    }
    
    // Helper function to draw the current grain window representation
    function drawWindow(){
    	sketch.stroke(0);
    	const rootX = sliderX + 124;
    	const rootY = windowSelY + grainWindowSize;
    	//sketch.rect(rootX, rootY, grainWindowSize, grainWindowSize);
    	
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
    	
    	// Draw surrounding container
    	sketch.strokeWeight(0.5);
    	sketch.beginShape(sketch.LINES);
		sketch.vertex(rootX, rootY);
		sketch.vertex(rootX + grainWindowSize, rootY);
		sketch.vertex(rootX, rootY - grainWindowSize);
		sketch.vertex(rootX + grainWindowSize, rootY - grainWindowSize);
		sketch.endShape();
		sketch.strokeWeight(1);
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