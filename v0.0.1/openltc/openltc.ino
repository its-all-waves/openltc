#include <avr/interrupt.h>
#include <avr/io.h>

#define FPS_2398 0
#define FPS_24   1
#define FPS_25   2
#define FPS_2997 3
#define FPS_30   4
#define FRAME_RATE FPS_2398 // set the frame rate

// The time values outputted as LTC
volatile unsigned char hourCount = 0;
volatile unsigned char minuteCount = 0;
volatile unsigned char secondCount = 0;
volatile unsigned char frameCount = 0;
volatile unsigned char bitCount = 0; // 80 bits make a frame per LTC spec

volatile unsigned char outputBit = 0; // the bit to be outputted as LTC at each bit period / FKA currentBit
volatile unsigned char updateCnt = 0; // 1 signals mid-bit period and thus the condition where we can output a 1 bit. toggles per interrupt.
volatile unsigned char lastLevel = 0; // ??? intermediary used to determine if output wave goes high or low

volatile unsigned char polarBit = 0; // ??? set to 0 at 0th bit of each frame
/* TODO: understand polarBit
    where it's used & what it's doing there:
        A) in setLevel() -> every bit, update_LTClevel_and_polarBit()
            polarBit is inverted when mid bit period && outputting zero
        B) in setLevel() -> LTC schema bit 0 / start of frame (1st instruction therein)
            polarBit is hard set to 0
            iow, polarBit starts as 0 for each LTC frame
        C) in setLevel() -> LTC schema flag bit 59 (1st instruction) (maybe only in 25 FPS)
            LTC bit to be outputted is set to polarBit (outputBit = polarBit)

    ???
        ?) what depends on polarBit?
            bit 0 of each frame depends on polarBit starting as 0
        ?) why is the outputBit set to polarBit in bit 59?
            does this make bit 59 always, or sometimes 1?
                either way, what does it mean?
        ?) what is the significance of bit 59?
            in 25 FPS?
            in others?
                advice from http://www.philrees.co.uk/articles/timecode.htm
                    readers should ingore bits 27, 43, 58 and 59
                    writers should set those bits to 0
 */

// helper fn headers
void update_LTClevel_and_polarBit();
unsigned short timer1_compareA_from(unsigned char frame_rate_id);

int main(void)
{
    DDRD = 0b00101000; // set port D's pins 3 and 5 to OUTPUT. audio connections: pin3 -> hot, pin 5 -> ground (seems to give a correctly-phased output)
    DDRB = 0b00110000; // set port B's pins 12 and 13 to OUTPUT. TODO: what are we doing with port B? seens unused in this version. probably used in RTC supporting version.

    /* register vs physical PWM output (fairly confident)
        OCR0A / OC0A  ->  portD, pin6  =  pin6  =  PWM
        OCR0B / OC0B  ->  portD, pin5  =  pin5  =  PWM

        OCR2A / OC2A  ->  portB, pin3  =  pin11 =  PWM
        OCR2B / OC2B  ->  portD, pin3  =  pin3  =  PWM

        OCR1A / OC1A  ->  portB, pin1  =  pin9  =  PWM

        photo looks like...
            audio pin3 = audio ground
            uno pin3  ->  audio pin2
            uno pin5  ->  audio ground
        
        TODO: update pin??? below
        correct phase according to Sound Devices 664
            uno pin3  ->  audio pin???
            uno pin5  ->  audio pin???
    */


    // 50% PWM "ground level": set timer0 for PWM, toggling output bt hi and lo (timer0 is 8-bit)
    TCCR0A = _BV(COM0A1) | _BV(COM0B1) | _BV(WGM01) | _BV(WGM00); // configure timer/counter 0 for fast PWM mode
    TCCR0B = _BV(WGM02) | _BV(CS00); // set the clock source for timer/counter 0 to be the system clock with no prescaling, so 16M ticks/sec
    OCR0A = 1; // set the compare match value for output compare unit A of timer/counter 0
    OCR0B = 0; // same, for output compare unit B. TODO: why does a timer need multiple compare units?

    // Set Output to Ground Level (timer2 is 8-bit)
    TCCR2A = _BV(COM2A1) | _BV(COM2B1) | _BV(WGM21) | _BV(WGM20); // configure timer/counter 2 for fast PWM mode
    TCCR2B = _BV(WGM22) | _BV(CS20); // set the clock source for timer/counter 2 to be the system clock with no prescaling
    OCR2A = 127; // set the compare match value for output compare unit A of timer/counter 2
    OCR2B = 63; // same, for output compare unit B

    // Per 1/2-Bit Interrupt
    TCCR1A = 0b00 << WGM10; // configure timer/counter 1 for CTC mode (clear timer on compare match)
    TCCR1B = 0b01 << WGM12 | 0b001 << CS10; // set the clock source for timer/counter 1 to be the system clock with no prescaling
    OCR1A = timer1_compareA_from(FRAME_RATE); // 3995; // (3999@25fps) set the compare match value for output compare unit A of timer/counter 1. timer1 is the main counter that determines either the frame rate or the rate of pulses. TODO: which is it?
    TIMSK1 = 1 << OCIE1A; // enable the output compare interrupt for output compare unit A of timer/counter 1
    sei(); // enable global interrupts

    // do nothing and wait for interrupts
    while (1) { }
}

/* computes the LTC bit to output and sets PWM (timer/clock) registers to output
that bit

    ltc schema:
        https://en.wikipedia.org/wiki/Linear_timecode#cite_note-BR.780-2-1
        > SMPTE linear timecode table footnotes */
void setLevel(void)
{
    switch (bitCount) {
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    frame number ones place (0-9) - 4 bits
        e.g. frame number 24
            0   1   2   3   <-- bitCount or bit # of ltc schema
            0   0   1   0   <-- 4 = frame number *ones place* (value at corresponding bit #)
            1   2   4   8   <-- weight of the corresponding value

    the process of reducing a frame, sec, or hour value to the bit we intend to
    transmit at the next bit period:
        given: frame count cycles bt 0 and FPS
        remember: LSB on LEFT, right shift (>>) = shift toward LSB

        (value % 10 >> place) & 1           <- ones place
        (value / 10 % 10 >> place) & 1      <- tens place
            value = frame, sec, or hour number
            place = the place in the binary(value) needed for the current LTC bit

        frame 24 -> binary(4) = 0 0 1 0

        BIT 0
            24 % 10 = 4 ->
            4 >> 0 = 4 ->
            4 & 1  ->  0 0 1 0
                    &  1 0 0 0
                    ----------
        BIT 0 = 0 ->   0 0 0 0
        ---------
        BIT 1
            24 % 10 = 4 ->
            4 >> 1  =  0010 >> 1  =  0100 = 2 ->
            2 & 1 = 0 = BIT 1
                    ---------
        BIT 2
            24 % 10 = 4 ->
            4 >> 2 = 1 ->
            1 & 1 = 1 = BIT 2
                    ---------
        BIT 3
            24 % 10 = 4 ->
            4 >> 3 = 0 ->
            0 & 1 = 0 = BIT 3
                    ---------

        Output as confirmed above: 0 0 1 0 (bit 2 = 1, LSB left)
    */
    case 0:
        polarBit = 0;
        outputBit = ((frameCount % 10) >> 0) & 1; // reduce the ones place to the bit value needed for *this bit position (bitCount)
        update_LTClevel_and_polarBit();
        break;
    case 1:
        outputBit = ((frameCount % 10) >> 1) & 1; // ...*this bit position
        update_LTClevel_and_polarBit();
        break;
    case 2:
        outputBit = ((frameCount % 10) >> 2) & 1; // ...*this...
        update_LTClevel_and_polarBit();
        break;
    case 3:
        outputBit = ((frameCount % 10) >> 3) & 1; // ...*this...
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 1 - 4 bits */
    case 4 ... 7:
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    frame number tens place (0-2) - 2 bits
        e.g. frame number 24
            8   9   <-- bitCount or bit # of ltc schema
            0   1   <-- 20 = frame number *tens place* (value at corresponding bit #)
            10  20  <-- weight  of the corresponding value
    */
    case 8:
        outputBit = ((frameCount / 10 % 10) >> 0) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 9:
        outputBit = ((frameCount / 10 % 10) >> 1) & 1;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    flags - 2 bits
        bit 10 - drop frame
        bit 11 - color frame */
    case 10 ... 11:
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 2 - 4 bits
    */
    case 12 ... 15:
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    seconds ones place (0-9) - 4 bits
        e.g. second 36
            16  17  18  19  <-- bitCount or bit # of ltc schema
            0   1   1   0   <-- 6 = seconds *ones place* (value at corresponding bit #)
            1   2   4   8   <-- weight of the corresponding value
    */
    case 16:
        outputBit = (secondCount % 10 >> 0) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 17:
        outputBit = (secondCount % 10 >> 1) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 18:
        outputBit = (secondCount % 10 >> 2) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 19:
        outputBit = (secondCount % 10 >> 3) & 1;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 3 - 4 bits */
    case 20 ... 23:
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    seconds tens place (0-9) - 3 bits
        e.g. second 36
            24  25  26  <-- bitCount or bit # of ltc schema
            1   1   0   <-- 30 = seconds *tens place* (value at corresponding bit #)
            10  20  40  <-- weight of the corresponding value
    */
    case 24:
        outputBit = ((secondCount / 10 % 10) >> 0) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 25:
        outputBit = ((secondCount / 10 % 10) >> 1) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 26:
        outputBit = ((secondCount / 10 % 10) >> 2) & 1;
        update_LTClevel_and_polarBit();
        break;
    /* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    flag *see note* */
    case 27:
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 4 - 4 bits */
    case 28 ... 31:
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    minutes ones place (0-9) - 4 bits
        e.g. minute 59
            32  33  34  35  <-- bitCount or bit # of ltc schema
            1   0   0   1   <-- 9 = minutes *ones place* (value at corresponding bit #)
            1   2   4   8   <-- weight of the corresponding value
    */
    case 32:
        outputBit = ((minuteCount % 10) >> 0) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 33:
        outputBit = ((minuteCount % 10) >> 1) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 34:
        outputBit = ((minuteCount % 10) >> 2) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 35:
        outputBit = ((minuteCount % 10) >> 3) & 1;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 5 - 4 bits */
    case 36 ... 39:
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    minutes tens place (0-9) - 3 bits
        e.g. minute 59
            24  25  26  <-- bitCount or bit # of ltc schema
            1   0   1   <-- 50 = seconds *tens place* (value at corresponding bit #)
            10  20  40  <-- weight of the corresponding value
    */
    case 40:
        outputBit = ((minuteCount / 10 % 10) >> 0) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 41:
        outputBit = ((minuteCount / 10 % 10) >> 1) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 42:
        outputBit = ((minuteCount / 10 % 10) >> 2) & 1;
        update_LTClevel_and_polarBit();
        break;
    /* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    flag *see note* */
    case 43:
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 6 - 4 bits */
    case 44 ... 47:
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    hours ones place (0-9) - 4 bits
        e.g. hour 15
            48  49  50  51  <-- bitCount or bit # of ltc schema
            1   0   1   0   <-- 5 = hours *ones place* (value at corresponding bit #)
            1   2   4   8   <-- weight of the corresponding value
    */
    case 48:
        outputBit = ((hourCount % 10) >> 0) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 49:
        outputBit = ((hourCount % 10) >> 1) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 50:
        outputBit = ((hourCount % 10) >> 2) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 51:
        outputBit = ((hourCount % 10) >> 3) & 1;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 7 - 4 bits */
    case 52 ... 55:
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    hours tens place (0-2) - 2 bits
        e.g. hour 15
            56  57   <-- bitCount or bit # of ltc schema
            1   0   <-- 10 = hours *tens place* (value at corresponding bit #)
            10  20  <-- weight  of the corresponding value
    */
    case 56:
        outputBit = ((hourCount / 10 % 10) >> 0) & 1;
        update_LTClevel_and_polarBit();
        break;
    case 57:
        outputBit = ((hourCount / 10 % 10) >> 1) & 1;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    clock flag (aka BGF1) */
    case 58:
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    /* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    flag *see note* */
    case 59:
        // TODO: why is polarBit used here?
        // outputBit = polarBit;
        // update_LTClevel_and_polarBit();
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 8 (final) - 4 bits */
    case 60 ... 63:
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    fixed pattern for sync - 16 bits (thru end of tc frame/schema)
        occurs at end of each frame of picture/timecode
        must be these 16 bits (assuming playback is forward, as usual):
            0  0  [ (12) x 1 ]  0  1  (reflected in the remaining outputBit values)
            prefix and suffix bit pairs are used to get direction of playback
            works bc 12 consecutive 1s cannot appear anywhere else in the schema
    */
    case 64 ... 65: // 2x 0
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    case 66 ... 77: // 12x 1
        outputBit = 1;
        update_LTClevel_and_polarBit();
        break;
    case 78: // 0
        outputBit = 0;
        update_LTClevel_and_polarBit();
        break;
    case 79: // 1
        outputBit = 1;
        update_LTClevel_and_polarBit();
        break;
    default:
        // if on no particular bit, set OCR2B to 63 (0.25 * full byte of 256)
        OCR2B = 63;
    }
}

/* updates bit, frame, second, minute, & hour counts. checks each unit from
smallest to largest, returning if a unit can be incremented. if on the last unit
of a count, reset the count and check the next largest unit/count. */
void timeUpdate(void)
{
    if (bitCount < 79) {
        bitCount++;
        return;
    }
    bitCount = 0;

    if (frameCount < 24) {
        frameCount++;
        return;
    }
    frameCount = 0;

    if (secondCount < 59) {
        secondCount++;
        return;
    }
    secondCount = 0;

    if (minuteCount < 59) {
        minuteCount++;
        return;
    }
    minuteCount = 0;

    if (hourCount < 23) {
        hourCount++;
        return;
    }
    hourCount = 0;
}

/* Interrupt Service Routine
    triggered everytime timer1 compareA match value reaches
 */
ISR(TIMER1_COMPA_vect)
{
    setLevel();

    if (updateCnt)
        timeUpdate();

    updateCnt = !updateCnt;
}

/* does the updates required at each LTC bit:
    1) sets the register that dictates the output wave level (hi or lo)
    2) flips polarBit when the time is right */
void update_LTClevel_and_polarBit()
{
    // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    // 1) update LTC output level

    /* invert lastLevel when A _or_ B is true:
        A) we're at the start of a bit period,
        B) we're mid-bit period & the LTC bit to be sent is a 1 */
    if (!updateCnt || (updateCnt && outputBit))
        lastLevel = !lastLevel;

    // if lastLevel, PWM outputs biphase/Manchester high, else, outputs low
    OCR2B = lastLevel
        ? 100
        : 27;

    // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    // 2) update polarBit

    /* invert polarBit when both A _and_ B are true
        A) we are at the middle of a bit period
        B) we intend to output a 0 for this LTC bit period */
    if (updateCnt & !outputBit)
        polarBit = !polarBit;
}

/* utility + debugging function - UNTESTED
returns a 16-bit timer compare match value (OCR1A) from a frame rate. when OCR1A
is matched by timer/counter1, the interrupt is generated, calling ISR(). 
    OCR refers to Output Compare Register
    FPS_ constants are #defined as integers 0 - 4
 */
unsigned short timer1_compareA_from(unsigned char frame_rate_id)
{
    if (frame_rate_id < 0 || frame_rate_id > 4)
        return;

    unsigned short frame_dur_us; // frame duration in microsec, avoids floats
    switch (frame_rate_id) {
    case FPS_2398: // 23.976
        frame_dur_us = 41708;
        break;
    case FPS_24:
        frame_dur_us = 41667;
        break;
    case FPS_25:
        frame_dur_us = 40000;
        break;
    case FPS_2997: // 29.97
        frame_dur_us = 33367;
        break;
    case FPS_30:
        frame_dur_us = 33333;
        break;
    default:
        break;
    }

    /* Atmega328p gets a 16MHz clock signal from the Uno. since no prescaler is
    used in the program, the timer/counter produces 16 million ticks per second,
    or one tick per clock cycle.

        (ticks_per_sec * frame_dur_sec) / interrupts_per_frame
    */
    double frame_dur_sec = frame_dur_us / 1E6;
    return round(16E6 * frame_dur_sec / 160.0) - 1;
}