/*******************************************************************************

  Bare Conductive Pi Cap
  ----------------------

  midi.cpp - sends MIDI messages for Fluidsynth via stdout output

  Written for Raspberry Pi.

  Bare Conductive code written by Stefan Dzisiewski-Smith.

  This work is licensed under a MIT license https://opensource.org/licenses/MIT
  
  Copyright (c) 2016, Bare Conductive
  
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
  
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

 *******************************************************************************/

#include <MPR121.h>
#include <signal.h>
#include <iostream>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <wiringPi.h>
#include <unistd.h>
#include <string>
#include <time.h>

using namespace std;

#define NUM_ELECTRODES 12
#define RED_LED_PIN 22
#define GREEN_LED_PIN 21
#define BLUE_LED_PIN 25

#define BUTTON_PIN 7 // this is wiringPi pin 7, which just happens to be physical pin 7 too
#define DEBOUNCE_LOCKOUT_MS    10
#define DOUBLEPRESS_TIMEOUT_US 300000
#define LONGPRESS_TIMEOUT_US   750000

#define VOLUME_INSTRUMENT 1
#define PERCUSSION 2
#define VOLUME_INSTRUMENT_DELAY 50;
#define PERCUSSION_DELAY 100;

// enums and variables for state and timeout action
enum state_t {IDLE, PRESSED, RELEASED};
state_t volatile state = IDLE;
enum action_t {NONE, SINGLE_PRESS, LONG_PRESS};
action_t volatile action = NONE;
bool volatile isrEnabled = true;
bool volatile buttonFlag = false;


string doublePressCommand = "sync && reboot now &";
string longPressCommand = "sync && halt &";

 
int zlimit = 150; //sensitvity value for midi conversion. Bigger number less sensitivity.
int sensorNo = 11; 
int updateDelay = VOLUME_INSTRUMENT_DELAY;
bool sound = false;
int prvMidi = 0;
unsigned int instrumentNo = 0;
int PROG = 1;
int T_PROG = 2;
int elecTouch[12];
bool touched[12];
int a;

bool volatile keepRunning = true;

//forward defined fucntions for button press and ctrl+c
void singlePress();
void doublePress();
void longPress();
void alarmHandler(int dummy);
void buttonIsr(void);
void buttonPress(void);
void intHandler(int dummy);


void led(int r, int g, int b) {
  // we are inverting the values, because the LED is active LOW
  // LOW - on
  // HIGH - off
  digitalWrite(RED_LED_PIN, !r);
  digitalWrite(GREEN_LED_PIN, !g);
  digitalWrite(BLUE_LED_PIN, !b);
}

void allNotesOff(void)
{
cout << "cc 0 123 0" << endl;
sound = false;
}



int midiSort(int baseline, int filtered)
{
  int i = baseline - filtered;  
  
  //reverse values
  i = zlimit - i;
  
  //scale to midi 127
  i = i*127/zlimit;

  if(i>127) i = 127; //keep with in midi limit
  else if(i<10) i = 10; //make sure there is still sound
    
  return i;
}

void toggleSound(void)
{
  if(sound)
  { 
  cout << "noteoff 0 " << 60 << endl; //turn sound off  
  led(1, 0, 0);
  }
  else
  {
  cout << "noteon 0 " << 60 << " 100" << endl; //turn sound on
  led(0, 1, 0);
  }
  
  sound = !sound;
}


int main(void) {
  // register our interrupt handler for the Ctrl+C signal
  signal(SIGINT, intHandler);
  
  // register our interrupt handler for button press
  signal(SIGALRM, alarmHandler);
  wiringPiSetup();
  
  // button pin is input, pulled up, linked to a dual-edge interrupt
  pinMode(BUTTON_PIN, INPUT);
  pullUpDnControl(BUTTON_PIN, PUD_UP);
  wiringPiISR(BUTTON_PIN, INT_EDGE_BOTH, buttonIsr);

  // setup MPR121 address on the Pi Cap
  if (!MPR121.begin(0x5C)) {
    cout << "error setting up MPR121: ";

    switch (MPR121.getError()) {
      case NO_ERROR:
        cout << "no error" << endl;
        break;
      case ADDRESS_UNKNOWN:
        cout << "incorrect address" << endl;
        break;
      case READBACK_FAIL:
        cout << "readback failure" << endl;
        break;
      case OVERCURRENT_FLAG:
        cout << "overcurrent on REXT pin" << endl;
        break;
      case OUT_OF_RANGE:
        cout << "electrode out of range" << endl;
        break;
      case NOT_INITED:
        cout << "not initialised" << endl;
        break;
      default:
        cout << "unknown error" << endl;
        break;
    }

    exit(1);
  }

  //next 4 settings are just for touching pin 0 to start and stop sounds
  // this is the touch threshold - setting it low makes it more like a proximity trigger
  int touchThreshold = 40;
  
  // this is the release threshold - must ALWAYS be smaller than the touch threshold
  int releaseThreshold = 20;
  
  MPR121.setTouchThreshold(touchThreshold);
  MPR121.setReleaseThreshold(releaseThreshold);
  
  
  // set up LED
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  
  //tell the world we are awake
  delay(2000); //wait for the synth to wake up
  led(1, 0, 0);
  
   singlePress(); //set up percussion to start
  
 
  
  while (keepRunning) {
    
    if (buttonFlag) buttonPress();
    
       if (MPR121.touchStatusChanged()) {
      MPR121.updateTouchData();
    }

    MPR121.updateBaselineData();
    MPR121.updateFilteredData();
    
    //simple over touch calculator
    for (int i = 0; i < NUM_ELECTRODES; i++) {
      elecTouch[i]+=10;
      if(elecTouch[i]>20000) elecTouch[i]=500; //just incase we never reset
    }
    
    switch(PROG)
    {
      case VOLUME_INSTRUMENT:
 
        if(MPR121.getTouchData(0) && elecTouch[0]>updateDelay)
        {
          toggleSound();
          elecTouch[0] = 0;
        }
    
        if(MPR121.getTouchData(3) && elecTouch[0]>updateDelay)
        {
          toggleSound();
          elecTouch[3] = 0;
        }
        
        if(sound)
        {
        int midi = midiSort(MPR121.getBaselineData(sensorNo), MPR121.getFilteredData(sensorNo));
        if(midi!=prvMidi)
          {
          cout << "cc 0 11 ";
          cout << midi << endl;
          prvMidi = midi; 
          }
        }
      break;
      
      case PERCUSSION:
      
      for (int a = 0; a < NUM_ELECTRODES; a++) {
        if (elecTouch[a]>updateDelay)
        {
          if(touched[a] == false && MPR121.getFilteredData(a)<500)
          {
            touched[a] = true;
            cout << "noteon 9 " << 71-a*3 << " 100" << endl;
          }
          else if (touched[a] == true && MPR121.getFilteredData(a)>500)
          {
            touched[a] = false;         
            cout << "noteoff 9 " << 71-a*3 << " 100" << endl;
          }
        }
      }
      break;
      
    }
    
    delay(10);
    
  }

  // make sure we return gracefully
  return(0);
}

/*--------- The button Press commands & Ctr+C -------*/
void singlePress() {
  // single press event handler
  allNotesOff();
  PROG++;
  if(PROG>T_PROG) PROG = 1;
  
  switch(PROG)
  {
    case VOLUME_INSTRUMENT:
    updateDelay = VOLUME_INSTRUMENT_DELAY;
    cout << "prog 0 50" << endl;
    break;
    
    case PERCUSSION:
    updateDelay = PERCUSSION_DELAY;
    break;
  }
  
}

void doublePress() {
  // double press event handler
  system(doublePressCommand.c_str());
}

void longPress() {
  // long press event handler
  system(longPressCommand.c_str());
}

void alarmHandler(int dummy) {
  // time-based part of state machine
  switch (action) {
    case NONE:
      break;
    case SINGLE_PRESS:
      singlePress(); // call the single press event handler
      action = NONE;
      state = IDLE;
      break;
    case LONG_PRESS:
      longPress(); // call the long press event handler
      action = NONE;
      state = IDLE;
      break;
    default:
      break;
  }
}

void buttonIsr(void) {
  // event based part of state machine
  if(isrEnabled) buttonFlag = true; // set the ISR flag, but only if our soft-gate is enabled
}



void buttonPress(void)
{
        if (!digitalRead(BUTTON_PIN)) {
        // button just pressed
        led(0, 0, 0);
        delay(100);
        led(1,0,0);
        
        switch (state) {
          case IDLE:
            // disable the button ISR, set state to pressed and set long press timeout
            isrEnabled = false;
            state = PRESSED;
            action = LONG_PRESS; // what we'll do if we time out in this state...
            ualarm(LONGPRESS_TIMEOUT_US,0);
            // delay a bit to avoid erroneous double-presses from switch bounce
            usleep(DEBOUNCE_LOCKOUT_MS);
            // re-enable the ISR once we're clear of switch bounce
            isrEnabled = true;
            break;
          case RELEASED:
            // if we get another press when the switch has been released (and before
            // the double-press timeout has occured) we have a double-press
            // so reset the state machine
            action = NONE;
            state = IDLE;
            doublePress(); // call the double press event handler
            break;
          default:
            break;
        }
      }
      else {
        // button just released
        switch (state) {
          case PRESSED:
            // disable the button ISR, set state to released and set double press timeout
            isrEnabled = false;
            action = SINGLE_PRESS; // what we'll do if we timeout in this state
            ualarm(DOUBLEPRESS_TIMEOUT_US,0);
            // delay a bit to avoid erroneous double-presses from switch bounce
            usleep(DEBOUNCE_LOCKOUT_MS);
            state = RELEASED;
            // re-enable the ISR once we're clear of switch bounce
            isrEnabled = true;
            break;
          default:
            break;
        }
      }

      buttonFlag = false;
}

// this allows us to exit the program via Ctrl+C while still exiting elegantly
void intHandler(int dummy) {
  keepRunning = false;
  exit(0);
}
