#include "EWBServer.h"

// UUIDs únicos para o serviço e características. Gerados em https://www.uuidgenerator.net/
#define SERVICE_UUID           "287f2128-568f-46a5-9231-25a01f2fc48b"
#define JSON_VARS_CHAR_UUID    "c9f1b2cf-11de-4d3c-a05a-27a4490e1b47"
#define STREAM_DATA_CHAR_UUID  "c188ecce-30e4-4853-9ea8-b46b2d6012e9"
#define STREAM_CONTROL_CHAR_UUID "f544c35a-b6b8-49dd-8087-ccb1a8b4885b"

// --- Implementação da Classe EWBServer ---

EWBServer::EWBServer() {}

void EWBServer::begin(const char* deviceName, VariableConfig* vars, int numVars) {
  this->variables = vars;
  this->numVariables = numVars;

  Serial.println("Starting EWBServer BLE...");

  BLEDevice::init(deviceName);
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks(&this->clientConnected));
  BLEService *pService = pServer->createService(SERVICE_UUID);

  jsonVariablesCharacteristic = pService->createCharacteristic(
                                  JSON_VARS_CHAR_UUID,
                                  BLECharacteristic::PROPERTY_READ |
                                  BLECharacteristic::PROPERTY_WRITE
                                );
  jsonVariablesCharacteristic->setCallbacks(new JsonCharacteristicCallbacks(this));
  jsonVariablesCharacteristic->setValue("{}");

  streamDataCharacteristic = pService->createCharacteristic(
                               STREAM_DATA_CHAR_UUID,
                               BLECharacteristic::PROPERTY_NOTIFY
                             );
  streamDataCharacteristic->addDescriptor(new BLE2902());

  streamControlCharacteristic = pService->createCharacteristic(
                                  STREAM_CONTROL_CHAR_UUID,
                                  BLECharacteristic::PROPERTY_WRITE
                                );

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("EWBServer started. Waiting for a client connection...");
}

void EWBServer::setStreamCallbacks(StreamCallback onStart, StreamCallback onStop) {
  this->onStreamStartCallback = onStart;
  this->onStreamStopCallback = onStop;
  streamControlCharacteristic->setCallbacks(new StreamControlCallbacks(onStart, onStop));
}

void EWBServer::setOnVariableChangeCallback(VariableChangeCallback callback) {
    this->onVariableChange = callback;
}

void EWBServer::sendStreamData(const uint8_t* data, size_t length) {
  if (clientConnected) {
    streamDataCharacteristic->setValue((uint8_t*)data, length);
    streamDataCharacteristic->notify();
  }
}

bool EWBServer::isClientConnected() {
    return this->clientConnected;
}

void EWBServer::setPassword(const char* pwd) {
    this->expectedPassword = String(pwd);
    this->usePassword = true;
}

void EWBServer::handleJsonGet(JsonDocument& doc) {
  generateJsonState(doc);
}

void EWBServer::handleJsonSet(JsonDocument& doc) {
  JsonObject setObject = doc["set"];
  if (!setObject) return;

  for (int i = 0; i < numVariables; i++) {
    const char* varName = variables[i].name;
    if (setObject.containsKey(varName)) {
      JsonVariant val = setObject[varName];
      switch (variables[i].type) {
        case TYPE_INT:
          variables[i].intValue = val.as<int>();
          if (variables[i].useLimits) {
            variables[i].intValue = constrain(variables[i].intValue, variables[i].min, variables[i].max);
          }
          break;
        case TYPE_FLOAT:
          variables[i].floatValue = val.as<float>();
          if (variables[i].useLimits) {
            variables[i].floatValue = constrain(variables[i].floatValue, variables[i].min, variables[i].max);
          }
          break;
        case TYPE_STRING:
          strncpy(variables[i].stringValue, val.as<const char*>(), sizeof(variables[i].stringValue) - 1);
          variables[i].stringValue[sizeof(variables[i].stringValue) - 1] = '\0';
          break;
      }
      Serial.printf("Set variable '%s' updated.\n", varName);
      if (this->onVariableChange) {
        this->onVariableChange(varName);
      }
    }
  }
}

void EWBServer::generateJsonState(JsonDocument& doc) {
    doc.clear();
    for (int i = 0; i < numVariables; i++) {
        switch (variables[i].type) {
            case TYPE_INT:    doc[variables[i].name] = variables[i].intValue; break;
            case TYPE_FLOAT:  doc[variables[i].name] = variables[i].floatValue; break;
            case TYPE_STRING: doc[variables[i].name] = variables[i].stringValue; break;
        }
    }
}

MyServerCallbacks::MyServerCallbacks(bool* flag) : connectedFlag(flag) {}
void MyServerCallbacks::onConnect(BLEServer* pServer) { *connectedFlag = true; Serial.println("Client Connected"); }
void MyServerCallbacks::onDisconnect(BLEServer* pServer) { *connectedFlag = false; Serial.println("Client Disconnected"); BLEDevice::startAdvertising(); }

JsonCharacteristicCallbacks::JsonCharacteristicCallbacks(EWBServer* serverInstance) : server(serverInstance) {}

void JsonCharacteristicCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
  String value = pCharacteristic->getValue();
  if (value.length() > 0) {
    Serial.print("Received JSON: "); Serial.println(value);
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, value);
    if (error) { Serial.print("deserializeJson() failed: "); Serial.println(error.c_str()); return; }

    if (doc.containsKey("set")) {
      if (server->usePassword) {
        if (!doc.containsKey("auth") || doc["auth"].as<String>() != server->expectedPassword) {
          Serial.println("Auth failed! Ignoring set command.");
          return;
        }
      }
      server->handleJsonSet(doc);
    }
  }
}

void JsonCharacteristicCallbacks::onRead(BLECharacteristic* pCharacteristic) {
    StaticJsonDocument<512> doc;
    server->generateJsonState(doc);
    String jsonString;
    serializeJson(doc, jsonString);
    pCharacteristic->setValue(jsonString.c_str());
    Serial.print("Sent JSON state on read: "); Serial.println(jsonString);
}

StreamControlCallbacks::StreamControlCallbacks(StreamCallback onStart, StreamCallback onStop)
    : onStreamStart(onStart), onStreamStop(onStop) {}

void StreamControlCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
  String value = pCharacteristic->getValue();
  if (value.length() == 1) {
    if (value[0] == 0x01 && onStreamStart) { Serial.println("Stream Start command received."); onStreamStart(); } 
    else if (value[0] == 0x00 && onStreamStop) { Serial.println("Stream Stop command received."); onStreamStop(); }
  }
}