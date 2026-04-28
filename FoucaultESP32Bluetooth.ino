/*
Projeto pêndulo de Foucault.
Controle da bobina de reforço de oscilação.
João Teles (jteles@ufscar.br), 2023
*/

/*
Habilitando comunicação serial:
python3 -m pip install pyserial
sudo usermod -aG dialout joao
sudo chmod 666 /dev/ttyUSB0
*/

//Documentação: https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/en/latest/libraries.html#apis
#include "EWBServer.h"
#include "EWBSecret.h"

EWBServer ewbServer;
const char* DEVICE_NAME = "Foucault-Araras";

const uint8_t pinVprobe = 32;
const uint8_t pinVtip = 25;

const uint16_t freqPulse = 1380;   //[Hz]

const uint8_t NPdummy = 50;
const uint8_t NP = 1000;
uint8_t Np = 8;

const uint32_t T = 3460;  //pendulum period [ms]

uint32_t tp = 300;  //delay before pulse sequence [ms] 300
uint32_t ton = 200;  //pulse on duration [ms] 200
uint32_t toff = T/2 - ton;  //pulse off duration [ms]

const uint32_t interProbingDelay = 70; // [microsegundos]

float Von = 2.2; // [V] voltage on for coil full power (max: 3.3 V) 2.3
float Vprobing = 0.46; // [V] 0.49
float fp = 0.90;  // Fração relativa da tolerância da voltagem [adimensional] [0.90]

// --- Configuração das Variáveis Bluetooth ---
VariableConfig configurableVariables[] = {
  {"Np",       TYPE_INT,   8,    0.0f,  "",  0,    32,   true},
  {"tp",       TYPE_INT,   300,  0.0f,  "",  0,    1000, true},
  {"Von",      TYPE_FLOAT, 0,    2.2f,  "",  0.0f, 3.3f, true},
  {"Vprobing", TYPE_FLOAT, 0,    0.46f, "",  0.0f, 0.6f, true},
  {"fp",       TYPE_FLOAT, 0,    0.90f, "",  0.01f, 0.99f, true},
  {"ton",      TYPE_INT,   200,  0.0f,  "",  50,   500,  true}
};
const int numConfigurableVariables = sizeof(configurableVariables) / sizeof(configurableVariables[0]);

void onVariableChanged(const char* varName) {
    if (strcmp(varName, "Np") == 0)       Np = configurableVariables[0].intValue;
    if (strcmp(varName, "tp") == 0)       tp = (uint32_t)configurableVariables[1].intValue;
    if (strcmp(varName, "Von") == 0)      Von = configurableVariables[2].floatValue;
    if (strcmp(varName, "Vprobing") == 0) Vprobing = configurableVariables[3].floatValue;
    if (strcmp(varName, "fp") == 0)       fp = configurableVariables[4].floatValue;
    if (strcmp(varName, "ton") == 0)      {
        ton = (uint32_t)configurableVariables[5].intValue;
        toff = T/2 - ton;
    }
    Serial.printf("Variable %s updated via Bluetooth\n", varName);
}
float V; //voltagem

uint32_t probeReading, pastReading;

uint16_t i;

uint32_t tu0, t0;

bool firstReading = true;

const uint32_t Thalf = (uint32_t)round(1.0e6/(2*freqPulse));

uint8_t dacLevelFromVoltage(float voltage) {
  const float innerOffset = 0.1;
  uint8_t v = (uint8_t)round(voltage*255.0/3.3 + 0.5 - innerOffset);
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  return v;
}

void setup() {
  Serial.begin(115200);  
  dacWrite(pinVtip, dacLevelFromVoltage(0.0));    
  
  // Inicialização Bluetooth
  ewbServer.begin(DEVICE_NAME, configurableVariables, numConfigurableVariables);
  ewbServer.setOnVariableChangeCallback(onVariableChanged);
  ewbServer.setPassword(AUTH_PASSWORD);
  
  t0 = millis();
}

void loop() {

  if (firstReading) {
    probeReading = 0;
    tu0 = micros();
    for (i = 0; i < NPdummy; i++) {   //dummy scans, it takes 100us per loop
      V = Vprobing*(((micros()-tu0)/Thalf)%2);  //Square wave 
      dacWrite(pinVtip, dacLevelFromVoltage(V));
      probeReading += analogRead(pinVprobe);
      delayMicroseconds(random(interProbingDelay));  
    }
  }
  probeReading = 0;
  for (i = 0; i < NP; i++) {      //valid scans, it takes 100us per loop
    V = Vprobing*(((micros()-tu0)/Thalf)%2);  //Square wave 
    dacWrite(pinVtip, dacLevelFromVoltage(V));
    probeReading += analogRead(pinVprobe);
    delayMicroseconds(random(interProbingDelay));  
  }
  if (!firstReading) {    
    if (probeReading < fp*pastReading) {          
      delay(tp);
      for (i = 0; i < Np; i++) {
        dacWrite(pinVtip, dacLevelFromVoltage(0.0));      
        delay(toff);
        dacWrite(pinVtip, dacLevelFromVoltage(Von));
        delay(ton);
      }
      firstReading = true;
    }
  }
  else firstReading = false;
  pastReading = probeReading;
}
