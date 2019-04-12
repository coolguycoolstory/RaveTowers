/*
This code and the overall design drew heavy inspiration from Hans's "MUSIC VISUALIZING Floor Lamps". 
His code can be found here: https://github.com/hansjny/Natural-Nerd/tree/master/SoundReactive2.
Code for the VU Meter began with haag991's code, found here: https://github.com/haag991/Spectrum_Visualizer

This is the RaveTowerSlave code, which receives data from the RaveTowerMaster via a UDP Broadcast over WiFi, 
and uses it to drive a strip of WS2812B LEDS with the FASTLED library. This code was written for the ESP8266 
on the Arduino IDE, but only the WiFi portions require re-writing for other chips. 

The general flow of the code is:
Setup
Connect to WiFi
Receive Data
Play Selected Pattern
If WiFi is Lost, Wait to Reacquire Signal
*/

///////USER INPUTS HERE/////////
#define TowerID 4 //This changes for each unit, and must be correct.
const char *ssid     = "RaveTowerTime";         // The SSID (name) of the Wi-Fi network you want to connect to
const char *password = "6026971660";     // The password of the Wi-Fi network (blank between quotes if it's open).
uint8_t WheelSpeed = 2; //How fast the colors go through a palette
uint8_t NoiseFilter = 60; //The input must exceed this threshold to display.
uint8_t BassTrigger = 120; //For specific patterns that require heavy bass to trigger.

#include <ESP8266WiFi.h>
#include <WiFiUDP.h>   
WiFiUDP UDP; //Creates a UDP class; necessary for subsequent UDP code to work (and errors won't be clear).

struct ClientPings {
  uint8_t ClientID;
  };
struct ClientPings Ping;   //This lets the main unit know that the receiver is still connected.

const int PingInterval = 100;
uint32_t LastSent;
uint32_t LastReceived;
bool FirstConnection = true;
bool FirstPass;

uint8_t audio[10]; //This array is populated with the data received from the main unit. 7 frequency bands and 3 commands.
uint8_t CurrentMode; 
uint8_t CurrentFunc;
uint8_t CurrentColor;
uint8_t PrevMode; //used to detect button presses.
uint8_t PrevFunc; //used to detect button presses.
uint8_t PrevColor; //used to detect button presses.

//LED SETUP//

#include <FastLED.h>
#define NUM_LEDS  144
#define LED_PIN  D3
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define Brightness  255 
CRGB leds[NUM_LEDS];

/////////////////Pattern Variables///////////////

uint8_t AudioFunc[8]; //Array populated with the 7 audio bands, and 1 random number between 0-255.
uint8_t audio_input;
const uint8_t midway = NUM_LEDS / 2; // Center mark from double level visualizer

//VU Meter//
DEFINE_GRADIENT_PALETTE(VaporwaveDefinition) {0,0,186,72,50,26,73,152,210,186,0,69,255,255,255,255}; //TEAL -> BRIGHT PINK -> LIGHT GREEN -> FULL WHITE {0,0,186,72,50,26,73,152,210,186,0,69,255,255,255,255}
DEFINE_GRADIENT_PALETTE(SunsetDefinition) {0,175,0,80,112,239,16,0,255,180,72,0}; //PURPLE -> ORANGE -> YELLOW 
DEFINE_GRADIENT_PALETTE(OutrunDefinition) {0,233,0,2,122,5,0,253,255,175,0,80}; //RED -> BLUE -> PURPLE

CRGBPalette16 VaporwavePalette = VaporwaveDefinition;
CRGBPalette16 OutrunPalette = OutrunDefinition;
CRGBPalette16 SunsetPalette = SunsetDefinition;
CRGBPalette16 VUPalette = RainbowColors_p;

uint8_t NewVU;
uint8_t CurrentVU;
uint8_t ColorIndex;
uint8_t StartIndex;

//Flames//
CRGBPalette16 FlamesPalette;
CRGBPalette16 IceFlamesPalette = CRGBPalette16(CRGB::Black, CRGB::Blue, CRGB::Aqua,  CRGB::White);
CRGBPalette16 PinkFlamesPalette = CRGBPalette16(CRGB::Black, CRGB::Magenta, CRGB::LightPink,  CRGB::White);
CRGBPalette16 GreenFlamesPalette = CRGBPalette16(CRGB::Black, CRGB::Green, CRGB::Lime,  CRGB::White);

//Confetti//
uint8_t ConfettiSetting[4];  
uint8_t vape = 128;
uint8_t outrun = 165;
uint8_t sunny;
uint8_t gHue;

//Strobes//
uint8_t StrobeHue[] = {0,0,0,130,236,229,0,161,206,17,232,46}; 
uint8_t StrobeSat[] = {0,0,0,255,170,255,255,255,255,255,255,255};
uint8_t randowhite;
const uint8_t FullStrobeDelay = 60;
uint8_t StrobeSpeeds[] = {0,15,30,45,60,75,90,105};
bool FirstStrobe = true;


uint8_t i; 
uint8_t j;
uint8_t k; 

////////////////////////////////////////////

void setup() {
  delay(2000);
  
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip ); 
  FastLED.setMaxPowerInVoltsAndMilliamps(5,5750); //I had 5V 6A DC Wall adapters; at 144 LEDs, the max draw is 144*.06 = 8.64 A, so this is an added precaution.
  FastLED.setBrightness(Brightness);
  
  WiFi.begin(ssid, password);
  UDP.begin(124); //Starts UDP, opening port 124; Data is received on this port.
  
  ConnectToWiFi();
  SendPing();
  }

//THIS SECTION IS FOR WI-FI MANAGEMENT//

 void ConnectToWiFi() {
  WiFi.mode(WIFI_STA);
  FastLED.clear(); //Clears any pattern currently being displayed.
  i = 0;
  while (WiFi.status() != WL_CONNECTED) { //wait for the WiFi to connect. Sometimes the chip won't realize it's lost connection yet even after the pre-set time-out. 
    delay(1000);
    leds[i] =  CHSV(160,255,255); //Add a blue LED every second to signify waiting for connection.
    FastLED.show();
    i++;

    if (i == 10) {
      FastLED.clear(); //Clear after ten seconds.
      i = 0;
    }
    
    FirstConnection = true; //This becomes true only when the receiver has been waiting for a connection.
    }
  if (FirstConnection) { //If the receiver was waiting for a connection, light up Green when the while loop exits after a connection is established.
  delay(2);
  for(i = 0; i < NUM_LEDS; i++) {
  leds[i] = CHSV(96,255,255);
  }
  delay(2);
  FastLED.show();
  delay(1000);
  }
  LastReceived = millis();
 }

void SendPing() {
  Ping.ClientID = TowerID; //The receiver sends its ID Number to the base unit to let it know it's still connected.
  IPAddress ip(192,168,4,1); 
  UDP.beginPacket(ip, 123); //this targets the IP Address and the port of the base unit. This must match the port that is opened on the base unit.
  UDP.write((char*)&Ping, sizeof(struct ClientPings));
  UDP.endPacket();
  LastSent = millis();
}

void loop() {
  
  if (millis() - LastSent > PingInterval) {  //This sends a Ping every interval you set. If the selected pattern takes too much time to display, this will be every single loop.
    SendPing();
  }

  if(millis() - LastReceived >= 5000) { //If data hasn't been received for 5 seconds, wait for a connection.
  FirstConnection = false; //This is so the green "connected" lamp doesn't display if the receiver thinks it's still connected.
  ConnectToWiFi();
  }

  GetData();
  audio_input = AudioFunc[CurrentFunc];  //"CurrentFunc" is received from the base unit, and used to select the frequency band.
  
  switch (CurrentMode) {  //Calls the current mode, based on the "CurrentMode" data received from the base unit.
    case 0:  
        MirrorVUMeter();
        break;
    case 1:  
        EVERY_N_MILLISECONDS(10) { //FastLED code that instructs the program to run the code at a given interval without pausing the program with delays.
        MirrorFlames(); //Taken from FastLED example code.
        }
        break;
    case 2: 
        Confetti(); //Taken from FastLED example code.
        break;
    case 3:  
        EVERY_N_MILLISECONDS(30) {
        Strobe(); //Strobes 7 LED segments
        }
        break;
    case 4:  
        if(FirstStrobe) {  //On the first pass after this mode is selected, delay based on the TowerID #
          i = TowerID*random(20,40);
          delay(i);
        }
        FullStrobe(); //Strobes the entire LED strip. This code incorporates hard delays, which can cause the base unit to become momentarily unresponsive.
        break;
    case 5:   //This case turns the towers into LAMPs. Multicolor options are also provided (in the case that there are 4 lamps).
        if (TowerID == 1 || TowerID == 3) {
          j = 1;
        }
        else {
          j = 0;
        }
        fill_solid(leds,NUM_LEDS,CHSV(StrobeHue[3*CurrentColor+j],StrobeSat[3*CurrentColor+j],Brightness)); //Colors are selected from an array defined previously.
        break;
  
  }

FastLED.show();  
delay(1); 	//A 1 millisecond delay for good luck at the end of each loop. In a simpler version of this code, this was necessary to ensure that UDP packets were 
			//received instead of the chip losing connection and crashing out.
  
}


void GetData() {
  int packetSize = UDP.parsePacket();
  if (packetSize) { //this is a true/false statement for whether or not a packet has been received
  i = UDP.read((char*)&audio, sizeof(audio));
  LastReceived = millis();
  CurrentMode = audio[7]; //The last three values in the array are the commands.
  CurrentFunc = audio[8];
  CurrentColor = audio[9];

  for (i = 0; i<8; i++) {
    AudioFunc[i] = audio[i]; //This may not be efficient, but this new array only contains data to be used in the LED patterns.
  }
  AudioFunc[7] = random8(); //This random value can be used if no music is connected to the base unit. Range is 0-255.
 
 if (FirstConnection) {  //This clears the LED array if this is the first time data has been received since connecting. Clears away the Green "connected" pattern.
    delay(1);
    FastLED.clear();
    delay(1);
    FirstConnection = false;
  }
 }
  
  if (CurrentMode != PrevMode) { //This clears the LED array if a new pattern is selected.
    FastLED.clear();
    FirstStrobe = true; //Also, this resets the random first delay for the FullStrobe pattern.
  }
  PrevMode = CurrentMode;

}

void MirrorVUMeter() { //LEDs are filled based on the magnitude of the frequency band selected. They begin at the middle and go outward.

  if (CurrentColor == 1) VUPalette = VaporwavePalette; ////Palette selected from the "CurrentColor" command using palettes defined previously.
  else if (CurrentColor == 2) VUPalette = OutrunPalette;
  else if (CurrentColor == 3) VUPalette = SunsetPalette;
  else VUPalette = RainbowColors_p;
  
  if (audio_input > NoiseFilter) //This prevents background noise from activating the display.
  {
    NewVU = map(audio_input,NoiseFilter,255,0,midway); //This determines how many LEDs should be lit based on the volume level.
    
    if (NewVU > CurrentVU) // Only adjust levelof led if level is higher than current level
      CurrentVU = NewVU;    
  }

  ColorIndex = StartIndex; //Index from which to begin the palete.
  for(int i = NUM_LEDS - 1; i >= midway; i--) {
    if (i < CurrentVU + midway) {
      leds[i] = ColorFromPalette(VUPalette,ColorIndex,Brightness,NOBLEND);
      leds[(midway - i) + midway] = ColorFromPalette(VUPalette,ColorIndex,Brightness,NOBLEND);
    }
    else
      leds[i] = CRGB(0, 0, 0);
      leds[midway - CurrentVU] = CRGB(0, 0, 0);
  
  ColorIndex = ColorIndex + 1;
  }
StartIndex = StartIndex + WheelSpeed;
   
    if (CurrentVU > 0)
      CurrentVU--;
  
  }

void MirrorFlames() {  //Modified from the "Fire2012WithPalette" Example
// Array of temperature readings at each simulation cell
  static byte heat[midway];
  
  if (CurrentColor == 1) FlamesPalette = IceFlamesPalette; //Palette selected from the "CurrentColor" command  using palettes defined previously.
  else if (CurrentColor == 2) FlamesPalette = PinkFlamesPalette;
  else if (CurrentColor == 3) FlamesPalette = GreenFlamesPalette;
  else FlamesPalette = HeatColors_p;
    

  // Step 1.  Cool down every cell a little
    for(i = 0; i < midway; i++) {
      heat[i] = qsub8( heat[i],  random8(0, ((70 * 10) / midway) + 2)); //The number multiplied by 10 is the cooling factor. Suggested range is 20-100
    }
  
    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for(i= midway - 1; i >= 2; i--) {
      heat[i] = (heat[i - 1] + heat[i - 2] + heat[i - 2] ) / 3;
    }
    
    // Step 3.  Ignite new 'sparks' of heat near the bottom, based on the selected input. 
    if( audio_input > BassTrigger ) {  //Audio must be greater than the value to trigger the event. Regular range is 50-200 with "random".
      i = random8(7);
      heat[i] = qadd8( heat[i], random8(160,255) );
    }

    // Step 4.  Map from heat cells to LED colors
    for(i = 0; i < midway; i++) {
      byte FlameIndex = scale8( heat[i], 240); //This scales heat value from 255 to 240. Necessary because the last 16 palette values forcibly loop back to the starting value.
    CRGB color = ColorFromPalette( FlamesPalette, FlameIndex);  //This assigns a color based on the palette you're using.
      leds[midway+i] = color;
      leds[midway-1-i] = leds[midway+i];
     } 
  }

void Confetti() {   //Modified from the FastLED example code.                                          

	gHue++;  //The below code limits the colors to a certain band, while gHue cycles through the whole rainbow.
    vape++; 
    outrun++;
    sunny++;
    if (vape == 224) {vape = 128;}
    if (outrun == 255) {outrun = 165;}
    if (sunny == 64) {sunny = 0;}
    ConfettiSetting[0] = gHue;
    ConfettiSetting[1] = vape;
    ConfettiSetting[2] = outrun;
    ConfettiSetting[3] = sunny;

   
  fadeToBlackBy( leds, NUM_LEDS, 10);
  i = random8(NUM_LEDS);
  leds[i] += CHSV(ConfettiSetting[CurrentColor] + random8(64), 200, Brightness-50); //The -50 to brightness enables some Sound Reactive-ness below.
	
	j = map(audio_input-BassTrigger,BassTrigger,255,0,50); //This code adds brightness (up to 50) to the led selected above, but only if the audio exceeds the bass trigger.
	if (audio_input > BassTrigger) {
		leds[i] += CHSV(0,0,j); 
    }
}

void Strobe() {  

  nscale8(leds, NUM_LEDS, 100);
  randowhite = random8(3,NUM_LEDS-4);
  if( AudioFunc[CurrentFunc] > BassTrigger); { 		//This creates a 7 LED long strobe every time the audio exceeds the bass input. I've observed that this tends to happen every single time,
    j = random(3*CurrentColor,3*CurrentColor+3);	//so perhaps the BassTrigger should be increased, or eliminated entirely.
    for (i = randowhite-3; i<randowhite+4; i++) {
      leds[i] = CHSV(StrobeHue[j],StrobeSat[j],Brightness);
      }
}
}



void FullStrobe() {							//This lights up the entire strip as a strobe, and it is BRIGHT. However, incorporating a hard delay is
											//the only way I can think of that forces the code to run in order instead of concurrently (as with EVERY_N_etc).
  delay(FullStrobeDelay);					//This is how long the strobe is lit.
  fill_solid(leds,NUM_LEDS,CHSV(0,0,0));  	//This blacks out the strip.
  FastLED.show();							
  delay(FullStrobeDelay + 10*CurrentFunc); 			//This is how long the delay in between strobes is, as determined by the CurrentFunc command.
  if( audio_input > BassTrigger + 50); { 			//This triggers the strobe every time the audio exceeds the bass input. I've observed that this tends to happen every single time,
      j = random(3*CurrentColor,3*CurrentColor+3);	////so perhaps the BassTrigger should be increased, or eliminated entirely.
      fill_solid(leds,NUM_LEDS,CHSV(StrobeHue[j],StrobeSat[j],Brightness));
     }
  FirstStrobe = false; //Prevents the first delay from happening while this mode is selected.
}
