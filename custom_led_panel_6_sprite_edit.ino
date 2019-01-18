#include "font.h"
#include "sprites.h"
#include <string.h>

#define GREEN_BUTTON 52
#define RED_BUTTON   50
#define BLUE_BUTTON  48

#define DATA    PORTA
#define R1_PIN  24
#define G1_PIN  25
#define B1_PIN  26
#define R2_PIN  27
#define G2_PIN  28
#define B2_PIN  29
#define TOP_COLOR_OFFSET 2
#define BOTTOM_COLOR_OFFSET 5
// note: bsRead() and bsWrite() assume bits 0 and 1 are avalible

#define LINE PORTF // I reserve the entire port, wastefully
#define LINE_A   A0
#define LINE_B   A1
#define LINE_C   A2
#define LINE_D   A3 // for SCAN_LINES == 8 this is unused
// for SCAN_LINES > 16 you need more of these

// it's importent that this is OC1B
#define OE  12 // output enable

#define LAT 10 // latch
#define LAT_LOAD (PORTB |= B00010000) // high
#define LAT_DONE (PORTB &= B11101111) // low

#define CLK 11
#define CLK_HIGH (PORTB |= B00100000)
#define CLK_LOW  (PORTB &= B11011111)

#define SCAN_LINES 8
#define SHIFT_LENGTH 64
#define SHIFT_LENGTH_POWER 6 // 2^6 == 64
#define PHYSICAL_HEIGHT 32
#define PHYSICAL_WIDTH (2*SCAN_LINES*SHIFT_LENGTH / PHYSICAL_HEIGHT)

#define USER_TIME 500 // Clock cycles after the interupt that are avalible to the user per hblank (interupt is ~2200)

// enable OC1B output and fast PWM
#define PWM_MODE (TCCR1A |= B00110000) // set on match (output off)
#define MANUAL_CTRL (TCCR1A &= B11001111)

#define RED     B00000001
#define GREEN   B00000010
#define BLUE    B00000100
#define CYAN    (GREEN | BLUE)
#define MAGENTA (RED | BLUE)
#define YELLOW  (RED | GREEN)
#define WHITE   (RED | GREEN | BLUE)
#define BLACK   0
#define DONT_DRAW 0xFF
#define FULL_COLOR 0xFF
#define TRANSPARENT_COLOR 0xFE

#define SWAP(a, b) { a^=b; b^=a; a^=b; }

volatile bool readyFlip = false;
volatile bool showingFront = true;
unsigned char buf[2*SCAN_LINES*SHIFT_LENGTH] = {0};

void setPixel(unsigned char x, unsigned char y, unsigned char color) {
  if(x >= PHYSICAL_WIDTH || y >= PHYSICAL_HEIGHT) {
    return;
  }
  
  unsigned char scanLine = y % SCAN_LINES;

  // the first physical scanline is the last one in memory
  // the 2nd is at position 0
  if(scanLine == 0) {
    scanLine = SCAN_LINES;
  }
  scanLine--;

  // TODO this is probably not generic
  unsigned char logicalCol;
  if(y % (2 * SCAN_LINES) < SCAN_LINES) {
    // y is in an even numbered block (y is in a split section)
    
    if(x < SHIFT_LENGTH/4) {
      // we are before the split
      logicalCol = x;
    } else {
      // we are after the split
      logicalCol = x + (SHIFT_LENGTH/2);
    }
    
  } else {
    // y is in the continus section (odd numbered blocks)
    logicalCol = x + SHIFT_LENGTH/4;
  }

  unsigned int index = scanLine*(unsigned int)SHIFT_LENGTH + logicalCol;

  // if we are showing the front, we should draw on the back
  if(showingFront) index += SCAN_LINES*SHIFT_LENGTH;
  

  unsigned char bitOffset;
  if(y < PHYSICAL_HEIGHT/2) {
    bitOffset = TOP_COLOR_OFFSET;
  } else {
    bitOffset = BOTTOM_COLOR_OFFSET;
  }

  buf[index] &= ~(B00000111 << bitOffset); // clear bits
  buf[index] |= color << bitOffset; // set bits from color
}

#include <ThreeWire.h>  
#include <RtcDS1302.h>

ThreeWire myWire(51,53,49); // DAT/IO, CLK/SCLK, RST/CE
RtcDS1302<ThreeWire> Rtc(myWire);

void setup(void) {
  // RTC stuff
  Rtc.Begin();
  if (Rtc.GetIsWriteProtected()) {
      Rtc.SetIsWriteProtected(false);
  }
  if (!Rtc.GetIsRunning()) {
      Rtc.SetIsRunning(true);
  }
  // end RTC stuff

  pinMode(GREEN_BUTTON, INPUT_PULLUP);
  pinMode(RED_BUTTON,   INPUT_PULLUP);
  pinMode(BLUE_BUTTON,  INPUT_PULLUP);
  
  pinMode(R1_PIN, OUTPUT);
  pinMode(G1_PIN, OUTPUT);
  pinMode(B1_PIN, OUTPUT);
  pinMode(R2_PIN, OUTPUT);
  pinMode(G2_PIN, OUTPUT);
  pinMode(B2_PIN, OUTPUT);
  pinMode(CLK, OUTPUT);
  pinMode(OE, OUTPUT);
  digitalWrite(OE, HIGH); // this only takes effect when PWM controll is disabled
  pinMode(LAT, OUTPUT);
  pinMode(LINE_A, OUTPUT);
  pinMode(LINE_B, OUTPUT);
  pinMode(LINE_C, OUTPUT);
  pinMode(LINE_D, OUTPUT);

  // setup timer 1
  // fast PWM mode (top from ICR1)
  // clock from system clock
  TCCR1A = B00000010;
  TCCR1B = B00011001;

  // ICR1 to max
  // a.k.k. don't rollover the pwm timer for us
  ICR1 = 0xFFFF;

  // we reset the time after the interupt is done, so this is really avalible to the user
  OCR1A = (uint16_t)USER_TIME;

  setBrightness(25);

  // enable "timer 1 compare with OCR1A" interupt
  TIMSK1 |= B00000010;

  // enable interupts globaly
  sei();

  TCNT1 = 0; // reload timer
}

void setBrightness(uint16_t brightness) {
  // this is how long after the reset we leave the LEDs on (brightness)
  OCR1B = (uint16_t)brightness; //todo
}

ISR(TIMER1_COMPA_vect) {
  static unsigned int currentPixel = 0;
  static unsigned int nextHBlank = SHIFT_LENGTH;
  static unsigned int nextVBlank = SCAN_LINES*SHIFT_LENGTH;
  
  // shift data out
  for(; currentPixel < nextHBlank; currentPixel++) {
    DATA = buf[currentPixel];
    CLK_HIGH;
    CLK_LOW;
  }

  nextHBlank += SHIFT_LENGTH;

  // this is an optimization so this comaprison can be before the OUTPUT_OFF
  bool vBlank = currentPixel >= nextVBlank;

  MANUAL_CTRL;  // take controll of OE (defaults HIGH (off))
  LAT_LOAD; // begin load data from shift registers
  
  // this is in the middle, because we can use the time the latch is latching for other stuff
  // ">> SHIFT_LENGTH_POWER" is the same as "/SHIFT_LENGTH"
  LINE = currentPixel >> SHIFT_LENGTH_POWER;

  // this should go here, because no line should benifit from our processing time.
  if(vBlank) {    
    if(readyFlip) {
      readyFlip = false;
      showingFront = !showingFront;
      nextVBlank = showingFront ? (SCAN_LINES*SHIFT_LENGTH) : (2*SCAN_LINES*SHIFT_LENGTH);
    }
    currentPixel = showingFront ? 0                         : (SCAN_LINES*SHIFT_LENGTH);
    nextHBlank   = showingFront ? SHIFT_LENGTH              : (SCAN_LINES*SHIFT_LENGTH + SHIFT_LENGTH);
  }

  LAT_DONE; // end load data fron shift registers

  PWM_MODE; // give back controll of OE to timer1

  // reload timer (the clock starts when the line lights up so we have even light)
  // we let it roll over on it's own b.c. we are indirectly controling OE via OC1B
  // (ICR1 is TOP of timer)
  TCNT1 = ICR1;
}

void flip(bool retainPrevious) {
  readyFlip = true;
  
  // wait untill it is safe to draw/copy onto other buffer
  // it is unsafe if we have just fliped the buffer, but are still displaying the old one
  while(readyFlip);

  if(retainPrevious) {
    // copy so we can keep editing
    if(showingFront) {
      // copy front to back
      memcpy(buf + SCAN_LINES*SHIFT_LENGTH, buf, SCAN_LINES*SHIFT_LENGTH);
    } else {
      // copy back to front
      memcpy(buf, buf + SCAN_LINES*SHIFT_LENGTH, SCAN_LINES*SHIFT_LENGTH);
    }
  }
}

// bytes 0-127 are avalible
void bsWrite(unsigned char pos, unsigned char val) {
  buf[ pos*4                               ] &= B11111100;
  buf[ pos*4                               ] |= (val & B00000011);
  buf[ pos*4     + SCAN_LINES*SHIFT_LENGTH ] &= B11111100;
  buf[ pos*4     + SCAN_LINES*SHIFT_LENGTH ] |= (val & B00000011);
  
  buf[ pos*4 + 1                           ] &= B11111100;
  buf[ pos*4 + 1                           ] |= (val & B00001100) >> 2;
  buf[ pos*4 + 1 + SCAN_LINES*SHIFT_LENGTH ] &= B11111100;
  buf[ pos*4 + 1 + SCAN_LINES*SHIFT_LENGTH ] |= (val & B00001100) >> 2;
  
  buf[ pos*4 + 2                           ] &= B11111100;
  buf[ pos*4 + 2                           ] |= (val & B00110000) >> 4;
  buf[ pos*4 + 2 + SCAN_LINES*SHIFT_LENGTH ] &= B11111100;
  buf[ pos*4 + 2 + SCAN_LINES*SHIFT_LENGTH ] |= (val & B00110000) >> 4;
  
  buf[ pos*4 + 3                           ] &= B11111100;
  buf[ pos*4 + 3                           ] |= (val & B11000000) >> 6;
  buf[ pos*4 + 3 + SCAN_LINES*SHIFT_LENGTH ] &= B11111100;
  buf[ pos*4 + 3 + SCAN_LINES*SHIFT_LENGTH ] |= (val & B11000000) >> 6;
}

unsigned char bsRead(unsigned char pos) {
  return
    ((buf[pos*4    ] & B00000011)     ) |
    ((buf[pos*4 + 1] & B00000011) << 2) |
    ((buf[pos*4 + 2] & B00000011) << 4) |
    ((buf[pos*4 + 3] & B00000011) << 6);
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////

bool getBit(unsigned char pos, unsigned char b) {
  return (b >> pos) & B00000001;
}

#define FONT_WIDTH 5
#define FONT_HEIGHT 7
unsigned char drawChar(unsigned char c, unsigned char x, unsigned char y, unsigned char color, bool monospace) {
  unsigned char savedLines = 0; // empty lines in font/back
  for(unsigned char xOffset = 0; xOffset < FONT_WIDTH; xOffset++) {
    unsigned char fontVLine = pgm_read_byte_near(fontData + FONT_WIDTH*c + xOffset);
    if(fontVLine == 0 && !monospace) {
      // this is an empty line, so we can kern the font tighter
      savedLines++;
      continue;
    }

    if(color == DONT_DRAW) {
      // this is so we can check text width without drawing it
      continue;
    }
    
    for(unsigned char yOffset = 0; yOffset < 7; yOffset++) {
      if(getBit(yOffset, fontVLine) != 0) {
        unsigned char screenX = x + xOffset - savedLines;
        unsigned char screenY = y + yOffset;
        setPixel(screenX, screenY, color);
      }
    }
  }

  // return char width
  return FONT_WIDTH - savedLines;
}

#define VARIABLE_WIDTH -1
#define FIXED_WIDTH     0
#define LCD_1           1
#define LCD_2           2
#define LCD_3           3
#define LCD_4           4
unsigned char drawString(unsigned char s[], unsigned char x, unsigned char y, unsigned char color, signed char font, bool wrap) {
  unsigned char xCursor = x;

  // the amount of empty space between chars
  unsigned char padSize;
  if(font >= LCD_1) {
    padSize = font;
  } else {
    padSize = 1;
  }
  
  for(unsigned char i = 0; s[i] != '\0' ; i++) {
    unsigned char c;
    if(font >= LCD_1) {
      // lcd has a limited char set
      c = s[i] - '0'; // this translates '0' to 0
    } else {
      // this is out big font table with most chars
      c = s[i] - 32; // strings are in ASCII, the font dosen't have ctrl chars
    }

    if(wrap) {
      unsigned char charWidth;
      if(font == FIXED_WIDTH) {
        charWidth = FONT_WIDTH;
      } else if (font == VARIABLE_WIDTH) {
        charWidth = drawChar(c, 0, 0, DONT_DRAW, false);
      } else {
        // lcd fonts
        charWidth = font * 4;
      }

      if(xCursor + charWidth > PHYSICAL_WIDTH) {
        // we gotta wrap
        
        // how far down to move the cursor
        if(font >= LCD_1) {
          y += font * 8;
        } else {
          y += FONT_HEIGHT + 1;
        }
        
        xCursor = x;
      }
    }

    // draw char and move "cursor" over
    if(font == FIXED_WIDTH) {
      xCursor += drawChar(c, xCursor, y, color, true);
    } else if (font == VARIABLE_WIDTH) {
      xCursor += drawChar(c, xCursor, y, color, false);
    } else {
      drawLCD(c, xCursor, y, color, font);
      xCursor += font * 4;
    }
    xCursor += padSize;
  }

  // return string width (not including last blank line)
  // note this gives the width of the last line, so it may be odd with wrap=true;
  return xCursor - x - padSize;
}

void fill(unsigned char x1, unsigned char y1, unsigned char x2, unsigned char y2, unsigned char color) {
  if(x1 > x2) SWAP(x1, x2);
  if(y1 > y2) SWAP(y1, y2);
  
  for(; x1 <= x2; x1++) {
    for(unsigned char y = y1; y <= y2; y++) {
      setPixel(x1, y, color);
    }
  }
}

// d is 0-11 (10 is ":")
// s is size (>= 1)
#define LCD_FONT_WIDTH 4
void drawLCD(unsigned char c, unsigned char x, unsigned char y, unsigned char color, unsigned char s) {
  // get "glyph" from font
  unsigned char g = pgm_read_byte_near(lcdFontData + c);

  // fill in all the blocks for each lcd "segment"
  if(getBit(7, g) == 1) fill(x+0*s, y+0*s, x+4*s-1, y+1*s-1, color);
  if(getBit(6, g) == 1) fill(x+0*s, y+0*s, x+1*s-1, y+4*s-1, color);
  if(getBit(5, g) == 1) fill(x+3*s, y+0*s, x+4*s-1, y+4*s-1, color);
  if(getBit(4, g) == 1) fill(x+0*s, y+3*s, x+4*s-1, y+4*s-1, color);
  if(getBit(3, g) == 1) fill(x+0*s, y+3*s, x+1*s-1, y+7*s-1, color);
  if(getBit(2, g) == 1) fill(x+3*s, y+3*s, x+4*s-1, y+7*s-1, color);
  if(getBit(1, g) == 1) fill(x+0*s, y+6*s, x+4*s-1, y+7*s-1, color);

  // for ":"
  if(getBit(0, g) == 1) fill(x+1*s, y+1*s, x+3*s-1, y+3*s-1, color);
  if(getBit(0, g) == 1) fill(x+1*s, y+4*s, x+3*s-1, y+6*s-1, color);
}

bool getBitFromProgmem(const unsigned char a[], unsigned int bitIndex) {
  unsigned int bytePos = bitIndex / 8;
  unsigned char bitPos = bitIndex % 8;
  unsigned char b = pgm_read_byte_near(a + bytePos);
  return getBit(bitPos, b);
}

unsigned char getBitsFromProgmem(const unsigned char a[], unsigned int startBit, unsigned char width) {
  unsigned char ret = 0x00;
  for(unsigned char i = 0; i < width; i++) {
    bool b = getBitFromProgmem(a, startBit + i);
    ret <<= 1;
    ret |= b;
  }
  return ret;
}

unsigned char spriteWidth(const unsigned char sprite[]) {
  return getBitsFromProgmem(sprite, 0, 5) + 1;
}

unsigned char spriteHeight(const unsigned char sprite[]) {
  return getBitsFromProgmem(sprite, 5, 5) + 1;
}

unsigned char spritePixelColor(const unsigned char sprite[], unsigned int px) {
  // each pixel is 3 bits. The width and height are each 5 bits;
  unsigned int startBit = px*3 + 10;
  
  return getBitsFromProgmem(sprite, startBit, 3);
}

bool spritePixelMonochrome(const unsigned char sprite[], unsigned int px) {
  // The width and height are each 5 bits;
  unsigned int bitPos = px + 10;
  
  return getBitFromProgmem(sprite, bitPos);
}

void drawSprite(const unsigned char sprite[], unsigned char x, unsigned char y, unsigned char color) {
  unsigned char width = spriteWidth(sprite);
  unsigned int numPixels = width * spriteHeight(sprite);

  // if a sprite is drawn with TRANSPARENT_COLOR, the last pixel (*after* end)
  // sets which color indicates transparency.
  unsigned int transparencyColor = 0xFF;
  if(color == TRANSPARENT_COLOR) {
    transparencyColor = spritePixelColor(sprite, numPixels);
  }

  for(unsigned int pixel = 0, xOffset = 0; pixel < numPixels; pixel++) {
    if(xOffset >= width) {
      xOffset = 0;
      y++;
    }

    if(color == FULL_COLOR || color == TRANSPARENT_COLOR) {
      unsigned char pixelData = spritePixelColor(sprite, pixel);
      if(pixelData != transparencyColor) {
        setPixel(x + xOffset, y, pixelData);
      }
    } else {
      if(spritePixelMonochrome(sprite, pixel)) {
        setPixel(x + xOffset, y, color);
      }
    }

    xOffset++;
  }
}

unsigned char charToColor(char c) {
  switch(c) {
    case 'r': return RED;
    case 'g': return GREEN;
    case 'b': return BLUE;
    case 'c': return CYAN;
    case 'm': return MAGENTA;
    case 'y': return YELLOW;
    case 'k': return BLACK;
    case 'w': return WHITE;
    default:  return DONT_DRAW;
  }
}

void editorRenderLoop(void) {
  static char tmpStr[50];
  static unsigned long i = 0;
  if(i == 0) {
    // cheeting into not having a setup() function
    Serial.begin(9600);
    // the editor can't load things with a small value for user time
    OCR1A = (uint16_t)2500;
  }
  i++;

  static unsigned char imageData[PHYSICAL_WIDTH][PHYSICAL_HEIGHT];
  static unsigned char cursorX, cursorY;
  static unsigned char canvasWidth = 8, canvasHeight = 8;
  static unsigned char bgColor = RED;

  char command = Serial.read();
  switch(command) {
    case 'x': // set cursor x
      cursorX = Serial.parseInt();
      break;
    case 'y': // set cursor y
      cursorY = Serial.parseInt();
      break;
    case 'w': // set canvas width
      canvasWidth = Serial.parseInt();
      break;
    case 'h': // set canvas height
      canvasHeight = Serial.parseInt();
      break;
    case 'i': // set light intensity
      setBrightness(Serial.parseInt());
      break;
    case 'b': // set background (for display and sprite export)
      Serial.readBytes(tmpStr, 1);
      bgColor = charToColor(*tmpStr);
      break;
    case '8': // up
      cursorY--;
      break;
    case '4': // left
      cursorX--;
      break;
    case '6': // right
      cursorX++;
      break;
    case '2': // down
      cursorY++;
      break;
    case 'p': // paint color at cursor
      Serial.readBytes(tmpStr, 1);
      imageData[cursorX][cursorY] = charToColor(*tmpStr);
      break;
    case 'c': // clear
      for(unsigned char x = 0; x < PHYSICAL_WIDTH; x++) {
        for(unsigned char y = 0; y < PHYSICAL_HEIGHT; y++) {
          imageData[x][y] = 0;
        }
      }
      break;
    case 'f': // flip (upside down)
      for(unsigned char x = 0; x < canvasWidth; x++) {
        for(unsigned char y = 0; y < canvasHeight/2; y++) {
          SWAP(imageData[x][y], imageData[x][canvasHeight-1-y]);
        }
      }
      break;
    case 'r': // reverse (ex: make text backwards)
      for(unsigned char x = 0; x < canvasWidth/2; x++) {
        for(unsigned char y = 0; y < canvasHeight; y++) {
          SWAP(imageData[x][y], imageData[canvasWidth-1-x][y]);
        }
      }
      break;
    case 'z': // flip along diagonal
      bool swapFirst;
      swapFirst = false;
      if(canvasWidth > canvasHeight) {
        SWAP(canvasWidth, canvasHeight);
        swapFirst = true;
      }
      for(unsigned char x = 0; x < canvasWidth; x++) {
        for(unsigned char y = x+1; y < canvasHeight; y++) {
          SWAP(imageData[x][y], imageData[y][x]);
        }
      }
      if(!swapFirst) SWAP(canvasWidth, canvasHeight);
      break;
    case 'e': // export to sprite
      // width (1 is the min, so it get the first position of all zeros)
      sprintf(tmpStr, "%d%d%d%d%d",
        getBit(0, canvasWidth-1),
        getBit(1, canvasWidth-1),
        getBit(2, canvasWidth-1),
        getBit(3, canvasWidth-1),
        getBit(4, canvasWidth-1));
      Serial.println(tmpStr);

      // height
      sprintf(tmpStr, "%d%d%d%d%d",
        getBit(0, canvasHeight-1),
        getBit(1, canvasHeight-1),
        getBit(2, canvasHeight-1),
        getBit(3, canvasHeight-1),
        getBit(4, canvasHeight-1));
      Serial.println(tmpStr);

      // data
      for(unsigned char y = 0; y < canvasHeight; y++) {
        for(unsigned char x = 0; x < canvasWidth; x++) {
          unsigned char colorData = imageData[x][y];
          sprintf(tmpStr, "%d%d%d",
            getBit(0, colorData),
            getBit(1, colorData),
            getBit(2, colorData));
          Serial.println(tmpStr);
        }
      }

      // transparency color
      sprintf(tmpStr, "%d%d%d",
        getBit(0, bgColor),
        getBit(1, bgColor),
        getBit(2, bgColor));
      Serial.println(tmpStr);
      
      break;
      
    case 's': // save for import later
      // trigger load with 0 offset
      // you can manually tweek this to shift objects
      Serial.print("c l 0 0 ");
      
      Serial.print(canvasWidth);
      Serial.print(' ');

      Serial.print(canvasHeight);
      Serial.print(' ');
      
      // data
      for(unsigned char y = 0; y < canvasHeight; y++) {
        for(unsigned char x = 0; x < canvasWidth; x++) {
          unsigned char colorData = imageData[x][y];
          Serial.print(colorData & WHITE);
          Serial.print(' ');
        }
      }

      // transparency color
      Serial.print(bgColor);
      Serial.println(' ');      
      break;
      
    case 'l': // load
      // this is so we have a (somewhat cumbersome) way to shift objects
      signed char xOffset = Serial.parseInt();
      signed char yOffset = Serial.parseInt();
      
      canvasWidth = Serial.parseInt();
      canvasHeight = Serial.parseInt();

      // data
      for(unsigned char y = 0; y < canvasHeight; y++) {
        for(unsigned char x = 0; x < canvasWidth; x++) {
          unsigned char screenX = (x + xOffset) % PHYSICAL_WIDTH;
          unsigned char screenY = (y + yOffset) % PHYSICAL_HEIGHT;
          imageData[screenX][screenY] = Serial.parseInt();
        }
      }

      // transparency color
      bgColor = Serial.parseInt();

      break;
  }

  fill(0, 0, PHYSICAL_WIDTH-1, PHYSICAL_HEIGHT-1, bgColor);
  
  for(unsigned char x = 0; x < canvasWidth; x++) {
    for(unsigned char y = 0; y < canvasHeight; y++) {
      setPixel(x, y, imageData[x][y]);
    }
  }

  // blink inverting cursor
  setPixel(cursorX, cursorY, i/10%2 ? imageData[cursorX][cursorY] : imageData[cursorX][cursorY] ^ WHITE);
  
  flip(false);
}


// draw a search animation
void searchAnimation() {
  for(unsigned char frame = 0; frame < 30; frame++) {
    fill(0,0,31,31,BLACK);
    
    // width calculations (we could pre-compute this)
    char str[] = "4:??";
    unsigned char clockWidth = drawString(str, 0, 0, DONT_DRAW, VARIABLE_WIDTH, false);
    unsigned char margin = (32 - clockWidth) / 2;
  
    // draw the clock for real
    drawString(str, margin, 1, WHITE, VARIABLE_WIDTH, false);
  
    // draw a map
    drawSprite(search[frame%8], 0, 11, FULL_COLOR);
    
    flip(false);
    delay(100);
  }
}

#define NORTH_EAST B11000000
#define SOUTH_EAST B01000000
#define SOUTH_WEST B00000000
#define NORTH_WEST B10000000

unsigned char FGColor = WHITE;
unsigned char BGColor = BLACK;

void showRealTime(unsigned char hour, unsigned char minute) {
  fill(0,0,31,31,BGColor);
  static signed char x = 0;
  static signed char y = 0;
  static unsigned char dir = SOUTH_EAST;

  if(hour == 0) hour = 12;
  if(hour > 12) hour -= 12;

  char str[6];
  snprintf(str, 10, "%u:%02u", hour, minute);
  unsigned char clockWidth = drawString(str, x, y, FGColor, VARIABLE_WIDTH, false);
  
  switch(dir) {
    case NORTH_EAST:
      x++;
      y--;
      break;
    case SOUTH_EAST:
      x++;
      y++;
      break;
    case SOUTH_WEST:
      x--;
      y++;
      break;
    case NORTH_WEST:
      x--;
      y--;
      break;
  }
  
  if(x+clockWidth > 31) {
    dir &= B10111111;
  }
  if(x <= 0) {
    dir |= B01000000;
  }
  if(y+7 > 31) {
    dir |= B10000000;
  }
  if(y <= 0) {
    dir &= B01111111;
  }

  flip(false);
}

#include "zones.h"

void showFantasyTime(unsigned char month, unsigned char day, unsigned char hour, unsigned char minute) {
  static unsigned char lastAnimation = 1;
  if( (minute == 0 || minute == 30) && minute != lastAnimation ) {
    lastAnimation = minute;
    searchAnimation();
  }
  
  fill(0,0,31,31,BLACK);

  unsigned int dayIdx = dateToIndex(month, day);
  signed char timeIdx = timeToIndex(hour, minute);
  if(timeIdx < 0) timeIdx = 0; // shouldn't happen
  unsigned char zoneNum = pgm_read_byte_near(bestZoneByDay + 18*dayIdx + timeIdx);

  if(zoneNum == 0) minute -= 30; // this is the only "half-hour" time zone
  drawClock(4, minute);

  drawString(zoneNames[zoneNum], 0, 9, GREEN, VARIABLE_WIDTH, true);
  
  flip(false);
}

void drawClock(unsigned char hour, unsigned char minute) {
  char str[6];
  // do the width calculations as though the minute ends in 9
  // this prevents the clock from moving more than once per 10 minutes
  snprintf(str, 10, "%u:%02u", hour, minute/10*10 + 9);
  unsigned char clockWidth = drawString(str, 0, 0, DONT_DRAW, VARIABLE_WIDTH, false);
  unsigned char margin = (32 - clockWidth) / 2;

  // draw the clock for real with real minute value
  snprintf(str, 10, "%u:%02u", hour, minute);
  drawString(str, margin, 1, WHITE, VARIABLE_WIDTH, false);
}

// convert month and day to index position where jan 1 is 0
unsigned int dateToIndex(unsigned char month, unsigned int day) {
  switch(month) {
    case 12: day += 30; //nov
    case 11: day += 31; //oct
    case 10: day += 30; //sep
    case  9: day += 31; //aug
    case  8: day += 31; //jul
    case  7: day += 30; //jun
    case  6: day += 31; //may
    case  5: day += 30; //apr
    case  4: day += 31; //mar
    case  3: day += 28; //feb
    case  2: day += 31; //jan
  }
  return day - 1;
}

signed char timeToIndex(unsigned char hour, unsigned char minute) {
  if(hour < 7 || hour >= 16) return -1;
  return (hour-7)*2 + (minute/30);
}

unsigned int editNumber(char displayName[], unsigned int editing, unsigned int min, unsigned int max) {
  //save the user some scrolling in edge cases
  if(editing < min) editing = min;
  if(editing > max) editing = max;
  
  char str[6];
  while(digitalRead(RED_BUTTON) == LOW);
  delay(100);
  while(digitalRead(RED_BUTTON) == HIGH) {
    if(digitalRead(GREEN_BUTTON) == LOW && editing > min) editing--;
    if(digitalRead(BLUE_BUTTON)  == LOW && editing < max) editing++;
    while(digitalRead(GREEN_BUTTON) == LOW || digitalRead(BLUE_BUTTON) == LOW);
    fill(0,0,31,31,BLACK);
    drawString(displayName, 0, 0, WHITE, VARIABLE_WIDTH, false);
    drawString("-", 0, 7, GREEN, VARIABLE_WIDTH, false);
    drawSprite(rightArrowIcon, 13, 8, RED);
    drawString("+", 27, 7, BLUE, VARIABLE_WIDTH, false);
    snprintf(str, 6, "%u", editing);
    drawString(str, 0, 16, WHITE, VARIABLE_WIDTH, true);
    flip(false);
  }
  return editing;
}

unsigned char countdownDaysLeft = 0;
void showCountdown(unsigned char hour) {
  static bool adjustedCountdownToday = true;
  if(hour < 12 && !adjustedCountdownToday) {
    countdownDaysLeft--;
    adjustedCountdownToday = true;
  }
  if(hour >= 12) {
    adjustedCountdownToday = false;
  }
  
  char str[3];
  snprintf(str, 3, "%u", countdownDaysLeft);
  unsigned char font, x, y;
  if(countdownDaysLeft == 0) {
    font = LCD_4;
    x    = 8;
    y    = 2;
  } else if(countdownDaysLeft <= 1) {
    font = LCD_4;
    x    = 2;
    y    = 2;
  } else if(countdownDaysLeft <= 9) {
    font = LCD_4;
    x    = 8;
    y    = 2;
  } else if(countdownDaysLeft <= 19) {
    font = LCD_3;
    x    = -2;
    y    = 5;
  } else if(countdownDaysLeft <= 99) {
    font = LCD_3;
    x    = 3;
    y    = 5;
  } else {
    countdownDaysLeft = 99;
    return; // rough error handeling
  }
  
  fill(0,0,31,31,BGColor);
  drawString(str, x, y, FGColor, font, false);
  flip(false);
}

void showSettings() {
  char monthNames[][4] = {
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec"
  };

  unsigned char monthMaxDays[] {
    32, // Jan
    29, // Feb
    31, // Mar
    30, // Apr
    31, // May
    30, // Jun
    31, // Jul
    31, // Aug
    30, // Sep
    31, // Oct
    30, // Nov
    31  // Dec
  };
  
  fill(0,0,31,31,BLACK);
  drawString("Sett", 0, 0, WHITE, VARIABLE_WIDTH, true);
  flip(false);
  delay(1000);
  while(digitalRead(RED_BUTTON) == LOW);

  RtcDateTime now = Rtc.GetDateTime();
  unsigned int  year   = editNumber("Year",    now.Year(),   2000, 2099);
  unsigned char month  = editNumber("Month",   now.Month(),  1, 12);
  unsigned char day    = editNumber("Day",     now.Day(),    1, monthMaxDays[month-1]);
  unsigned char hour   = editNumber("Hr (24)", now.Hour(),   0, 23);
  unsigned char minute = editNumber("Min",     now.Minute(), 0, 59);

  char dateStr[12];
  char timeStr[9];
  snprintf(dateStr, 12, "%s %02u %04u", monthNames[month-1], day, year);
  snprintf(timeStr, 9, "%02u:%02u:00", hour, minute);
  Rtc.SetDateTime(RtcDateTime(dateStr, timeStr));

  countdownDaysLeft = editNumber("C Days",  countdownDaysLeft, 0, 99);
  FGColor  = editNumber("FG   Col", FGColor,  0, 7);
  BGColor  = editNumber("BG   Col", BGColor,  0, 7);

  fill(0,0,31,31,BLACK);
  drawString("Saved", 0, 0, WHITE, VARIABLE_WIDTH, true);
  flip(false);
  delay(1000);
}

void adjustBrightness() {
  static unsigned int brightness = 25; // this is what we set it to in setup
  unsigned char framesWithoutInput = 0;
  char str[6];
  
  while(framesWithoutInput < 100) {
    unsigned int incriment = 1;
    if(brightness >= 500) incriment = 5;
    if(brightness >= 2000) incriment = 50;
    
    if(digitalRead(GREEN_BUTTON) == LOW && brightness > 0) {
      brightness -= incriment;
      framesWithoutInput = 0;
    } else if(digitalRead(BLUE_BUTTON) == LOW && brightness < 10000) {
      brightness += incriment;
      framesWithoutInput = 0;
    } else {
      framesWithoutInput++;
    }
    
    fill(0,0,31,31,BLACK);
    drawSprite(sunIcon, 8, 12, YELLOW);
    snprintf(str, 6, "%u", brightness);
    drawString(str, 0, 0, WHITE, VARIABLE_WIDTH, true);
    flip(false);
    setBrightness(brightness);
  }
}

void loop(void) {
  //editorRenderLoop(); /*
  RtcDateTime now = Rtc.GetDateTime();

  if(digitalRead(GREEN_BUTTON) == LOW || digitalRead(BLUE_BUTTON) == LOW) {
    adjustBrightness();
  } else if(digitalRead(RED_BUTTON) == LOW) {
    showSettings();
  } else if(countdownDaysLeft > 0) {
    showCountdown(now.Hour());
  } else if(timeToIndex(now.Hour(), now.Minute()) == -1) {
    showRealTime(now.Hour(), now.Minute());
  } else {
    showFantasyTime(now.Month(), now.Day(), now.Hour(), now.Minute());
  }
  for(unsigned char i = 0; i < 10; i++) {
    if(digitalRead(GREEN_BUTTON) == LOW) break;
    if(digitalRead(RED_BUTTON) == LOW) break;
    if(digitalRead(BLUE_BUTTON) == LOW) break;
    delay(100);
  }
  //*/
}
