#include <avr/interrupt.h>
#include <avr/io.h>

// #define DEBUG_OCR1A_TIME_FACTOR 4000 // 25 fps
#define FPS_23 0
#define FPS_24 1
#define FPS_25 2
#define FPS_29 3
#define FPS_30 4

volatile unsigned char hourCount = 0;
volatile unsigned char minuteCount = 0;
volatile unsigned char secondCount = 0;
volatile unsigned char frameCount = 0;

volatile unsigned char bitCount = 0;
volatile unsigned char updateCnt = 0;
volatile unsigned char currentBit = 0;

volatile unsigned char lastLevel = 0;
volatile unsigned char polarBit = 0;

void update_timer2_compareB();
void update_polarBit();
void write_0_bit();
void update_timer2_compareB_and_polarBit();
unsigned short timer1_compareA_from(unsigned char frame_rate_id);

/* main() comments curtesy of GPT 4 */
int main(void)
{
    DDRD = 0b00101000; // set port D's pins 3 and 5 to OUTPUT, rest to INPUT. audio connections: pin3 -> hot, pin 5 -> ground (seems to give a correctly-phased output)
    DDRB = 0b00110000; // set port B's pins 12 and 13 to OUTPUT, rest to INPUT. TODO: what are we doing with port B? seens unused in this version. probably used in RTC supporting version.

    // 50% PWM Ground Level
    TCCR0A = _BV(COM0A1) | _BV(COM0B1) | _BV(WGM01) | _BV(WGM00); // configure timer/counter 0 for fast PWM mode
    TCCR0B = _BV(WGM02) | _BV(CS00); // set the clock source for timer/counter 0 to be the system clock with no prescaling, so 16M ticks/sec
    OCR0A = 1; // set the compare match value for output compare unit A of timer/counter 0
    OCR0B = 0; // same, for output compare unit B. TODO: why does a timer need multiple compare units?

    // Set Output to Ground Level (timer2 is 8 bit, so max compare value is 255)
    TCCR2A = _BV(COM2A1) | _BV(COM2B1) | _BV(WGM21) | _BV(WGM20); // configure timer/counter 2 for fast PWM mode
    TCCR2B = _BV(WGM22) | _BV(CS20); // set the clock source for timer/counter 2 to be the system clock with no prescaling
    OCR2A = 127; // set the compare match value for output compare unit A of timer/counter 2
    OCR2B = 63; // same, for output compare unit B

    // Per 1/2-Bit Interrupt
    TCCR1A = 0b00 << WGM10; // configure timer/counter 1 for CTC mode (clear timer on compare match)
    TCCR1B = 0b01 << WGM12 | 0b001 << CS10; // set the clock source for timer/counter 1 to be the system clock with no prescaling
    OCR1A = timer1_compareA_from(FPS_25); // 3995; // (3999@25fps) set the compare match value for output compare unit A of timer/counter 1. timer1 is the main counter that determines either the frame rate or the rate of pulses. TODO: which is it?
    TIMSK1 = 1 << OCIE1A; // enable the output compare interrupt for output compare unit A of timer/counter 1
    sei(); // enable global interrupts

    while (1) {
        // do nothing and wait for interrupts
    }
}

/* Ian -- assuming this sets the level of the sorta-pwm output at each bit of
the ltc schema for info on the flags:
    https://en.wikipedia.org/wiki/Linear_timecode#cite_note-BR.780-2-1
        > SMPTE linear timecode table footnotes */
void setLevel(void)
{
    switch (bitCount) {
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    frame number ones place (0-9) - 4 bits
        e.g. frame number 24
            0   1   2   3   <-- `bitCount` or bit # of ltc schema
            0   0   1   0   <-- 4 = frame number *ones place* (value at corresponding bit #)
            1   2   4   8   <-- weight of the corresponding value


        how we get a binary # from a specific place in the frame count
            given: frame count cycles bt 0 and FPS
            remember: LSB on LEFT

            24 -> binary(4) = 0 0 1 0

            BIT 0
                24 % 10 = 4  ->
                4 >> 0 = 4 ->
                4 & 1  ->  0 0 1 0
                        &  1 0 0 0
                        ----------
            BIT 0 = 0 ->   0 0 0 0
            ---------
            BIT 1
                24 % 10 = 4  ->
                4 >> 1 = 0 0 1 0 >> 1 = 0 1 0 0 = 2 ->
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

            Output as confirmed above: 0 0 1 0 (1 on bit 2, LSB left)
    */
    case 0:
        polarBit = 0;
        currentBit = ((frameCount % 10) >> (1 - 1)) & 1; // reduce the ones place to the value at *this bit position (bitCount)
        update_timer2_compareB_and_polarBit();
        break;
    case 1:
        currentBit = ((frameCount % 10) >> (2 - 1)) & 1; // ...*this bit position
        update_timer2_compareB_and_polarBit();
        break;
    case 2:
        currentBit = ((frameCount % 10) >> (3 - 1)) & 1; // ...*this...
        update_timer2_compareB_and_polarBit();
        break;
    case 3:
        currentBit = ((frameCount % 10) >> (4 - 1)) & 1; // ...*this...
        update_timer2_compareB_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 1 - 4 bits */
    case 4 ... 7:
        write_0_bit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    frame number tens place (0-2) - 2 bits
        e.g. frame number 24
            8   9   <-- `bitCount` or bit # of ltc schema
            0   1   <-- 20 = frame number *tens place* (value at corresponding bit #)
            10  20  <-- weight  of the corresponding value
    */
    case 8:
        currentBit = ((frameCount / 10 % 10) >> (1 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 9:
        currentBit = ((frameCount / 10 % 10) >> (2 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    flags - 2 bits
        bit 10 - drop frame
        bit 11 - color frame */
    case 10 ... 11:
        write_0_bit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 2 - 4 bits
    */
    case 12 ... 15:
        write_0_bit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    seconds ones place (0-9) - 4 bits
        e.g. second 36
            16  17  18  19  <-- `bitCount` or bit # of ltc schema
            0   1   1   0   <-- 6 = seconds *ones place* (value at corresponding bit #)
            1   2   4   8   <-- weight of the corresponding value
    */
    case 16:
        currentBit = (secondCount % 10 >> (1 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 17:
        currentBit = (secondCount % 10 >> (2 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 18:
        currentBit = (secondCount % 10 >> (3 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 19:
        currentBit = (secondCount % 10 >> (4 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 3 - 4 bits */
    case 20 ... 23:
        write_0_bit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    seconds tens place (0-9) - 3 bits
        e.g. second 36
            24  25  26  <-- `bitCount` or bit # of ltc schema
            1   1   0   <-- 30 = seconds *tens place* (value at corresponding bit #)
            10  20  40  <-- weight of the corresponding value
    */
    case 24:
        currentBit = ((secondCount / 10 % 10) >> (1 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 25:
        currentBit = ((secondCount / 10 % 10) >> (2 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 26:
        currentBit = ((secondCount / 10 % 10) >> (3 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    /* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    flag *see note* */
    case 27:
        write_0_bit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 4 - 4 bits */
    case 28 ... 31:
        write_0_bit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    minutes ones place (0-9) - 4 bits
        e.g. minute 59
            32  33  34  35  <-- `bitCount` or bit # of ltc schema
            1   0   0   1   <-- 9 = minutes *ones place* (value at corresponding bit #)
            1   2   4   8   <-- weight of the corresponding value
    */
    case 32:
        currentBit = ((minuteCount % 10) >> (1 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 33:
        currentBit = ((minuteCount % 10) >> (2 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 34:
        currentBit = ((minuteCount % 10) >> (3 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 35:
        currentBit = ((minuteCount % 10) >> (4 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 5 - 4 bits */
    case 36 ... 39:
        write_0_bit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    minutes tens place (0-9) - 3 bits
        e.g. minute 59
            24  25  26  <-- `bitCount` or bit # of ltc schema
            1   0   1   <-- 50 = seconds *tens place* (value at corresponding bit #)
            10  20  40  <-- weight of the corresponding value
    */
    case 40:
        currentBit = ((minuteCount / 10 % 10) >> (1 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 41:
        currentBit = ((minuteCount / 10 % 10) >> (2 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 42:
        currentBit = ((minuteCount / 10 % 10) >> (3 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    /* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    flag *see note* */
    case 43:
        write_0_bit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 6 - 4 bits */
    case 44 ... 47:
        write_0_bit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    hours ones place (0-9) - 4 bits
        e.g. hour 15
            48  49  50  51  <-- `bitCount` or bit # of ltc schema
            1   0   1   0   <-- 5 = hours *ones place* (value at corresponding bit #)
            1   2   4   8   <-- weight of the corresponding value
    */
    case 48:
        currentBit = ((hourCount % 10) >> (1 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 49:
        currentBit = ((hourCount % 10) >> (2 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 50:
        currentBit = ((hourCount % 10) >> (3 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 51:
        currentBit = ((hourCount % 10) >> (4 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 7 - 4 bits */
    case 52 ... 55:
        write_0_bit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    hours tens place (0-2) - 2 bits
        e.g. hour 15
            56  57   <-- `bitCount` or bit # of ltc schema
            1   0   <-- 10 = hours *tens place* (value at corresponding bit #)
            10  20  <-- weight  of the corresponding value
    */
    case 56:
        currentBit = ((hourCount / 10 % 10) >> (1 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 57:
        currentBit = ((hourCount / 10 % 10) >> (2 - 1)) & 1;
        update_timer2_compareB_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    clock flag (aka BGF1) */
    case 58:
        write_0_bit();
        break;
    /* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    flag *see note* */
    case 59:
        currentBit = (polarBit);
        update_timer2_compareB_and_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 8 (final) - 4 bits */
    case 60 ... 63:
        write_0_bit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    fixed pattern for sync - 16 bits (thru end of tc frame/schema)
        occurs at end of each frame of picture/timecode
        must be these 16 bits (assuming playback is forward, as usual):
            0  0  [ (12) x 1 ]  0  1  (reflected in the remaining `currentBit` values)
            prefix and suffix bit pairs are used to get direction of playback
            works bc 12 consecutive 1s cannot appear anywhere else in the schema
    */
    case 64 ... 65: // 2x 0
        write_0_bit();
        break;
    case 66 ... 77: // 12x 1
        currentBit = 1;
        update_timer2_compareB_and_polarBit();
        break;
    case 78: // 0
        write_0_bit();
        break;
    case 79: // 1
        currentBit = 1;
        update_timer2_compareB_and_polarBit();
        break;
    default:
        // if on no particular bit, set OCR2B to 63 (0.25 * full byte of 256)
        OCR2B = 63;
    }
}

/* keeps track of and updates bit, frame, second, minute, & hour counts. */
void timeUpdate(void)
{
    // ++ bit count and return, if not at end of a frame
    if (bitCount < 79) {
        bitCount++;
        return;
    }
    bitCount = 0;

    // ++ frame count and return, if not at end of a second
    if (frameCount < 24) {
        frameCount++;
        return;
    }
    frameCount = 0;

    // ++ second count and return, if not at end of a minute
    if (secondCount < 59) {
        secondCount++;
        return;
    }
    secondCount = 0;

    // ++ minute count and return, if not at end of a frame
    if (minuteCount < 59) {
        minuteCount++;
        return;
    }
    minuteCount = 0;

    // increment hour count and return, if not at end of a day (end of 24 hr cycle)
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

/* does the biphase / Machester encoding */
void update_timer2_compareB()
{
    /* invert lastLevel when A or B is true:
        A) we're at the start of a bit period,
        B) we're at the middle of a bit period & the LTC bit to be sent is a 1 */
    if (!updateCnt || (updateCnt && currentBit))
        lastLevel = !lastLevel;

    // if lastLevel, PWM outputs biphase/Manchester high, if 0, outputs low
    OCR2B = lastLevel
        ? 100
        : 27;
}

void update_polarBit()
{
    if (updateCnt & !currentBit)
        polarBit = !polarBit;
}

/* writes a zero bit
TODO: is all of this really related to writing a zero bit? */
void write_0_bit()
{
    currentBit = 0;
    update_timer2_compareB();
    update_polarBit();
}

/* does a chunk of the work for many cases. */
void update_timer2_compareB_and_polarBit()
{
    update_timer2_compareB();
    update_polarBit();
}

/* utility + debugging function - UNTESTED
returns a value for OCR1A from a frame rate. OCR1A is the data that determines
the duty cycle of the PWM output. OCR refers to Output Compare Register. It's
the value that, when matched by its corresponding counter value, triggers an
interrupt.
 */
unsigned short timer1_compareA_from(unsigned char frame_rate_id)
{
    if (frame_rate_id < 0 || frame_rate_id > 4)
        return;

    unsigned short frame_dur_us; // frame duration in microsec, avoids floats
    switch (frame_rate_id) {
    case 0: // 23.976
        frame_dur_us = 41708;
        break;
    case 1: // 24
        frame_dur_us = 41667;
        break;
    case 2: // 25
        frame_dur_us = 40000;
        break;
    case 3: // 29.97
        frame_dur_us = 33367;
        break;
    case 4: // 30
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

    // TODO: do I round or truncate here? neither fixed the rapid drift
    // TODO: is it possible that this function is causing the rapid drift, as it
    // seems a little worse than before? but I could be hallucinating a
    // difference. try just #define-ing the outputs of this function and see if
    // the drift appears to be less
    double frame_dur_sec = frame_dur_us / 1E6;
    return round(16E6 * frame_dur_sec / 160.0) - 1;
}