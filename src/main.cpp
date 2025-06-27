#include <Arduino.h>
#include "midi.h"
#include "fastExpo.h"
#include <SPI.h>

#define LOW_NOTE 36 //C2 MIDI note

MIDI midi;
void noteOnHandler(uint8_t note, uint8_t vel);
void noteOffHandler(uint8_t note, uint8_t vel);
void midiCcHandler(uint8_t cc, uint8_t val);

void setNote(uint8_t note);
void stepLFO();
void stepGlissando();

uint8_t pressedKeys[48]; //hold all currently pressed notes
//allows us to fall back to previously pressed note after release
uint8_t keyCount = 0;

//PORTC
//0-3 note select
#define MUX0_CS 4
#define MUX1_CS 5
//PORTB
#define OCT_OUT 0
#define OCT_IN 1    //original octave switch
#define GATE_IN 2   //original gate signal
//PORTD
#define LFO_OUT 3

//lfo variables----------------------------------
uint8_t tick = 0; //timing flag
int32_t lfoPhase = 0; //phase accumulator for NCO
uint16_t lfoRate = expoConvertInt(600);
#define LFOMAX ((long)1 << 18) - 1 //max speed 31.25Hz
uint8_t lfoAmt = 0; //vibrato amount

uint32_t glisProgress = 0;
uint16_t glisRate = 0; //10000 - 100 range
uint8_t currentNote = 0;

uint16_t touchPadOld = 0;

ISR(TIMER1_COMPA_vect) { tick = 1; }

void setup() {
    DDRC = 0b111111; //all outputs
    PORTC = 0xff; //turn off muxes

    DDRB = 0b111101;
    PORTB |= (1<< OCT_IN); //pullup resistor for switch

    Serial.begin(31250); //MIDI baud

    midi.noteOn = noteOnHandler;
    midi.noteOff = noteOffHandler;
    midi.controlChange = midiCcHandler;

    //analogWrite(LFO_OUT, 126); //init LFO

    //setup tick timer used by glissando
    noInterrupts();
    TCCR1A = TCCR1B = TCNT1 = 0;
    TCCR1B = _BV(CS11) | _BV(CS10) | _BV(WGM12); //div 64 16MHz/64=250kHz
    OCR1A = (250000/4000) - 1; //4kHz tick
    bitSet(TIMSK1, OCIE1A);

    //speed up PWM timer ~8kHz
    TCCR2A = _BV(COM2A1) | _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);
    TCCR2B = _BV(CS21);
    interrupts();

    //init note array with blank value (255)
    for (uint8_t i = 48; i; i--) pressedKeys[i] = 255;

    //TTP229 touchpad
    SPI.begin();
    SPI.beginTransaction(SPISettings(14000000, MSBFIRST, SPI_MODE3));
    delay(500);
}

void loop() {
    if (Serial.available())
        midi.handleByte(Serial.read());

    //if original keyboard is playing, respect setting of octave switch
    uint8_t gate = PINB & (1 << GATE_IN);
    if (keyCount == 0 && gate) {
        uint8_t octSwitch = PINB & (1 << OCT_IN);
        (octSwitch) ? bitSet(PORTB, OCT_OUT) : bitClear(PORTB, OCT_OUT);

        OCR2B = 0; //set velocity output
    }

    if (tick) {
        tick = 0;
        stepGlissando();
        stepLFO();
        OCR2A = ((lfoPhase * lfoAmt) >> (11 + 7)) + 128; //output LFO

        //check touchpad
//        uint16_t touchPad = ~SPI.transfer16(0);
//        uint16_t diff = touchPad ^ touchPadOld; //difference of touchpads
//
//        if (diff) {
//            touchPadOld = touchPad;
//
//            for (int i = 0; i < 16; i++) {
//                uint8_t note = i + LOW_NOTE + ((i >= 8) ? 24 : 0);
//                if (diff & 1)
//                    (touchPad & 1) ? noteOnHandler(note, 64) : noteOffHandler(note, 0);
//
//                diff >>= 1;
//                touchPad >>= 1;
//            }
//        }
    }
}

void midiCcHandler(uint8_t cc, uint8_t val) {
    switch (cc) {
        case 1: //Modulation Wheel
            lfoAmt = val;
            break;
        case 5: //Portamento Time
            glisRate = val; //set glissando time
            if (val) glisRate = 51200 / val; //12,700 - 127
            break;
        case 16: //LFO rate
            lfoRate = expoConvertInt(val * (1024 / 128));
            break;
        case 65: //Portamento
            break;
        case 123: //All Notes Off
            PORTC |= (1 << MUX1_CS) | (1 << MUX0_CS); //turn off muxes
            keyCount = 0;

            //init note array with blank value (255)
            for (uint8_t i = 48; i; i--) pressedKeys[i] = 255;
            break;
    }
}

//find item in array
uint8_t *find(uint8_t *first, uint8_t *last, uint8_t value) {
    for (; first != last; first++) if (*first == value) return first;

    return last;
}

void stepGlissando() {
    if (keyCount == 0) return; //only progress glissando if a key is held

    glisProgress += glisRate;
    uint16_t noteStep = glisProgress >> 16;
    uint8_t targetNote = pressedKeys[keyCount - 1];

    if (!noteStep) return; //didn't progress enough to change note

    if (currentNote == targetNote) { //burn this step since we're already on the right note
        glisProgress -= (uint32_t) noteStep << 16; //otherwise it'll be applied on next key change
    } else {
        //snap to note if we're going to overshoot it
        if (noteStep > abs(targetNote - currentNote))
            currentNote = targetNote;
        else //move in the direction of the target note
            (targetNote > currentNote) ? currentNote += noteStep : currentNote -= noteStep;

        setNote(currentNote);
        glisProgress -= (uint32_t) noteStep << 16; //keep just the decimal portion
    }
}

void stepLFO() {
    static bool lfoDir = false;

    if (lfoDir) {
        lfoPhase -= lfoRate;
        if (lfoPhase <= -(long int) LFOMAX) {
            lfoPhase = -LFOMAX - (lfoPhase + LFOMAX);
            lfoDir = !lfoDir; //change direction
        }
    } else {
        lfoPhase += lfoRate;
        if (lfoPhase >= LFOMAX) {
            lfoPhase = LFOMAX - (lfoPhase - LFOMAX);
            lfoDir = !lfoDir; //change direction
        }
    }
}

void setNote(uint8_t note) {
    //set octave switch
    if (note >= 24) {
        bitSet(PORTB, OCT_OUT);
        note -= 24;
    } else bitClear(PORTB, OCT_OUT);

    if (note < 16) { //lower mux
        PORTC = note | (1 << MUX1_CS); //negative logic for CS
    } else { //higher mux
        PORTC = (note - 16) | (1 << MUX0_CS);
    }
}

void noteOnHandler(uint8_t note, uint8_t vel) {
    if (note < LOW_NOTE || note > (LOW_NOTE + 48)) return; //invalid note

    note -= LOW_NOTE;
    OCR2B = vel << 1; //set velocity output

    if (glisRate == 0) {
        setNote(note); //jump right to note if no glissando
        currentNote = note;
    } else setNote(currentNote);

    //put key into array if it's not there already
    uint8_t * arrEnd = pressedKeys + keyCount + 1;
    if (find(pressedKeys, arrEnd, note) == arrEnd)
        pressedKeys[keyCount++] = note;
}

void noteOffHandler(uint8_t note, uint8_t vel) {
    if (note < LOW_NOTE || note > (LOW_NOTE + 48)) return; //invalid note

    note -= LOW_NOTE;

    //remove key from array if it's there
    uint8_t * arrEnd = pressedKeys + keyCount + 1;
    uint8_t * keyPtr = find(pressedKeys, arrEnd, note);
    if (keyPtr != arrEnd) { //found
        for (; keyPtr < arrEnd; keyPtr++) //fill in hole
            *keyPtr = *(keyPtr + 1);

        keyCount--;
    }

    //TODO: add option to use lowest note?
    if (keyCount > 0) //fallback to last note
        setNote(pressedKeys[keyCount - 1]);
    else //disable muxes (negative logic for CS)
        PORTC |= (1 << MUX1_CS) | (1 << MUX0_CS);
}