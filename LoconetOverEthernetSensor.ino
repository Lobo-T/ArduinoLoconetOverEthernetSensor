#include <SPI.h>
#include <Ethernet.h>

/* LoconetOcerTCP defaults to PORT 1234 */
#define TCP_PORT 1234
#define RX_BUF_SZ   50

//Startadresse.  Fire innganger på en adresse.  Verdi mellom 0 og 511
#define LOCONET_ADR 0

//Setter navn på pinnenummre
#define ut0 2
#define ut1 3
#define ut2 4
#define ut3 5
#define inn0 6
#define inn1 7
#define inn2 8
#define inn3 9
#define inn4 A0
#define inn5 A1
#define inn6 A2
#define inn7 A3


/* Husk å endre MAC adresse hvis du har flere arduinoer på samme nettverk */
byte mac[] = {
  0x10, 0x13, 0x37, 0x23, 0x13, 0x37
};
/* Sett også ip adresse hvis du ikke benytter DHCP.  Kanskje best å ikke bruke DHCP, siden JMRI trenger å vite adressen. */
byte ip[] = {
  10, 0, 100, 98
};
byte gateway[] = {
  10, 0, 0, 1
};
byte subnet[] = {
  255, 255, 0, 0
};


EthernetServer server(TCP_PORT);
EthernetClient client;

//De pinnene som er i denne arrayen vil bli satt som innganger
byte innportPins[] = {
  //Adr+0
  inn0,
  inn1,
  inn2,
  inn3,
  //Adr+1
  inn4,
  inn5,
  inn6,
  inn7
};

//De pinnene som er i denne arrayen vil bli satt som utganger
byte utportPins[] = {
  //Adr+0
  ut0,
  ut1,
  ut2,
  ut3
};

boolean innportState[sizeof(innportPins)];
boolean innportStateLast[sizeof(innportPins)];

char eth_rx_msg[RX_BUF_SZ];

boolean newmsg = false;


void setup() {
  Serial.begin(57600);

  //Sett pinmode
  for (byte i = 0; i < sizeof(innportPins); i++) {
    pinMode(innportPins[i], INPUT_PULLUP);
  }
  for (byte i = 0; i < sizeof(utportPins); i++) {
    pinMode(utportPins[i], OUTPUT);
  }

  /* initialise the ethernet device */
  //Ethernet.begin(mac, ip, gateway, subnet);   //Sett inn denne linjen for å alltid bruke statisk IP
  //DHCP med statisk fallback.  Kommenter bort den følgende blokken for å alltid bruke statisk IP
  Serial.println(F("Trying to get IP address via DHCP..."));
  if (Ethernet.begin(mac) == 0) {
    Serial.println(F("DHCP failed.  Using static IP"));
    Ethernet.begin(mac, ip, gateway, subnet);
  }
  //DHCP blokk slutt

  // Skriver ut IP adresse til seriellporten slik at vi vet hva vi har fått hvis vi bruker DHCP
  Serial.print(F("My IP address: "));
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    // print the value of each byte of the IP address:
    Serial.print(Ethernet.localIP()[thisByte], DEC);
    Serial.print(F("."));
  }
  Serial.println();


  //Vi leser pinnene og oppdaterer innportStateLast før vi starter hovedprogrammet.
  for (byte i = 0; i < sizeof(innportPins); i++) {
    innportState[i] = digitalRead(innportPins[i]);
  }
  memcpy(innportStateLast, innportState, sizeof(innportState));

  /*start listening for clients */
  server.begin();

  Serial.print(F("Setup end RAM:"));
  Serial.println(freeRam());
}


void loop() {
  client = server.available();

  //Receive message from client
  if (client) {
    int rxbuf_count = 0;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n') {
          eth_rx_msg[rxbuf_count] = 0;
          break;
        }
        if (c != '\r') {
          eth_rx_msg[rxbuf_count] = c;
          rxbuf_count++;
          if (rxbuf_count > sizeof(eth_rx_msg)) {
            //Buffer overrun. Abort.
            eth_rx_msg[sizeof(eth_rx_msg) - 1] = 0;
            break;
          }

        }
      }
    }
    newmsg = true;
    Serial.print(F("From eth:"));
    Serial.println(eth_rx_msg);
  }

  if (newmsg) {
    //Echo received message back to client.  Acknowledge with SENT OK.
    client.print(F("RECEIVE "));
    client.print(&eth_rx_msg[5]);
    client.print(F("\r\n"));
    client.print(F("SENT OK\r\n"));
    String msg = String(eth_rx_msg);

    /*
        OPC_SW_REQ
    */
    if (msg.startsWith("SEND B0")) {
      Serial.println(F("OPC_SW_REQ (B0)"));
      char hexbuf[3];
      msg.substring(8, 10).toCharArray(hexbuf, 3);
      byte sw1 = hex2byte(hexbuf);
      msg.substring(11, 13).toCharArray(hexbuf, 3);
      byte sw2 = hex2byte(hexbuf);

      byte function = sw1 & B00000011;
      unsigned int adress = ((sw2 & B00001111) << 5) | ((sw1 >> 2) & B00011111);
      boolean closed, on;
      Serial.print(F("Decoder adress:"));
      Serial.println(adress);
      Serial.print(F("Function:"));
      Serial.println(function);

      if ((sw2 & B00100000) != 0) {
        Serial.println(F("Closed"));
        closed = true;
      } else {
        Serial.println(F("Thrown"));
        closed = false;
      }
      if ((sw2 & B00010000) != 0) {
        Serial.println(F("ON"));
        on = true;
      } else {
        Serial.println(F("OFF"));
        on = false;
      }

      if ((adress >= LOCONET_ADR) && (adress <= ((sizeof(utportPins) - 1) / 4) + LOCONET_ADR)) {
        byte arrPos = function % 4 + (adress - LOCONET_ADR);
        Serial.print("Output pin no.:");
        Serial.print(utportPins[arrPos]);
        if (on) {
          if (closed) {
            digitalWrite(utportPins[arrPos], HIGH);
            Serial.println(" HIGH");
          } else {
            digitalWrite(utportPins[arrPos], LOW);
            Serial.println(" LOW");
          }
        }
      }

      /*
         JMRI sender forespørsler på adresse 254, som ser ut til å være en broadcast adresse, når den starter.
         Det kommer 8 meldinger med pause mellom som antakeligvis er for å spre Loconet trafikken som bli generert hvis man har mange sensorer.
         Siden vi nå kjører over ethernet og båndbredde ikke skulle være noe problem har jeg tenkt å bare lytte etter en av forespørslene
         og sende alle inngangene vi har uansett adresse.
      */
      if (adress == 254 && sw1 == 0x78 && sw2 == 0x07) {
        Serial.println("Report state");
        for (int i = 0; i < sizeof(innportPins); i++) {
          int adr = (i / 4) + LOCONET_ADR;
          byte port = i % 4;
          sendOPC_INPUT_REP(adr, port, innportState[i]);
        }
      }
      Serial.print(F("OPC_SW_REQ end RAM:"));
      Serial.println(freeRam());
      Serial.println("--------------------------------");
    }


    newmsg = false;
  }

  //Les pinner sjekk om de er endret fra sist gang vi leste
  for (byte i = 0; i < sizeof(innportPins); i++) {
    innportState[i] = digitalRead(innportPins[i]);

    if (innportState[i] != innportStateLast[i]) {
      Serial.print("\nUlik");
      Serial.println(innportPins[i]);
      int adr = (i / 4) + LOCONET_ADR;
      Serial.print("Adresse:");
      Serial.print(adr);
      byte port = i % 4;

      sendOPC_INPUT_REP(adr, port, innportState[i]);
      Serial.print(F("Input read end RAM:"));
      Serial.println(freeRam());
      Serial.println("--------------------------------");
    }
  }
  //Kopier det vi leste nå til det vi skal sammenligne med neste gang.
  memcpy(innportStateLast, innportState, sizeof(innportState));
}


void sendOPC_INPUT_REP(int adr, byte port, boolean innportState) {
  const byte OPCODE = 0xb2;
  byte in1 = 0;
  byte in2 = 0;
  byte adrl = adr;
  byte adrh = adr >> 8;

  Serial.print("\nadr:");
  Serial.print(adr);
  Serial.print("\nport:");
  Serial.print(port);
  Serial.print("\nlb:");
  Serial.print(adrl);
  Serial.print("\nhb:");
  Serial.print(adrh);
  Serial.print("\nstate:");
  Serial.print(innportState, BIN);

  //Bitmanipulasjon for å få det slik Loconet vil ha adressene
  in1 = adrl << 2;
  in1 = in1 | port;
  in1 = in1 & B01111111;  //bit7 alltid lavt

  in2 = innportState << 4;
  in2 = in2 | (adrh << 3) | ((adrl & B11100000) >> 5);
  in2 = in2 | B01000000;  //bit6 alltid satt

  Serial.print("\nIN1:");
  Serial.print(in1, BIN);
  Serial.print("\nIN2:");
  Serial.print(in2, BIN);
  byte cmdbytes[] = {OPCODE, in1, in2};
  byte checksum = calculateChecksum(cmdbytes);

  Serial.print(F("RECEIVE"));
  Serial.print(F(" "));
  Serial.print(OPCODE, HEX);
  Serial.print(F(" "));
  Serial.print(in1, HEX);
  Serial.print(F(" "));
  Serial.print(in2, HEX);
  Serial.print(F(" "));
  Serial.print(checksum, HEX);
  Serial.print(F("\n"));

  server.print(F("RECEIVE"));
  server.print(F(" "));
  server.print(OPCODE, HEX);
  server.print(F(" "));
  server.print(in1, HEX);
  server.print(F(" "));
  server.print(in2, HEX);
  server.print(F(" "));
  server.print(checksum, HEX);
  server.print(F("\r\n"));
  
  Serial.print(F("OPC_INPUT_END end RAM:"));
  Serial.println(freeRam());
}

byte calculateChecksum(byte msg[]) {
  byte checksum = 0xff;
  for (int i = 0; i < sizeof(msg); i++) {
    checksum ^= msg[i];
  }
  Serial.print(F("\ncsum:"));
  Serial.println(checksum);
  return checksum;
}

byte hex2byte(char* hexbuf) {
  byte retval = 0;
  byte hbyte = hex2nibble(hexbuf[0]);
  byte lbyte = hex2nibble(hexbuf[1]);
  hbyte = hbyte << 4;
  retval = hbyte | lbyte;
  return retval;
}

byte hex2nibble(char hchar) {
  switch (hchar) {
    case 'f':
    case 'F':
      return 15;
      break;
    case 'e':
    case 'E':
      return 14;
      break;
    case 'd':
    case 'D':
      return 13;
      break;
    case 'c':
    case 'C':
      return 12;
      break;
    case 'b':
    case 'B':
      return 11;
      break;
    case 'a':
    case 'A':
      return 10;
      break;
    case '9':
      return 9;
      break;
    case '8':
      return 8;
      break;
    case '7':
      return 7;
      break;
    case '6':
      return 6;
      break;
    case '5':
      return 5;
      break;
    case '4':
      return 4;
      break;
    case '3':
      return 3;
      break;
    case '2':
      return 2;
      break;
    case '1':
      return 1;
      break;
    case '0':
    default:
      return 0;
  }
}

int freeRam () {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

