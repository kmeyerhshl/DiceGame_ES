/*Projekt: DiceGame
   Autor: Katharina Meyer
   Letztes Update: 23.02.2023
   Ziel: Würfelspiel zur Bewegungsförderung von Menschen jeden Alters
   Bestandteile: Schaumstoffwürfel mit eingebauter Hardware (3-Achsen-Beschleunigungssensor ADXL335 (Gy-61), SparkFun ESP32 Thing, LiPo-Akku (400 mAh)) und App
   Funktion: Die App steuert das Bewegungsspiel und sendet per Bluetooth Low Energy das Signal zum Starten und Stoppen des Spiels an den ESP32.
             Der Beschleunigungssensor misst die Werte an der X-, Y- und Z-Achse und filtert diese.
             Festgelegte Grenzwerte ermöglichen die Bestimmung der oben liegenden Würfelseite.
             Sobald die gefilterten Werte den Grenzwert überschreiten, wird eine Zeitmessung gestartet.
             Wenn der Zustand für mehr als fünf Sekunden anhält, wird per Bluetooth Low Energy die gewürfelte Seite an die App gesendet.
*/

//--- Bluetooth Low Energy ---
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include <CircularBuffer.h>

#define BLE_DEVICENAME      "CubeGame1"
#define SERVICE_UUID        "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"

#define BUFFERSIZE 3
#define DELAYTIME 1000


//--- Spielstatus ---
#define state_playing       "P" //"Plays"
#define state_not_playing   "S" //"PlaysNot"
#define state_finished_game "F" //"Finished"
#define state_aborted_game  "A" //"Aborted"


//--- Rundenanzahl ---
#define three_Rounds 3
#define five_Rounds 5
#define eleven_Rounds 11


//--- Eingänge des Beschleunigungssensors Gy-61 ---
int accZPin = 13;
int accYPin = 12;
int accXPin = 14;


//--- Variablen für Filter ---
const int AverageCount = 10;   // Anzahl der in den Laufenden-Mittelwert aufgenommenen Messungen
float AverageBuffer1[AverageCount];
float AverageBuffer2[AverageCount];
float AverageBuffer3[AverageCount];
int NextAverage1;
int NextAverage2;
int NextAverage3;
float AcXAverage = 0;
float AcYAverage = 0;
float AcZAverage = 0;


//--- Variablen für Zeitmessung ---
unsigned long zeit = 0;
unsigned long gewuerfelt1 = 0;
unsigned long gewuerfelt2 = 0;
unsigned long gewuerfelt3 = 0;
unsigned long gewuerfelt4 = 0;
unsigned long gewuerfelt5 = 0;
unsigned long gewuerfelt6 = 0;


//--- Boolean für Werteüberprüfung ---
boolean XUnten = false;
boolean XOben = false;
boolean YUnten = false;
boolean YOben = false;
boolean ZUnten = false;
boolean ZOben = false;


//--- Variable für Seitenerkennung ---
int seite = 0;


//--- Variablen zur Spielsteuerung ---
String gameStatus = state_not_playing;
bool play = false;


//--- Output Signal ---
String signalStop;
int roundStatus = 0;
int nrRounds = 0;



//------------------------------ BLE Verbindung ------------------------------
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      play = false;
      gameStatus = state_aborted_game;
    }
};



//------------------------------ Datenempfang ------------------------------
class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {

    // Diese Funktion wird angesprungen, wenn per BLE Daten empfangen werden
    // ---------------------------------------------------------------------
    void onWrite (BLECharacteristic *pCharacteristic) {
      String jsonString = pCharacteristic->getValue().c_str();
      Serial.println("String Received: " + jsonString);
      //--- JSON String auswerten ---
      StaticJsonDocument<1000> doc;
      deserializeJson(doc, jsonString);

      //--- Empfang des GameStatus ---
      String signalval = doc["GameStatus"];
      gameStatus = signalval;
      //--- Empfang der Rundenanzahl ---
      roundStatus = (int) doc["ROUND"];

      //--- Startsignal empfangen --> play wird true ---
      if (gameStatus == state_playing) {
        play = true;
      }

      //--- Endsignal empfangen --> play wird false, Seite wird zurück auf 0 gesetzt ---
      if (gameStatus == state_finished_game) {
        play = false;
        seite = 0;
        Serial.println("--------state_finished_game--------");
      }

      //--- Stoppsignal empfangen --> play wird false, Seite wird zurück auf 0 gesetzt ---
      if (gameStatus == state_not_playing) {
        play = false;
        seite = 0;
        Serial.println("--------state_not_playing--------");
      }
    };
};



//------------------------------ Setup ------------------------------
//--- Funktion wird einmalig durchgeführt ---
void setup() {
  Serial.begin(115200);

  uint16_t mtu = 128;
  BLEDevice::setMTU(128);

  // Create the BLE Device
  BLEDevice::init(BLE_DEVICENAME);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}



//------------------------------ Loop ------------------------------
void loop() {
  //--- disconnecting ---
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  //--- connecting ---
  if (deviceConnected && !oldDeviceConnected) {
    Serial.println("connecting");
    oldDeviceConnected = deviceConnected;
  }

  //--- wenn Gerät verbunden ist ---
  if (deviceConnected) {
    Serial.println("deviceConnected");
    //--- Rundenanzahl 3, 5 oder 11 wird empfangen --> Funktion zum Zurücksetzen der Variablen durchführen ---
    switch (roundStatus) {
      case three_Rounds:
        nrRounds = 3;
        Serial.println("Rundenanzahl: 3");
        resetVariables();
        break;
      case five_Rounds:
        nrRounds = 5;
        Serial.println("Rundenanzahl: 5");
        resetVariables();
        break;
      case eleven_Rounds:
        nrRounds = 11;
        Serial.println("Rundenanzahl: 11");
        resetVariables();
        break;
      default:
        //--- Rundenanzahl wurde bereits ausgewählt ---
        Serial.println("default true");
        play = true;
        resetVariables();
        break;
    }


    //---------- Spielvorgang ----------
    //--- solange play true ist und ein Gerät verbunden ist ---
    while (play && deviceConnected) {

      //--- an den Achsen empfangene Werte werden gefiltert ---
      filterX();
      filterY();
      filterZ();

      //--- serielle Ausgabe der gefilterten Werte ---
      Serial.print("X:");
      Serial.print(AcXAverage);
      Serial.print("\tY:");
      Serial.print(AcYAverage);
      Serial.print("\tZ:");
      Serial.println(AcZAverage);


      //--- Überprüfen, ob die gefilterten Werte einen Grenzwert überschreiten ---

      //--- wenn AcXAverage unter dem Grenzwert 1700 liegt ---
      if (AcXAverage < 1700) {
        //--- wenn XUnten false ist ---
        if (!XUnten) {
          //--- Zeitmessung starten ---
          gewuerfelt1 = millis();
          //--- XUnten wird true --> Funktion wird verlassen ---
          XUnten = true;
        }
        //--- wenn der Grenzwert länger als 5 Sekunden überschritten wird ---
        if (millis() - gewuerfelt1 > 5000) {
          //--- X zeigt nach unten --> Würfelseite 4 liegt oben ---
          seite = 4;
          Serial.println("X zeigt nach unten");
          //--- Variable für Zeitmessung zurücksetzen ---
          gewuerfelt1 = 0;
          //--- Funktion zur Datenübertragung starten ---
          notifyDataBLE();
        }
      } else {
        //--- wenn AcXAverage nicht mehr den Grenzwert überschreitet ---
        //--- Variable für Zeitmessung zurücksetzen ---
        //--- XUnten wird false ---
        XUnten = false;
        gewuerfelt1 = 0;
      }


      //--- wenn AcXAverage über dem Grenzwert 2250 liegt ---
      if (AcXAverage > 2250) {
        //--- wenn XOben false ist ---
        if (!XOben) {
          //--- Zeitmessung starten ---
          gewuerfelt2 = millis();
          //--- XOben wird true --> Funktion wird verlassen ---
          XOben = true;
        }
        //--- wenn der Grenzwert länger als 5 Sekunden überschritten wird ---
        if (millis() - gewuerfelt2 > 5000) {
          //--- X zeigt nach oben --> Würfelseite 3 liegt oben ---
          seite = 3;
          Serial.println("X zeigt nach oben");
          //--- Variable für Zeitmessung zurücksetzen ---
          gewuerfelt2 = 0;
          //--- Funktion zur Datenübertragung starten ---
          notifyDataBLE();
        }
      } else {
        //--- wenn AcXAverage nicht mehr den Grenzwert überschreitet ---
        //--- Variable für Zeitmessung zurücksetzen ---
        //--- XOben wird false ---
        XOben = false;
        gewuerfelt2 = 0;
      }


      //--- wenn AcYAverage unter dem Grenzwert 1700 liegt ---
      if (AcYAverage < 1700) {
        //--- wenn YUnten false ist ---
        if (!YUnten) {
          //--- Zeitmessung starten ---
          gewuerfelt3 = millis();
          //--- YUnten wird true --> Funktion wird verlassen ---
          YUnten = true;
        }
        //--- wenn der Grenzwert länger als 5 Sekunden überschritten wird ---
        if (millis() - gewuerfelt3 > 5000) {
          //--- Y zeigt nach unten --> Würfelseite 1 liegt oben ---
          seite = 1;
          Serial.println("Y zeigt nach unten");
          //--- Variable für Zeitmessung zurücksetzen ---
          gewuerfelt3 = 0;
          //--- Funktion zur Datenübertragung starten ---
          notifyDataBLE();
        }
      } else {
        //--- wenn AcYAverage nicht mehr den Grenzwert überschreitet ---
        //--- Variable für Zeitmessung zurücksetzen ---
        //--- YUnten wird false ---
        YUnten = false;
        gewuerfelt3 = 0;
      }


      //--- wenn AcYAverage über dem Grenzwert 2250 liegt ---
      if (AcYAverage > 2250) {
        //--- wenn YOben false ist ---
        if (!YOben) {
          //--- Zeitmessung starten ---
          gewuerfelt4 = millis();
          //--- YOben wird true --> Funktion wird verlassen ---
          YOben = true;
        }
        //--- wenn der Grenzwert länger als 5 Sekunden überschritten wird ---
        if (millis() - gewuerfelt4 > 5000) {
          //--- Y zeigt nach oben --> Würfelseite 6 liegt oben ---
          seite = 6;
          Serial.println("Y zeigt nach oben");
          //--- Variable für Zeitmessung zurücksetzen ---
          gewuerfelt4 = 0;
          //--- Funktion zur Datenübertragung starten ---
          notifyDataBLE();
        }
      } else {
        //--- wenn AcYAverage nicht mehr den Grenzwert überschreitet ---
        //--- Variable für Zeitmessung zurücksetzen ---
        //--- YOben wird false ---
        YOben = false;
        gewuerfelt4 = 0;
      }


      //--- wenn AcZAverage unter dem Grenzwert 1700 liegt ---
      if (AcZAverage < 1700) {
        //--- wenn ZOben false ist ---
        if (!ZOben) {
          //--- Zeitmessung starten ---
          gewuerfelt5 = millis();
          //--- ZOben wird true --> Funktion wird verlassen ---
          ZOben = true;
        }
        //--- wenn der Grenzwert länger als 5 Sekunden überschritten wird ---
        if (millis() - gewuerfelt5 > 5000) {
          //--- Z zeigt nach oben --> Würfelseite 2 liegt oben ---
          seite = 2;
          Serial.println("Z zeigt nach oben");
          //--- Variable für Zeitmessung zurücksetzen ---
          gewuerfelt5 = 0;
          //--- Funktion zur Datenübertragung starten ---
          notifyDataBLE();
        }
      } else {
        //--- wenn AcZAverage nicht mehr den Grenzwert überschreitet ---
        //--- Variable für Zeitmessung zurücksetzen ---
        //--- ZOben wird false ---
        ZOben = false;
        gewuerfelt5 = 0;
      }


      //--- wenn AcZAverage über dem Grenzwert 2250 liegt ---
      if (AcZAverage > 2250) {
        //--- wenn ZUnten false ist ---
        if (!ZUnten) {
          //--- Zeitmessung starten ---
          gewuerfelt6 = millis();
          //--- ZUnten wird true --> Funktion wird verlassen ---
          ZUnten = true;
        }
        //--- wenn der Grenzwert länger als 5 Sekunden überschritten wird ---
        if (millis() - gewuerfelt6 > 5000) {
          //--- Z zeigt nach unten --> Würfelseite 5 liegt oben ---
          seite = 5;
          Serial.println("Z zeigt nach unten");
          //--- Variable für Zeitmessung zurücksetzen ---
          gewuerfelt6 = 0;
          //--- Funktion zur Datenübertragung starten ---
          notifyDataBLE();
        }
      } else {
        //--- wenn AcZAverage nicht mehr den Grenzwert überschreitet ---
        //--- Variable für Zeitmessung zurücksetzen ---
        //--- ZUnten wird false ---
        ZUnten = false;
        gewuerfelt6 = 0;
      }
    }
  }
}



//------------------------------ Daten senden über BLE  ------------------------------
bool notifyDataBLE() {

  Serial.println("BLE Daten senden: notifyDataBLE()");

  //--- solange ein Gerät verbunden ist ---
  if (deviceConnected) {
    //--- Daten als JSON-Objekt kodieren, in JSON-String umwandeln und über BT senden ---
    StaticJsonDocument<400> doc;
    char jsonstring[400];

    //--- Seite und GameStatus senden ---
    doc["seite"] = seite;
    doc["playStatus"] = gameStatus;

    serializeJson(doc, jsonstring);

    Serial.print("sending jsonstring: ");
    Serial.println(jsonstring);

    pCharacteristic->setValue(jsonstring);
    pCharacteristic->notify();

  } else {
    return false;
  }
  return true;
  Serial.println("data was send via BLE");
}



//------------------------------ Variablen zurücksetzen ------------------------------
void resetVariables() {
  AcXAverage = 0;
  AcYAverage = 0;
  AcZAverage = 0;
  zeit = 0;
  gewuerfelt1 = 0;
  gewuerfelt2 = 0;
  gewuerfelt3 = 0;
  gewuerfelt4 = 0;
  gewuerfelt5 = 0;
  gewuerfelt6 = 0;
  seite = 0;
  XUnten = false;
  XOben = false;
  YUnten = false;
  YOben = false;
  ZUnten = false;
  ZOben = false;
}



//------------------------------ Filter ------------------------------
//--- Werte an den Achsen einlesen ---
//--- 10 Werte dem Array hinzufügen ---
//--- Werte addieren und durch 10 rechnen ---

void filterX() {
  AverageBuffer1[NextAverage1++] = analogRead(accXPin);
  if (NextAverage1 >= AverageCount) {
    NextAverage1 = 0;
  }

  for (int i = 0; i < AverageCount; ++i) {
    AcXAverage += AverageBuffer1[i];
  }
  AcXAverage /= AverageCount;
}

void filterY() {
  AverageBuffer2[NextAverage2++] = analogRead(accYPin);
  if (NextAverage2 >= AverageCount) {
    NextAverage2 = 0;
  }

  for (int i = 0; i < AverageCount; ++i) {
    AcYAverage += AverageBuffer2[i];
  }
  AcYAverage /= AverageCount;
}

void filterZ() {
  AverageBuffer3[NextAverage3++] = analogRead(accZPin);
  if (NextAverage3 >= AverageCount) {
    NextAverage3 = 0;
  }

  for (int i = 0; i < AverageCount; ++i) {
    AcZAverage += AverageBuffer3[i];
  }
  AcZAverage /= AverageCount;
}
