#include <avr/interrupt.h>
#include <avr/io.h>

volatile unsigned char hourCount = 0;
volatile unsigned char minuteCount = 0;
volatile unsigned char secondCount = 0;
volatile unsigned char frameCount = 0;

volatile unsigned char bitCount = 0;
volatile unsigned char updateCnt = 0;
volatile unsigned char currentBit = 0;

volatile unsigned char lastLevel = 0;
volatile unsigned char polarBit = 0;

// Ian's Headers
void update_OCR2B();
void update_polarBit();

int main(void)
{
    DDRD = 0b00101000;
    DDRB = 0b00110000;

    // 50% PWM Ground Level
    TCCR0A = _BV(COM0A1) | _BV(COM0B1) | _BV(WGM01) | _BV(WGM00);
    TCCR0B = _BV(WGM02) | _BV(CS00);
    OCR0A = 1;
    OCR0B = 0;

    // Set Output to Ground Level
    TCCR2A = _BV(COM2A1) | _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);
    TCCR2B = _BV(WGM22) | _BV(CS20);
    OCR2A = 127;
    OCR2B = 63;

    // Per 1/2-Bit Interrupt
    TCCR1A = 0b00 << WGM10;
    TCCR1B = 0b01 << WGM12 | 0b001 << CS10;
    OCR1A = 3995; // 3999@25fps
    TIMSK1 = 1 << OCIE1A;
    sei();

    while (1) {
    }
}

void setLevel(void)
{
    switch (bitCount) {
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    frame number ones place (0-9) - 4 bits   
        e.g. frame number 24
            0   1   2   3   <-- `bitCount` or bit # of ltc schema
            0   0   1   0   <-- 4 = frame number *ones place* (value at corresponding bit #)
            1   2   4   8   <-- weight of the corresponding value
    */  
    case 0:
        polarBit = 0;
        currentBit = ((frameCount % 10) >> (1 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    case 1:
        currentBit = (((frameCount % 10) >> (2 - 1)) & 1);
        update_OCR2B();
        update_polarBit();
        break;
    case 2:
        currentBit = (((frameCount % 10) >> (3 - 1)) & 1);
        update_OCR2B();
        update_polarBit();
        break;
    case 3:
        currentBit = (((frameCount % 10) >> (4 - 1)) & 1);
        update_OCR2B();
        update_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 1 - 4 bits */
    case 4 ... 7:
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    frame number tens place (0-2) - 2 bits
        e.g. frame number 24
            8   9   <-- `bitCount` or bit # of ltc schema
            0   1   <-- 20 = frame number *tens place* (value at corresponding bit #)
            10  20  <-- weight  of the corresponding value
    */  
    case 8:
        currentBit = (((frameCount / 10 % 10) >> (1 - 1)) & 1);
        update_OCR2B();
        update_polarBit();
        break;
    case 9:
        currentBit = (((frameCount / 10 % 10) >> (2 - 1)) & 1);
        update_OCR2B();
        update_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    flags - 2 bits
        bit 10 - drop frame
        bit 11 - color frame */
    case 10 ... 11:
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 2 - 4 bits
    */
    case 12 ... 15:
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
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
        update_OCR2B();
        update_polarBit();
        break;
    case 17:
        currentBit = (secondCount % 10 >> (2 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    case 18:
        currentBit = (secondCount % 10 >> (3 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    case 19:
        currentBit = (secondCount % 10 >> (4 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 3 - 4 bits */ 
    case 20 ... 23:
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
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
        update_OCR2B();
        update_polarBit();
        break;
    case 25:
        currentBit = ((secondCount / 10 % 10) >> (2 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    case 26:
        currentBit = ((secondCount / 10 % 10) >> (3 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    /* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    flag *see note* */
    case 27:
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 4 - 4 bits */  
    case 28 ... 31:
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
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
        update_OCR2B();
        update_polarBit();
        break;
    case 33:
        currentBit = ((minuteCount % 10) >> (2 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    case 34:
        currentBit = ((minuteCount % 10) >> (3 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    case 35:
        currentBit = ((minuteCount % 10) >> (4 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 5 - 4 bits */ 
    case 36 ... 39:
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
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
        update_OCR2B();
        update_polarBit();
        break;
    case 41:
        currentBit = ((minuteCount / 10 % 10) >> (2 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    case 42:
        currentBit = ((minuteCount / 10 % 10) >> (3 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    /* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    flag *see note* */
    case 43:
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 6 - 4 bits */
    case 44 ... 47:
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
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
        update_OCR2B();
        update_polarBit();
        break;
    case 49:
        currentBit = ((hourCount % 10) >> (2 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    case 50:
        currentBit = ((hourCount % 10) >> (3 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    case 51:
        currentBit = ((hourCount % 10) >> (4 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 7 - 4 bits */
    case 52 ... 55:
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
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
        update_OCR2B();
        update_polarBit();
        break;
    case 57:
        currentBit = ((hourCount / 10 % 10) >> (2 - 1)) & 1;
        update_OCR2B();
        update_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    clock flag (aka BGF1) */
    case 58:
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
        break;
    /* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    flag *see note* */
    case 59:
        currentBit = (polarBit);
        update_OCR2B();
        update_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    user bits field 8 (final) - 4 bits */
    case 60 ... 63:
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
        break;
    /*  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    fixed pattern for sync - 16 bits (thru end of tc frame/schema)
        occurs at end of each frame of picture/timecode
        must be these 16 bits (assuming playback is forward, as usual):
            0  0  [ (12) x 1 ]  0  1  (reflected in the remaining `currentBit` values)
            prefix and suffix bit pairs are used to get direction of playback
            works bc 12 consecutive 1s cannot appear anywhere else in the schema
    */    
    case 64 ... 65:         // 2x 0
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
        break;
    case 66 ... 77:         // 12x 1
        currentBit = 1;
        update_OCR2B();
        update_polarBit();
        break;
    case 78:                // 0
        currentBit = 0;
        update_OCR2B();
        update_polarBit();
        break;
    case 79:                // 1
        currentBit = 1;
        update_OCR2B();
        update_polarBit();
        break;
    default:
        OCR2B = 63;
    }
}

void timeUpdate(void)
{
    // return if we're at the end of a tc frame
    if (bitCount < 79) {
        bitCount++;
        return;
    }
    bitCount = 0; // reset

    // return if we're at the end of a second
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

ISR(TIMER1_COMPA_vect)
{
    setLevel();

    if (updateCnt == 0) {
        updateCnt++;
    } else {
        updateCnt = 0;
        timeUpdate();
    }
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// 23.07.23 by Ian

void update_OCR2B()
{
    if (updateCnt) {
        if (currentBit) 
            lastLevel = !lastLevel;
    } else {
        lastLevel = !lastLevel;
    }

    OCR2B = lastLevel 
        ? 100
        : 27; 
}

void update_polarBit()
{
    if (updateCnt & !currentBit)
        polarBit = !polarBit;
}