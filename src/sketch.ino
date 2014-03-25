/*
 * Meteoduino - An Arduino meterorological application
 *
 * See README.md for sensors and physical connections
 *
 * mail@lmcarvalho
 *
 */
// Ported to SdFat from the native Arduino SD library example by Bill Greiman
// On the Ethernet Shield, CS is pin 4. SdFat handles setting SS
const int chipSelect = 4;

#include <SdFat.h>
#include <Ethernet.h>
#include <Wire.h>
#include <DHT.h>
#include <Timer.h>

#define SECONDS_FROM_1970_TO_2000 946684800
#define DS1307_ADDRESS 0x68

#define DHTPIN 2         // what pin we're connected to
#define DHTTYPE DHT22   // DHT 22  (AM2302)

// Connect pin 1 (on the left) of the sensor to +5V
// Connect pin 2 of the sensor to whatever your DHTPIN is
// Connect pin 4 (on the right) of the sensor to GROUND
// Connect a 10K resistor from pin 2 (data) to pin 1 (power) of the sensor

const char head[] PROGMEM = {"<head><title>Home</title><script src='http://code.highcharts.com/adapters/standalone-framework.js'></script><script src='http://code.highcharts.com/highcharts.js'></script></head>"};

const char js[] PROGMEM = {"<script>var e=document.querySelector('#x').innerHTML.split('\\n'),t=[],r=[];for(i=0;i<e.length;i++){var a=e[i].split(' ');if(a.length==4||a.length==2){if(a.length==4){var n=parseInt(a[0]),s=parseInt(a[1]),p=parseInt(a[2]),h=parseInt(a[3])}else{n+=s;p+=parseInt(a[0]);h+=parseInt(a[1])}t.push([n*1e3,p/10]);r.push([n*1e3,h/10])}}new Highcharts.Chart({chart:{renderTo:'x'},title:{text:'Meteo'},xAxis:{type:'datetime'},yAxis:{title:{text:'Meteo'}},series:[{name:'Humidity',data:t},{name:'Temperature',data:r}]})</script>"};

DHT dht(DHTPIN, DHTTYPE);

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(10,0,0,1);

// Initialize the Ethernet server library
// with the IP address and port you want to use
// (port 80 is default for HTTP):
EthernetServer server(80);

Timer timer;

SdFat sd;
SdFile myFile;

uint32_t last_timestamp = 0;
int last_h = 0;
int last_t = 0;

const int interval = 30; // In seconds

const char live[] = "GET /i HTTP/1.1";

char req[16];
byte bytes_read;
boolean get = true;
EthernetClient streamClient = NULL;

void setup() {
  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
  Wire.begin();
  dht.begin();

  // Initialize SdFat or print a detailed error message and halt
  // Use half speed like the native library.
  // change to SPI_FULL_SPEED for more performance.
  if (!sd.begin(chipSelect, SPI_HALF_SPEED)) sd.initErrorHalt();
  Serial.println("initialization done.");

  logValues();
  timer.every(interval * 1000, logValues); // Every 30s

  // start the Ethernet connection and the server:
  Ethernet.begin(mac, ip);
  server.begin();
  Serial.print("server is at ");
  Serial.println(Ethernet.localIP());
}

void loop() {
  // listen for incoming clients
  EthernetClient client = server.available();
  if (client) {
    Serial.println("new client");
    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    bytes_read = 0;
    get = false;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          if(get) {
            Serial.println("Starting stream");
            stream(client);
            break;
          } else {
            Serial.println("Normal request");
          }
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          // the connection will be closed after completion of the response
          client.println("Connection: close");
          // refresh the page automatically every 5 sec
//          client.println("Refresh: 5");
          client.println();
          client.println("<!DOCTYPE HTML>");
          client.println("<html>");

          writeProgStr(client, head);
          client.println("<body><div id='x'>");
          // Output Temperature and Humidity values
          // re-open the file for reading:
          if (myFile.open("values.txt", O_READ)) {
            // read from the file until there's nothing else in it:
            int data;
            while ((data = myFile.read()) >= 0) {
              client.write(data);
            }
            // close the file:
            myFile.close();
          } else {
            // if the file didn't open, print an error:
            sd.errorHalt("opening values.txt for read failed");
            client.println("error opening values.txt");
          }
          client.println("</div>");
          writeProgStr(client, js);
          client.println("</body></html>");
          break;
        }
        if (c == '\n') {
          if(!get) {
            get = true;
            for(byte i=0;i<bytes_read;i++) {
              if(req[i] != live[i]) {
                get = false;
              }
              Serial.write(req[i]);
            }
            bytes_read = 0;
          }
          // you're starting a new line
          currentLineIsBlank = true;
        } else if (c != '\r') {
          if(bytes_read < 16) {
            req[bytes_read++] = c;
          }
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
      timer.update();
    }
    // give the web browser time to receive the data
    delay(1);
    // close the connection:
    if(!get) {
      client.stop();
    }
    Serial.println("client disconnected");
  }
  timer.update();
}

void logValues(void) {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int _h = 0;
  int _t = 0;
  uint32_t tstamp = timestamp();
  if (isnan(t) || isnan(h)) {
    Serial.println("Failed to read from DHT");
  } else {
    // open the file for write at end like the Native SD library
    if (myFile.open("values.txt", O_RDWR | O_CREAT | O_AT_END)) {
      // if the file opened okay, write to it:
      _h = (int) (h * 10);
      _t = (int) (t * 10);
      if( last_timestamp == 0 ) {
        myFile.print(tstamp);
        myFile.print(" ");
        myFile.print(interval);
        myFile.print(" ");
      } else {
        _h = last_h - _h;
        _t = last_t - _t;
      }
      myFile.print(_h);
      myFile.print(" ");
      myFile.print(_t);
      myFile.println();
      // close the file:
      myFile.close();

      if(streamClient != NULL) {
        Serial.println("Stream is not null");
        if (streamClient.connected()) {
          Serial.println("Stream client is connected");
          streamClient.print(_h);
          streamClient.print(" ");
          streamClient.println(_t);
        } else {
          Serial.println("Stream client disconnected");
          streamClient.stop();
        }
      }

      Serial.println("done.");
      last_timestamp = tstamp;
      last_h = (int) (h * 10);
      last_t = (int) (t * 10);
    } else {
      // if the file didn't open, print an error:
      sd.errorHalt("opening values.txt for write failed");
      Serial.println("error opening values.txt");
    }
  }
}

long time2long(uint16_t days, uint8_t h, uint8_t m, uint8_t s) {
  return ((days * 24L + h) * 60 + m) * 60 + s;
}

//has to be const or compiler compaints
const uint8_t daysInMonth [] PROGMEM = { 31,28,31,30,31,30,31,31,30,31,30,31 };

// number of days since 2000/01/01, valid for 2001..2099
static uint16_t date2days(uint16_t y, uint8_t m, uint8_t d) {
    uint16_t days = d;
    for (uint8_t i = 1; i < m; ++i) {
      days += pgm_read_byte(daysInMonth + i - 1);
    }
    if (m > 2 && y % 4 == 0) {
      ++days;
    }
    return days + 365 * y + (y + 3) / 4 - 1;
}

static byte bcd2dec(byte val)  {
  // Convert binary coded decimal to normal decimal numbers
  return ( (val/16*10) + (val%16) );
}

static uint32_t timestamp(void) {
  // Reset the register pointer
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(0);
  Wire.endTransmission();

  Wire.requestFrom(DS1307_ADDRESS, 7);

  int ss = bcd2dec(Wire.read());
  int mm = bcd2dec(Wire.read());
  int hh = bcd2dec(Wire.read() & 0b111111); //24 hour time
  int __ = bcd2dec(Wire.read()); // DayOfWeek - Discard
  int d  = bcd2dec(Wire.read());
  int m  = bcd2dec(Wire.read());
  int y  = bcd2dec(Wire.read());

  uint16_t days = date2days(y, m, d);

  uint32_t t = time2long(days, hh, mm, ss);
  t += SECONDS_FROM_1970_TO_2000;

  return t;
}

// given a PROGMEM string, use Serial.print() to send it out
static void writeProgStr(EthernetClient client, const char str[]) {
  char c;
  if(!str) return;
  while((c = pgm_read_byte(str++))) {
    client.write(c);
  }
}

void stream(EthernetClient client) {
  streamClient = client;
  streamClient.println("HTTP/1.1 200 OK");
  streamClient.println("Content-Type: text/event-stream");
}
