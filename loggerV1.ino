
/* Off-grid energy monitor system based on the DataDuino project by Matt Little  See www.re-innovation.co.uk for information and construction details

Main code written by Matt Little 30/10/12
Additional code by Tom Dixon 1/5/14
 

/*************Details of Code*****************************

  The DataDuino is an Arduino based SD card datalogger.
  A PCF8563 Realt Time Clock is used to timestamp the data.
  

  Pin D3 is set up to cound pulses from a sensor (such as a anemometer or flow sensor)
  Pins D8,D9 are used to step through a 4051 multiplexer to yield 4 data readings into one analog input
  Pins A0 to A3 are set up to record analogue information (0 to 1024)
  
  Each logger has a reference (user adjustable from 00-99).
  
  Data is written to a .csv file created on an SD card.
  A new file is created each day. If file alreay present then data is appended.
  The file name is created from the reference number and the date in the format:
  RXXDXXXX.csv, where RXX is the reference number and DXXXX is the date in the format DDMM. 
  
  
  You can adjust the parameters of the device using serial commands. These parameters are stored in EEPROM.
  These are:
  R??E
  This will change the reference number to ??
  T??????E
  This will change the time to HHMMSS
  D??????E
  This will change the date to DDMMYY
  S?????E
  This will change the sample period to ????? seconds. Set to 00001 for 1 second data, set to 03600 for 1 hour data.
  The minimum is 1 second data. The maximum is 99999 seconds
 

 
 //*********SD CARD DETAILS***************************	
 The SD card circuit:
 SD card attached to SPI bus as follows:
 ** MOSI - pin 11
 ** MISO - pin 12
 ** CLK - pin 13
 ** CS - pin 10
 ** Card detect - pin 6
 
 SD card code details:
 created  24 Nov 2010
 updated 2 Dec 2010
 by Tom Igoe
 
 //************ Real Time Clock code*******************
 A PCF8563 RTC is attached to pins:
 ** A4 - SDA (serial data)
 ** A5 - SDC (serial clock)
 ** D2 - Clock out - This gives a 1 second pulse to record the data
 
 RTC PCF8563 code details:
 By Joe Robertson, jmr
 orbitalair@bellsouth.net
 
**********************************************************************************************************/


/************ External Libraries*****************************/

#include <stdlib.h>
#include <Wire.h>          // Required for RTC
#include <Rtc_Pcf8563.h>   // RTC library
#include <SD.h>            // SD card library
#include <avr/pgmspace.h>  // Library for putting data into program memory
#include <EEPROM.h>        // For writing values to the EEPROM
#include <avr/eeprom.h>

/************User variables and hardware allocation**********************************************/

/******* SD CARD*************/
const int chipSelect = 10; // The SD card Chip Select pin 10
const int cardDetect = 6;  // The SD card detect is on pin 6
// The other SD card pins (D11,D12,D13) are all set within SD.h
int cardDetectOld = LOW;  // This is the flag for the old reading of the card detect

/*************Real Time Clock*******/
Rtc_Pcf8563 rtc;
#define I2C_RTC 0x51 // 7 bit address (without last bit - look at the datasheet)
int RTCinterrupt = 0;  // RTC interrupt - This is pin 2 of ardunio - which is INT0

/********* Output LED *************/
const int LEDred = 5;  // The output led is on pin 5


//********Multiplexing******************/

const int select[] = {8,9}; // pins connected to the 4051 input select lines S0 and S1 - note S2 is connected to ground as we're only using first 4 pins of 4051
int MultiPin = A3; //4051 output pin "z"



// ****** Serial Data Read***********
// Variables for the serial data read
char inByte;         // incoming serial char
String str_buffer = "";  // This is the holder for the string which we will display


//********Variables for the Filename*******************

char filename[] = "RXXDXXXX.csv";  // This is a holder for the full file name
int refnumber;     // The house number here, which is stored in EEPROM
File datafile;   // The logging file
String dataString;    // This is the holder for the data as a string. Start as blank
String dataRString;
int counter = 0;   // Clue is in the name - its a counter.
long dataCounter = 0;  // This holds the number of seconds since the last data store
long sampleTime = 10;  // This is the time between samples for the DAQ
//int SamplesPerSD = 600;
// Variables for the Pulse Counter
int pulseinterrupt = 1;  // Pulse Counter Interrupt - This is pin 3 of arduino - which is INT1
volatile long pulsecounter = 0;  // This counts pulses from the flow sensor  - Needs to be long to hold number
volatile long pulsecounterold = 0;  // This is storage for the old flow sensor - Needs to be long to hold number
volatile unsigned long ContactTime; //Timer to avoid contact bounce in interrupt routine

volatile int writedataflag = HIGH;  // A flag to tell the code when to write data

// Varibales for writing to EEPROM
int hiByte;      // These are used to store longer variables into EERPRPROM
int loByte;

// These next ints are for the filename conversion
int ref1 = 0;
int ref2 = 0;
int day_int =0;      // To find the day from the Date for the filename
int day_int1 =0;
int day_int2 =0;
int month_int = 0;
int month_int1 = 0;
int month_int2 = 0;
int year_int = 0;  // Year
int hour_int = 0;
int min_int = 0;
int sec_int = 0;

//**********STRINGS TO USE****************************
String comma = ",";
String date;        // The stored date from filename creation
String newdate;     // The new date, read every time 

// These are Char Strings - they are stored in program memory to save space in data memory
// These are a mixutre of error messages and serial printed information
const char headers[] PROGMEM = "Time,Pulses,Voltage,Turbine1,Turbine2,Diesel,PV,Inverter,Wind Direction";  // Headers for the top of the file
const char headersOK[] PROGMEM = "Headers OK";
const char erroropen[] PROGMEM = "Error opening file";
const char error[] PROGMEM = "ERROR ERROR ERROR";
const char initialisesd[] PROGMEM = "Initialising SD card...";
const char dateerror[] PROGMEM = "Dates are not the same - create new file";
const char locate[] PROGMEM = "Locating devices....";
const char parasitic[] PROGMEM = "Parasitic power is:";
const char reference[] PROGMEM = "The ref number is:";
const char noSD[] PROGMEM = "No SD card present";

#define MAX_STRING 80      // Sets the maximum length of string probably could be lower
char stringBuffer[MAX_STRING];  // A buffer to hold the string when pulled from program memory
//**************Thing's we'll write to the SD card****************

    long PvTot = 0;
    long DieselTot = 0;
    long InverterTot = 0;
    long WindDirTot = 0;
    long VoltageTot = 0;
    long WindCurrentTot = 0;
    long WindCurrentTwoTot = 0;
    long pulsecounterTot = 0;
//****************INITIALISE ROUTINE******************************


void setup()
{
  Serial.begin(9600);    // Set up a serial output for data display and changing parameters
  
  //******Real Time Clock Set - up********
  // A4 and A5 are used as I2C interface.
  // D2 is connected to CLK OUT from RTC. This triggers an interrupt to take data
  // We need to enable pull up resistors
  pinMode(A4, INPUT);           // set pin to input
  digitalWrite(A4, HIGH);       // turn on pullup resistors
  pinMode(A5, INPUT);           // set pin to input
  digitalWrite(A5, HIGH);       // turn on pullup resistors
  pinMode(2,INPUT);    // Set D2 to be an input for the RTC CLK-OUT   
  //initialise the real time clock
  Rtc_Pcf8563 rtc; 
  
  // Read the reference number from the EEROM
  ref1 = EEPROM.read(0);
  ref2 = EEPROM.read(1);
  
  // Read the sample Time value from EEPROM
  hiByte = EEPROM.read(2);
  loByte = EEPROM.read(3);
  sampleTime = (hiByte << 8)+loByte;  // Get the sensor calibrate value 
 
  initialiseSD();    // Inisitalise the SD card
  createfilename();  // Create the corrct filename (from date)

  //digitalWrite(3, HIGH); //Sets pull-up resistor which is required for the pulse to be counted.
  attachInterrupt(pulseinterrupt, pulse, RISING);  // This sets up our Interrupt Service Routine (ISR) for flow
  attachInterrupt(RTCinterrupt, RTC, FALLING);  // This sets up our Interrupt Service Routine (ISR) for RTC
  pinMode(LEDred,OUTPUT);    // Set D5 to be an output LED
  pinMode(cardDetect,INPUT);  // D6 is the SD card detect on pin 6.
  
    //Multiplexing select pins
    pinMode(select[0], OUTPUT); //set select pin as output
    pinMode(select[1], OUTPUT); //set select pin as output 
  
  // This section configures the RTC to have a 1Hz output.
  // Its a bit strange as first we read the data from the RTC
  // Then we load it back again but including the correct second flag  
  rtc.formatDate(RTCC_DATE_WORLD);
  rtc.formatTime();
  
  year_int = rtc.getYear();
  day_int = rtc.getDay();
  month_int = rtc.getMonth();  
  hour_int = rtc.getHour();
  min_int = rtc.getMinute();
  sec_int = rtc.getSecond(); 
  
  Wire.begin(); // Initiate the Wire library and join the I2C bus as a master
  Wire.beginTransmission(I2C_RTC); // Select RTC
  Wire.write(0);        // Start address
  Wire.write(0);     // Control and status 1
  Wire.write(0);     // Control and status 2
  Wire.write(DecToBcd(sec_int));     // Second
  Wire.write(DecToBcd(min_int));    // Minute
  Wire.write(DecToBcd(hour_int));    // Hour
  Wire.write(DecToBcd(day_int));    // Day
  Wire.write(DecToBcd(2));    // Weekday
  Wire.write(DecToBcd(month_int));     // Month (with century bit = 0)
  Wire.write(DecToBcd(year_int));    // Year
  Wire.write(0b10000000);    // Minute alarm (and alarm disabled)
  Wire.write(0b10000000);    // Hour alarm (and alarm disabled)
  Wire.write(0b10000000);    // Day alarm (and alarm disabled)
  Wire.write(0b10000000);    // Weekday alarm (and alarm disabled)
  Wire.write(0b10000011);     // Output clock frequency enabled (1 Hz) ***THIS IS THE IMPORTANT LINE**
  Wire.write(0);     // Timer (countdown) disabled
  Wire.write(0);     // Timer value
  Wire.endTransmission();
}

//*********** The flow interrupt**************
void pulse()
{
  if ((millis() - ContactTime) > 15) {  //debounce of REED contact. With 15ms speed of more than 150km/h can be measured
  // Increment the pulse counter
  pulsecounter++;
  ContactTime = millis();
}
}
//**************The RTC interrupt****************
// I use the CLK_OUT from the RTC to give me exact 1Hz signal
// To do this I changed the initialise the RTC with the CLKOUT at 1Hz
void RTC()
{ 
  dataCounter++;
  
  if(writedataflag==LOW&&dataCounter>=sampleTime)  // This stops us loosing data if a second is missed
  { 
    // If this interrupt has happened then we want to write data to SD card:
    // Save the pulsecounter value (this will be stored to write to SD card
    pulsecounterold = pulsecounter;
    // Reset the pulse counter
    pulsecounter = 0;
// Reset the DataCounter
//    dataCounter = 0;  
    // Set the writedataflag HIGH
    writedataflag=HIGH;
  }
}


void loop()
{

  // If the RTC has given us an interrupt then we need to write the data
  // Also want to make sure that there is an SD card present
  if(writedataflag==HIGH)
  {
     
    // Get inputs, via smoothing loops
    //My smoothing code is not pretty, or clever, but is necessary to achive stable readings from the allegro sensors
    long PvRawSum = 0;
    long DieselRawSum = 0;
    long InverterRawSum = 0;
    long WindDirRawSum = 0;
    long VoltageRawSum = 0;
    long WindCurrentRawSum = 0;
    long WindCurrentTwoRawSum = 0;
    
    for (int i=0; i<100; i++) //loop through 100 samples from each sensor, summing the results to a running total
     {
      
    digitalWrite(select[0], LOW);
    digitalWrite(select[1], LOW);
    PvRawSum += analogRead(MultiPin);

    digitalWrite(select[0], HIGH);
    DieselRawSum += analogRead(MultiPin);

    digitalWrite(select[0], LOW);
    digitalWrite(select[1], HIGH);
    
    WindDirRawSum += analogRead(MultiPin);
    digitalWrite(select[0], HIGH);
    InverterRawSum += analogRead(MultiPin);

    
    VoltageRawSum += analogRead(A1);  

 //   WindCurrentRawSum += (analogRead(A0));  //Callibrated sensor box 1
    WindCurrentRawSum += (analogRead(A0));  //Callibrated sensor box 1
    
    WindCurrentTwoRawSum += (analogRead(A2));

     }
    
    int PvRaw = PvRawSum / 100;
    int DieselRaw = DieselRawSum / 100;
    int InverterRaw = InverterRawSum / 100;
    int WindDirRaw = WindDirRawSum / 100;
    int VoltageRaw = VoltageRawSum / 100;
    int WindCurrentRaw = WindCurrentRawSum / 100;
    int WindCurrentTwoRaw = WindCurrentTwoRawSum / 100; 
    
     PvTot += (PvRaw);
     DieselTot += (DieselRaw);
     InverterTot += (InverterRaw);
     WindDirTot += (WindDirRaw);
     VoltageTot += (VoltageRaw);
     WindCurrentTot += (WindCurrentRaw);
     WindCurrentTwoTot += (WindCurrentTwoRaw);
     pulsecounterTot += pulsecounterold;
   
    newdate = String(rtc.formatDate(RTCC_DATE_WORLD));  
    if(newdate != date)
    {
       // If date has changed then create a new file
       //Serial.println(getString(dateerror)); 
       createfilename();  // Create the corrct filename (from date)
    }
      
    // Here we create a data string to write
    // Subtracting '0' converts from ASCII to real number
    // The headers are: "Time,Pulses,Voltage,Turbine1,Turbine2,Diesel,PV,Inverter,Wind Direction"
    dataString = 0; //String(housenumber);
    dataString += String(rtc.formatTime());
    dataString += comma;
    dataString += String(pulsecounterTot);  // Store the number of pulses from the previous second
    dataString += comma;
    dataString += String(VoltageTot);  // A0 analogue value (0 to 1024)
    dataString += comma;
    dataString += String(WindCurrentTot);  // A1 analogue value (0 to 1024)
    dataString += comma;
    dataString += String(WindCurrentTwoTot);  // A1 analogue value (0 to 1024)
    dataString += comma;
    dataString += String(DieselTot);  // A1 analogue value (0 to 1024)
    dataString += comma;
    dataString += String(PvTot);  // A1 analogue value (0 to 1024)
    dataString += comma;
    dataString += String(InverterTot);  // A2 analogue value (0 to 1024)
    dataString += comma;
    dataString += String(WindDirRaw);  // A3 analogue value (0 to 1024)
    
    
    //Create Realtime datastring
    dataRString = 0; //String(housenumber);
    dataRString += String(rtc.formatTime());
    dataRString += comma;
    dataRString += String(pulsecounterold);  // Store the number of pulses from the previous second
    dataRString += comma;
    dataRString += String(VoltageRaw);  // A0 analogue value (0 to 1024)
    dataRString += comma;
    dataRString += String(WindCurrentRaw);  // A1 analogue value (0 to 1024)
    dataRString += comma;
    dataRString += String(WindCurrentTwoRaw);  // A1 analogue value (0 to 1024)
    dataRString += comma;
    dataRString += String(DieselRaw);  // A1 analogue value (0 to 1024)
    dataRString += comma;
    dataRString += String(PvRaw);  // A1 analogue value (0 to 1024)
    dataRString += comma;
    dataRString += String(InverterRaw);  // A2 analogue value (0 to 1024)
    dataRString += comma;
    dataRString += String(WindDirRaw);  // A3 analogue value (0 to 1024)
    
    if(digitalRead(cardDetect)==LOW&&cardDetectOld==HIGH)
    {
      // There was no card previously so re-initialise and re-check the filename
      initialiseSD();
      createfilename();
    }
    
    if(digitalRead(cardDetect)==LOW&&cardDetectOld==LOW)
    {
      //Ensure that there is a card present)
      digitalWrite(LEDred, HIGH);   // set the LED ON
      // We then write the data to the SD card here:
      
      if(dataCounter >= 599)  //600 samples equates to 10 minutes so lets dump it to SD and reset everything 
        {
          dataCounter = 0;
          writetoSD();
          
        }
      else
        {
//          Serial.println(dataCounter);
         Serial.println(dataRString);
        }
        
      digitalWrite(LEDred, LOW);   // set the LED ON
    }
    else
    {
       Serial.println(getString(noSD));
      // print to the serial port too:
      Serial.println(dataString);
    }
    
    if(digitalRead(cardDetect)==HIGH)
    {
      // This meands there is no card present so set he LED high
       digitalWrite(LEDred, HIGH);   // set the LED ON
    }     

    cardDetectOld = digitalRead(cardDetect);  // Store the old value of the card detect
    writedataflag=LOW;    // Reset the write data flag
  }
  
  
}

//*********** FUNCTION TO INITIALISE THE SD CARD***************
void initialiseSD()
{
  Serial.println(getString(initialisesd));
  // make sure that the default chip select pin is set to
  // output, even if you don't use it:
  pinMode(chipSelect, OUTPUT);

  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    //Serial.println("FAIL");
    // don't do anything more:
    // Want to turn on an ERROR LED here
    return;
  }
}


// *********FUNCTION TO SORT OUT THE FILENAME**************
void createfilename()
{
  // Check there is a file created with the date in the title
  // If it does then create a new one with the new name
  // The name is created from:
  // RXXDMMDD.CSV, where XX is the reference, MM is the month, DD is the day
  // You must add on the '0' to convert to ASCII
  
  filename[1]=ref1;  // Convert from int to ascii
  filename[2]=ref2;  // Convert from int to ascii  
  date = String(rtc.formatDate());
  day_int = rtc.getDay();  // Get the actual day from the RTC
  month_int = rtc.getMonth();  // Get the month
  day_int1 = day_int/10;    // Find the first part of the integer
  day_int2 = day_int%10;    // Find the second part of the integer
  filename[4]=day_int1 + '0';  // Convert from int to ascii
  filename[5]=day_int2 + '0';  // Convert from int to ascii 
  month_int1 = month_int/10;    // Find the first part of the integer
  month_int2 = month_int%10;    // Find the second part of the integer
  filename[6]=month_int1 + '0';  // Convert from int to ascii
  filename[7]=month_int2 + '0';  // Convert from int to ascii   

  // Create the file and put a row of headers at the top
  if (! SD.exists(filename)) {
    // only open a new file if it doesn't exist
    datafile = SD.open(filename, FILE_WRITE);    // Open the correct file
    // if the file is available, write to it:
    if (datafile) {
      datafile.println(getString(headers));
      datafile.close();
      Serial.println(getString(headersOK));
    }  
    // if the file isn't open, pop up an error:
    else {
      Serial.println(getString(erroropen));
    }  
  }
}


// This routine writes the dataString to the SD card
void writetoSD()
{
  datafile = SD.open(filename, FILE_WRITE);    // Open the correct file
  // if the file is available, write to it:
  if (datafile) {
    datafile.println(dataString);
    datafile.close();
    // print to the serial port too:
    Serial.println(dataString);
    
    //reset totals
    
    
    PvTot = 0;
    DieselTot = 0;
    InverterTot = 0;
    WindDirTot = 0;
    VoltageTot = 0;
    WindCurrentTot = 0;
    WindCurrentTwoTot = 0;
    pulsecounterTot = 0;
  }  
  // if the file isn't open, pop up an error:
  else {
    Serial.println(getString(erroropen));
  }
}



// This routine pulls the string stored in program memory so we can use it
// It is temporaily stored in the stringBuffer
char* getString(const char* str) {
	strcpy_P(stringBuffer, (char*)str);
	return stringBuffer;
}


// Converts a decimal to BCD (binary coded decimal)
byte DecToBcd(byte value){
  return (value / 10 * 16 + value % 10);
}
