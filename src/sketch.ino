/*
 * Meteoduino - An Arduino meterorological application
 *
 * See README.md for sensors and physical connections
 *
 * Author: Luis Carvalho (mail@lmcarvalho)
 * March 2014
 */

#include <Print.h>
#include <SdFat.h>
#include <SdFatUtil.h>
#include <Ethernet.h>
#include <Wire.h>
#include <Timer.h>

// The RTC module reports years in YY form
#define SECONDS_FROM_1970_TO_2000 946684800
// RTC I2C address
#define DS1307_ADDRESS 0x68

// What pin we're connected to
#define DHTPIN 2
// How many timing transitions we need to keep track of. 2 * number bits + extra
#define MAXTIMINGS 85
#define MAXCOUNT 6

// Maximum allowed clock drift between updates
#define MAXDRIFT 5

// The filename in the SD card in which to store the values
#define FILENAME "values.txt"

// Store error strings in flash to save RAM
//#define error(s) sd.errorHalt_P(PSTR(s))
#define error(s) SerialPrintln_P(PSTR(s))

// SD card chip select (pin 4 by default)
//const uint8_t SD_CHIP_SELECT = SS;
#define SD_CHIP_SELECT 4

// MAC and IP addresses
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192,168,2,251);
//IPAddress ip(10,0,0,1);

// HTML <head> section (includes Highcharts framework js)
const char head[] PROGMEM = {
    "<head><title>Home</title><script src='http://code.highcharts.com/adapters/"
    "standalone-framework.js'></script><script src='http://code.highcharts.com/"
    "stock/highstock.js'></script></head>"};

// Main js script to build the chart (see chart.js)
const char js[] PROGMEM = {
    "<script>var n=document.querySelector('#x').innerHTML.split('\\n'),a=[],r=["
    "];for(i=0;i<n.length;i++){var e=n[i].split(' ');if(e.length==4||e.length=="
    "2){if(e.length==4){var t=parseInt(e[0]),l=parseInt(e[1]),s=parseInt(e[2]),"
    "p=parseInt(e[3])}else{t+=l;s+=parseInt(e[0]);p+=parseInt(e[1])}a.push([t*1"
    "e3,s/10]);r.push([t*1e3,p/10])}}var o=new Highcharts.StockChart({chart:{re"
    "nderTo:'x'},title:{text:'Meteo'},rangeSelector:{buttons:[{type:'day',count"
    ":1,text:'1d'},{type:'week',count:1,text:'1w'},{type:'month',count:1,text:'"
    "1m'},{type:'ytd',text:'YTD'},{type:'year',count:1,text:'1y'},{type:'all',t"
    "ext:'All'}]},legend:{enabled:true},series:[{name:'Humidity',data:a},{name:"
    "'Temperature',data:r}]});var d=new EventSource('/i');d.onmessage=function("
    "t){var e=t.data.split(' ');o.series[0].addPoint([parseInt(e[0])*1e3,parseI"
    "nt(e[1])/10]);o.series[1].addPoint([parseInt(e[0])*1e3,parseInt(e[2])/10])"
    "}</script>"};

// stream-event HTTP Header
const char stream_header[] PROGMEM = {
    "HTTP/1.1 200 OK\n"
    "Content-Type: text/event-stream\n"
    "Cache-Control: no-cache\n\n"};

// plain HTML HTTP Header
const char html_header[] PROGMEM = {
    "HTTP/1.1 200 OK\n"
    "Content-Type: text/html\n"
    "Connection: close\n\n"
    "<!DOCTYPE HTML>\n<html>\n"};

// Initialize the Ethernet server library
EthernetServer server(80);

// Timer to periodically log sensor values
Timer timer;

// SD card-related variables
SdFat sd;
SdFile file;

// Sampling period, in seconds
const uint16_t interval = 5 * 60;

// GET HTTP request header for the live stream
const char streamReq[] = "GET /i HTTP/1.1";
// Ethernet client variable, so we can keep the stream connection alive
EthernetClient streamClient;
boolean streamConnected = false;

// Last values read
uint32_t last_tstamp = 0;
short    last_humi   = 0;
short    last_temp   = 0;
uint8_t  drift       = 0;

void setup() {
  // Open serial communications
  Serial.begin(9600);
  // Initialize RTC communications
  Wire.begin();

  // Initialize the SD at SPI_HALF_SPEED to avoid bus errors with
  // breadboards. Use SPI_FULL_SPEED for better performance.
  if (!sd.begin(SD_CHIP_SELECT, SPI_HALF_SPEED)) sd.initErrorHalt();

  // Start by login sensor values
  logValues();
  // Every _interval_ seconds, log values. Forever!
  timer.every(((unsigned long) interval) * 1000, logValues);

  // Start the Ethernet connection and the server
  Ethernet.begin(mac, ip);
  server.begin();
  PgmPrintln("Ready");
}

void loop() {
  // Listen for incoming clients
  EthernetClient client = server.available();
  if( client ) {
    PgmPrintln("new client");
    // An http request ends with a blank line
    boolean currentLineIsBlank = true;
    boolean startStream = false;
    byte bytes_read = 0;

    while( client.connected() ) {
      if( client.available() ) {
        char c = client.read();
        Serial.write(c);
        // If you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if( c == '\n' && currentLineIsBlank ) {
          // If the request was for a stream session, I.E.: /i
          if( startStream ) {
            PgmPrintln("Starting stream");
            httpStartStream(client);
          } else {
            PgmPrintln("Normal request");
            httpSendResponse(client);
            // Give the web browser time to receive the data
            delay(1);
            // Close the connection:
            client.stop();
          }
          break;
        }
        if( c == '\n' ) {
          // When the line has ended, and we have not identified the request
          // as strea, and 15 bytes of the request match our request string
          // GET /i HTTP/1.1
          if( !startStream && bytes_read == 15 ) {
            startStream = true;
          }
          // You're starting a new line
          currentLineIsBlank = true;
        } else {
          if( c != '\r' ) {
            // You've gotten a character on the current line
            currentLineIsBlank = false;
          }

          if( bytes_read < 15 ) {
            // Try to match /i request
            if( streamReq[bytes_read] == c ) {
              bytes_read++;
            } else {
              // Only allow it if the characters are sequential
              bytes_read = 0;
            }
          }
        }
      }
    }
  }
  // Check if _interval_ has passed
  timer.update();
}

/*
 * Store values in SD card for later retrieval
 * Send them through the ethernet, if a stream client is connected
 *
 */
void logValues(void) {
  PgmPrintln("logging values");
  short h, t;
  // Read values, check for success
  if( dhtRead(t, h) ) {
    // Check the time with the RTC
    uint32_t tstamp = timestamp();
    // Update drift
    // On first run, drift = tstamp - interval
    drift += tstamp - last_tstamp - interval;

    PgmPrintln("opening file");
    file.writeError = false;
    // Open the file for write at end like the Native SD library
    if( !file.open(FILENAME, O_CREAT | O_APPEND | O_WRITE) ) {
      error("open failed");
    }
    PgmPrintln("file ok");
    // If the file opened okay, write to it:
    // Don't tolerate errors > than 5 seconds
    if( drift > MAXDRIFT ) {
      PgmPrintln("MAXDRIFT exceeded");
      drift = 0;
      // First time we write to the file
      // Write the timestamp along with the values read
      file.print(tstamp);
      file.print(" ");
      file.print(interval);
      file.print(" ");
      file.print(h);
      file.print(" ");
      file.println(t);
    } else {
      // We already have values on the file,
      // write only the difference to the last values read, to save space
      file.print(h - last_humi);
      file.print(" ");
      file.println(t - last_temp);
    }
    // Check for errors
    if( file.writeError ) error("write failed");
    // Close the file:
    if( !file.close() ) error("close failed");

    // Do we have a client?
    if( streamConnected ) {
      PgmPrintln("Stream is not null");
      // Is it still connected?
      if( streamClient.connected() ) {
        // Client is still online. Give it data
        PgmPrintln("Stream client is connected");
        streamClient.print("data: ");
        streamClient.print(tstamp);
        streamClient.print(" ");
        streamClient.print(h);
        streamClient.print(" ");
        streamClient.print(t);
        streamClient.println("\n"); // Finish with 2 \n just because
      } else {
        // Client disconnected in the meanwhile. Close the connection
        PgmPrintln("Stream client disconnected");
        streamClient.stop();
        streamConnected = false;
      }
    }
    PgmPrintln("logValues done");
    // Update last values read
    last_tstamp = tstamp;
    last_humi   = h;
    last_temp   = t;
  } else {
    PgmPrintln("Failed to read from DHT");
  }
}

/*
 * Number of days in all months of the year
 */
const uint8_t daysInMonth [] PROGMEM = { 31,28,31,30,31,30,31,31,30,31,30,31 };

/*
 * Number of days since 2000/01/01, valid for 2001..2099
 */
static uint16_t date2days(uint8_t y, uint8_t m, uint8_t d) {
  uint16_t days = d;
  for( uint8_t i = 1; i < m; ++i ) {
    days += pgm_read_byte(daysInMonth + i - 1);
  }
  if( m > 2 && y % 4 == 0 ) {
    ++days;
  }
  return days + 365 * y + (y + 3) / 4 - 1;
}

/*
 * Convert binary coded decimal to normal decimal numbers
 */
static byte bcd2dec(byte val)  {
  return ( (val/16*10) + (val%16) );
}

/*
 * Read the current time from the RTC chip
 * Returns number of seconds since UNIX epoch
 */
uint32_t timestamp(void) {
  // Reset the register pointer
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(0);
  Wire.endTransmission();

  Wire.requestFrom(DS1307_ADDRESS, 7);

  uint8_t ss = bcd2dec(Wire.read());
  uint8_t mm = bcd2dec(Wire.read());
  uint8_t hh = bcd2dec(Wire.read() & 0b111111); //24 hour time
  uint8_t __ = bcd2dec(Wire.read()); // DayOfWeek - Discard
  uint8_t d  = bcd2dec(Wire.read());
  uint8_t m  = bcd2dec(Wire.read());
  uint8_t y  = bcd2dec(Wire.read());

  uint16_t days = date2days(y, m, d);

  return ((days * 24L + hh) * 60 + mm) * 60 + ss + SECONDS_FROM_1970_TO_2000;
}

/*
 * HTTP
 * Start streaming
 */
void httpStartStream(EthernetClient client) {
  PgmPrintln("Stream request");
  // Disconnect previous client
  if( streamConnected ) {
    streamClient.stop();
  }
  // Save client pointer for later
  streamClient = client;
  streamConnected = true;
  progWrite(&streamClient, stream_header);
}

/*
 * HTTP
 * Serve normal response page
 */
void httpSendResponse(EthernetClient client) {
  PgmPrintln("HTML request");
  progWrite(&client, html_header);
  // Write standard http response header and beginning of body
  progWrite(&client, head);
  client.println("<body><div id='x'>");

  // Output Temperature and Humidity values stored in values.txt
  if( file.open(FILENAME, O_READ) ) {
    // read from the file until there's nothing else in it:
    int16_t c;
    while ((c = file.read()) > 0) client.write((char)c);
    // close the file:
    if (!file.close()) error("close failed");
  } else {
    // if the file didn't open, print an error:
    error("file.open");
  }
  client.println("</div>");
  progWrite(&client, js);
  client.println("</body></html>");
}

/*
 * Read sensor data from DHT
 * Taken from Adafruit's DHT library
 * https://github.com/adafruit/DHT-sensor-library
 */
boolean dhtRead(short &temp, short &humi) {
  uint8_t data[6];
  uint8_t laststate = HIGH;
  uint8_t counter = 0;
  uint8_t j = 0, i;

  // pull the pin high and wait 250 milliseconds
  digitalWrite(DHTPIN, HIGH);
  delay(250);

  data[0] = data[1] = data[2] = data[3] = data[4] = 0;

  // now pull it low for ~20 milliseconds
  pinMode(DHTPIN, OUTPUT);
  digitalWrite(DHTPIN, LOW);
  delay(20);
  cli();
  digitalWrite(DHTPIN, HIGH);
  delayMicroseconds(40);
  pinMode(DHTPIN, INPUT);

  // read in timings
  for ( i=0; i < MAXTIMINGS; i++) {
    counter = 0;
    while (digitalRead(DHTPIN) == laststate) {
      counter++;
      delayMicroseconds(1);
      if (counter == 255) {
        break;
      }
    }
    laststate = digitalRead(DHTPIN);

    if (counter == 255) break;

    // ignore first 3 transitions
    if ((i >= 4) && (i%2 == 0)) {
      // shove each bit into the storage bytes
      data[j/8] <<= 1;
      if (counter > MAXCOUNT)
        data[j/8] |= 1;
      j++;
    }
  }
  sei();
  // check we read 40 bits and that the checksum matches
  if ((j >= 40) && (data[4] == ((data[0]+data[1]+data[2]+data[3]) & 0xFF))) {
    humi = data[0] * 256 + data[1];
    temp = (data[2] & 0x7F) * 256 + data[3];
    if (data[2] & 0x80) {
	    temp *= -1;
    }
    return true;
  }
  return false;
}

/*
 * Write from PROGMEM into any Print object
 * Useful to print PROGMEM vars into Ethernet client
 */
void progWrite(Print *ptr, const char* str) {
  char c;
  while(c = pgm_read_byte(str++)) {
    (*ptr).write(c);
  }
}
