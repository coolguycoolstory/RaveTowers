/*
This code and the overall design drew heavy inspiration from Hans's "MUSIC VISUALIZING Floor Lamps". 
His code can be found here: https://github.com/hansjny/Natural-Nerd/tree/master/SoundReactive2.
Other inspiration came from haag991's code, found here: https://github.com/haag991/Spectrum_Visualizer

This is the RaveTowerMaster code. An audio signal is converted into multiple analog voltage values using an Adafruit Spectrum Shield,
and this data is sent along with commands to a set number of RaveTowerSlave units via a UDP Broadcast over WiFi. 
Commands are toggled using push buttons, and current selections and system status are shown on a 128x64 OLED screen . 
This code was written for the ESP8266 on the Arduino IDE; Using an ESP32 requires a small re-write to WiFi portions and pin assignments
shot, but I could not resolve an unacceptable delay between the source audio and the Slave unit LED displays. Resolving this 
issue could mean more precise control of patterns and access to both audio channels.

The general flow of this code is:
__________
Setup
Wait for Set # of Clients to Connect
__________
Periodically Check if Clients are Connected.
If Clients Lost, Stop and Wait
__________
Read Analog Signals from Spectrum Shield
Check for Inputs from Controls
Send Data to Clients
__________
Update Display with Current Status when waiting or if inputs received.
*/

//USER INPUTS HERE///////////////
#define NumClients 4  //This is the number of RaveTowersSlaves. This must be correct.


//WI-FI SETUP/////////////////////
#include <ESP8266WiFi.h> // Include the Wi-Fi library
#include <WiFiUDP.h>   
const char *ssid     = "RaveTowerTime";         // The SSID (name) of the Wi-Fi network you want to connect to
const char *password = "6026971660";     // The password of the Wi-Fi network (blank between quotes if it's open).

WiFiUDP UDP; // this creates an instance of the WiFIUDP class to send and receive. UDP is "User Datagram Protocol", which is low latency and tolerant of losses. 
//However: this type of connection DEPENDS on the destination (i.e. IP address) being correct, or it won't work.

bool Pings[NumClients]; //This is an array of true/falses for checking if clients are connected.
const uint8_t CheckTime = 5000; //Interval to check to see if a ping was received.
uint32_t LastReceived;

struct ClientPings {
  uint8_t ClientID;
  };
struct ClientPings Ping;


//PIN ASSIGNMENTS////////////////////
const uint8_t strobe = D3; //4 on the Spectrum Shield
const uint8_t reset = D4; //5 on the Spectrum Shield
const uint8_t audio = A0; //The ESP8266's only Analog Input. This means you only get the left or the right channel of audio.
const uint8_t ModePin = D5;
const uint8_t FuncPin = D0;
const uint8_t ColorPin = D6;

///BUTTON VARIABLES//////////////
uint32_t ModeLastChecked;
uint32_t ModeChecked;
bool ModeClicked = false;
bool ModeTrigger;
const uint8_t NumModes = 6; //THIS IS PLUGGED IN DOWN BELOW, and must be correct.
uint8_t CurrentMode;

uint32_t FuncLastChecked;
uint32_t FuncChecked;
bool FuncClicked = false;
bool FuncTrigger;
const uint8_t NumFuncs = 8; //THIS IS PLUGGED IN DOWN BELOW, and must be correct.
uint8_t CurrentFunc;

uint32_t ColorLastChecked;
uint32_t ColorChecked;
bool ColorClicked = false;
bool ColorTrigger;
const uint8_t NumColors = 4; //THIS IS PLUGGED IN DOWN BELOW, and must be correct.
uint8_t CurrentColor;

const uint8_t DoubleTapDelay = 20;
bool DoubleTapped;


//OLED SCREEN////////////////////////
#include <Wire.h>
#include <Adafruit_GFX.h>  //Include the library for the OLED screen
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
//on ESP8266s: SCL is D1, SDA is D2
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

uint8_t ConnectionStatus;
const char* ConnectionStates[] = {"Waiting...","Sending!"};

const char* ModeNames1[] = {"VU Meter","Flames","Confetti","Strobe","Strobe","Lamp"}; //the length of these arrays must match the number of modes.
const char* ModeNames2[] = {"(Mirror)","(Mirror)"," ","(Small)","(Full)"," "};

const char* FuncNames1[] = {"63 Hz","160 Hz","400 Hz","1 kHz","2.5 kHz","6.25 kHz","16 kHz","Random"}; //the length of these arrays must match the number of functions.
const char* FuncNames2[] = {"(Bass)","(Bass)","(Mids)","(Mids)","(Mids)","(Highs)","(Highs)"," "};
const char* StrobeSpeedNames[] = {"60ms","70ms","80ms","90ms","100ms","110ms","120ms","130ms"}; 

const char* ColorNames1[] = {"Normal","Vaporwave","Outrun","Sunset"}; //the length of these arrays must match the number of colors.
const char* ColorNames2[] = {" "," "," "," "};
const char* FlameNames1[] = {"Normal","Icy Hot","Pink","Green"}; 
const char* FlameNames2[] = {" "," ","Flash","Magic"};

//////////////////////////////////////////////////////

uint8_t freq[10]; //This array is populated with the audio data from the Spectrum Shield, and the three commands. This is what is sent over WiFi.

uint8_t i;

//////////////////////////////////////////////////////

void setup() {
  delay(500);
  display.begin(SSD1306_SWITCHCAPVCC, 0X3C); //initialize the OLED screen
  display.clearDisplay();
  display.setTextSize(3); 
  display.setTextColor(WHITE);
  display.setCursor(23, 7);
  display.println("LET'S");
  display.setCursor(30, 32);
  display.println("RAVE");
  display.display(); 
  delay(2000);
  display.clearDisplay();

  pinMode(audio, INPUT);
  pinMode(ModePin, INPUT);
  pinMode(FuncPin, INPUT);
  pinMode(ColorPin, INPUT);
  
  pinMode(strobe, OUTPUT);
  pinMode(reset, OUTPUT);
  digitalWrite(reset, LOW);
  digitalWrite(strobe, HIGH);

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password); //Start the access point.
  UDP.begin(123);  //this instructs the ESP to start listening for UDP packets on port 123.
  
  //I include the following lines after every clearDisplay//
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Access Point");
  display.print("\"");
  display.print(ssid);
  display.println("\"");
  display.println("started");
  display.println(" ");
  display.println("IP Address:");
  display.println(WiFi.softAPIP());
  display.display();
  display.clearDisplay();
  delay(2000);
  
  UpdateDisplay();
  ResetPings();  
  WaitForClients(); //This will force the program to wait for connections instead of screaming UDP Packets into the void.
  LastReceived = millis();
}

//WI-FI MANAGEMENT///////////////////////////////////////////////////
void ResetPings() {			//Resets the pings so the program has to receive new pings to continue.
    for (int i = 0; i < NumClients; i++) {
    Pings[i] = false;
    }
}

void WaitForClients() { 
  while(true) { //This infinite loop starts you waiting for packets, then starts you waiting for all clients. Could be a long time if Wifi is bad.
      ReadPings();
    if (CheckPings()) {
        ConnectionStatus = 1;
        UpdateDisplay();
        return; //this allows you to escape the infinite loop if CheckPings() returns TRUE.
      }
   delay(CheckTime);
      ResetPings(); //This forces all clients to try again if one client is taking too long.
  }
}

void ReadPings() {
 
 while(true) {   
  int packetSize = UDP.parsePacket();
  if (!packetSize) {
    break;
  }
  UDP.read((char*)&Ping, sizeof(struct ClientPings));  
  if (Ping.ClientID > NumClients) {
    display.println("Error: invalid client_id received");
    display.display();
    continue;
  }
  Pings[Ping.ClientID - 1] = true;
 }
}

bool CheckPings() { //This returns a true/false result for the "Wait for Clients" loop. 
  for (int i = 0; i < NumClients; i++) {
   if (!Pings[i]) {
    return false;
   }
  }
  ResetPings();
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////

void loop() {
  ReadSpectrum(); 	//Get audio data.
  ReadControls(); 	//If buttons have been pressed, update commands.
  SendData(); 		//Send data to clients.
    
  if (millis() - LastReceived > CheckTime) { //Every check interval, Read Pings. 
    ReadPings(); 
  if(!CheckPings()) { //If someone drops out, stop and wait.
      ConnectionStatus = 0;
      UpdateDisplay();
      WaitForClients(); 
    }
    LastReceived =  millis();
  }
  
  }

///////////////////////////////////////////////////////////////////////////////////////

void ReadSpectrum() {
    
  digitalWrite(reset, HIGH);
  digitalWrite(reset, LOW);
  for(i=0; i <7; i++)
  {
    digitalWrite(strobe, LOW); // 
    delayMicroseconds(30); // 
    freq[i] = map(analogRead(audio),0,1023,0,255); //This populates the freq array with the 7 frequencies read by the Spectrum Shield.
    digitalWrite(strobe, HIGH); 
  }
}

///////////////////////////////////////////////////////////////////////////////////////

void ReadControls() {

//////MODE PIN FIRST: freq[7]
  
  i = digitalRead(ModePin);
  if (i == 0) {
    if (millis() - ModeChecked < DoubleTapDelay && ModeClicked == false ) {
      DoubleTapped = true;
    }
    ModeTrigger = true;
    ModeClicked = true; 
    ModeChecked = millis();
  }

  else if (i == 1) {
    if (millis() - ModeChecked > DoubleTapDelay && ModeTrigger) {
      if (!DoubleTapped) {
        CurrentMode++;      //Here the 7th byte in the freq array is the Mode. This will talk to the "switch" function on the receiving end.
        CurrentColor = 0;   //Resets the color to "normal" when a new mode is selected.
        if (CurrentMode > NumModes-1) {
          CurrentMode = 0;  //loop around to 0 if you go too high.
        }
    UpdateDisplay();
    freq[7] = CurrentMode;
    freq[9] = CurrentColor;
    }
      ModeTrigger = false;
      DoubleTapped = false;
    }
    ModeClicked = false;
  }

/////////FUNCTION PIN SECOND: freq[8]

  i = digitalRead(FuncPin);
  if (i == 0) {
    if (millis() - FuncChecked < DoubleTapDelay && FuncClicked == false ) {
      DoubleTapped = true;
    }
    FuncTrigger = true;
    FuncClicked = true; 
    FuncChecked = millis();
  }

  else if (i == 1) {
    if (millis() - FuncChecked > DoubleTapDelay && FuncTrigger) {
      if (!DoubleTapped) {
        CurrentFunc++;      //Here the 8th byte in the freq array is the Function. This will select the frequency band, or the ms delay for the strobes.
        if (CurrentFunc > NumFuncs-1) {
          CurrentFunc = 0; //loop around to 0 if you go too high.
        }
    UpdateDisplay();
    freq[8] = CurrentFunc;
    }
      FuncTrigger = false;
      DoubleTapped = false;
    }
    FuncClicked = false;
  }

/////////COLOR PIN THIRD: freq[9]

  i = digitalRead(ColorPin);
  if (i == 0) {
    if (millis() - ColorChecked < DoubleTapDelay && ColorClicked == false ) {
      DoubleTapped = true;
    }
    ColorTrigger = true;
    ColorClicked = true; 
    ColorChecked = millis();
  }

  else if (i == 1) {
    if (millis() - ColorChecked > DoubleTapDelay && ColorTrigger) {
      if (!DoubleTapped) {
        CurrentColor++;      //Here the 9th byte in the freq array is the Color. This toggles colors in different patterns.
        if (CurrentColor > NumColors-1) {
          CurrentColor = 0; //loop around to 0 if you go too high.
        }
    UpdateDisplay();
    freq[9] = CurrentColor;
    }
      ColorTrigger = false;
      DoubleTapped = false;
    }
    ColorClicked = false;
  }

}

void SendData() {

  for (i = 0; i<NumClients ; i++) {
  IPAddress ip(192,168,4,2+i); //this identifies which IP address you're sending this packet to. All new connections to an access point connect sequentially.
  UDP.beginPacket(ip, 124); //this targets the IP Address, and the port.
  UDP.write((char*)&freq,sizeof(freq)); //the first item is what is sent (buffer), and the second item is the size of the packet
  UDP.endPacket();
  delayMicroseconds(1000/NumClients);
  }
}

//UPDATE DISPLAY//

void UpdateDisplay() {  //This should only update when a button is pressed, or connection is lost.
    display.clearDisplay();
    display.setTextSize(1); 
    display.setTextColor(WHITE);
    display.setCursor(0, 12);
    display.setTextSize(2);
    display.println("Mode:");
    display.println("Freq:");
    display.println("Color:");
    display.setCursor(60, 11);
    display.setTextSize(1);
    display.println(ModeNames1[CurrentMode]);
    display.setCursor(60, 20);
    display.println(ModeNames2[CurrentMode]);
    if(CurrentMode == 3 || CurrentMode == 5) { //These if and elses show N/A if speed doesn't matter (it technically does on Mode 3, but it's difficult to distinguish).
    display.setCursor(60, 28);
    display.println("  N/A ");
    display.setCursor(60, 36);
    display.println(' ');
    }
    else if (CurrentMode == 4) {	//On full strobe, we have certain ms delays, so show those instead of the audio frequency.
    display.setCursor(60, 28);
    display.println(StrobeSpeedNames[CurrentFunc]);
    display.setCursor(60, 36);
    display.println(' ');  
    }
    else {
    display.setCursor(60, 28);
    display.println(FuncNames1[CurrentFunc]);
    display.setCursor(60, 36);
    display.println(FuncNames2[CurrentFunc]);
    }
    if(CurrentMode == 1) {
    display.setCursor(70, 44);
    display.println(FlameNames1[0]);
    display.setCursor(70, 52);
    display.println(FlameNames2[0]);
    }
    else {
    display.setCursor(70, 44);
    display.println(ColorNames1[CurrentColor]);
    display.setCursor(70, 52);
    display.println(ColorNames2[CurrentColor]);
    }
    display.setCursor(0, 0);
    display.print(ConnectionStates[ConnectionStatus]);
    display.setCursor(70, 0);
    display.print("Towers: ");
    display.print(NumClients);
    display.display();
}
