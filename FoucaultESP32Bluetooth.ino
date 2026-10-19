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
#include <Preferences.h>
#include "EWBServer.h"
#include "EWBSecret.h"

EWBServer ewbServer;
const char* DEVICE_NAME = "Foucault-Araras";

// --- Gerenciamento da Memória Flash (NVS) ---
Preferences preferences;
const char* PREFS_NAMESPACE = "foucault";

const uint8_t pinVprobe = 32;
const uint8_t pinVtip = 25;

const uint16_t freqPulse = 1380;   //[Hz]

const uint8_t NPdummy = 50;
const uint8_t NP = 1000;
uint8_t Np = 4;

const uint32_t T = 3460;  //pendulum period [ms]

uint32_t tp = 300;  //delay before pulse sequence [ms] 300
uint32_t ton = 300;  //pulse on duration [ms] 200
uint32_t toff = T/2 - ton;  //pulse off duration [ms]

const uint32_t interProbingDelay = 70; // [microsegundos]

float Von = 2.8; // [V] voltage on for coil full power (max: 3.3 V)
float Vprobing = 0.46; // [V] 0.49
float fp = 0.90;  // Fração relativa da tolerância da voltagem [adimensional] [0.90]

// --- Configuração das Variáveis Bluetooth ---
VariableConfig configurableVariables[] = {
  {"Np",       TYPE_INT,   Np,    0.0f,  "",  0,    32,   true},
  {"tp",       TYPE_INT,   tp,  0.0f,  "",  0,    1000, true},
  {"Von",      TYPE_FLOAT, 0,    Von,  "",  0.0f, 3.3f, true},
  {"Vprobing", TYPE_FLOAT, 0,    Vprobing, "",  0.0f, 0.6f, true},
  {"fp",       TYPE_FLOAT, 0,    fp, "",  0.01f, 0.99f, true},
  {"ton",      TYPE_INT,   ton,  0.0f,  "",  50,   500,  true}
};
const int numConfigurableVariables = sizeof(configurableVariables) / sizeof(configurableVariables[0]);

// Sincroniza o array de variáveis expostas ao Bluetooth com os valores atuais das variáveis
void syncConfigurableVariables() {
  configurableVariables[0].intValue   = Np;
  configurableVariables[1].intValue   = tp;
  configurableVariables[2].floatValue = Von;
  configurableVariables[3].floatValue = Vprobing;
  configurableVariables[4].floatValue = fp;
  configurableVariables[5].intValue   = ton;
}

// Carrega parâmetros da memória flash caso existam; caso contrário mantém os valores padrão do código
void loadParamsFromFlash() {
  if (!preferences.begin(PREFS_NAMESPACE, true)) {
    Serial.println("NVS: Nenhum registro prévio encontrado na flash. Mantendo parâmetros padrão do código.");
    return;
  }

  bool foundAny = false;

  if (preferences.isKey("Np")) {
    Np = preferences.getUChar("Np", Np);
    foundAny = true;
    Serial.printf("NVS: Carregado Np = %u\n", Np);
  }
  if (preferences.isKey("tp")) {
    tp = preferences.getUInt("tp", tp);
    foundAny = true;
    Serial.printf("NVS: Carregado tp = %u ms\n", tp);
  }
  if (preferences.isKey("Von")) {
    Von = preferences.getFloat("Von", Von);
    foundAny = true;
    Serial.printf("NVS: Carregado Von = %.2f V\n", Von);
  }
  if (preferences.isKey("Vprobing")) {
    Vprobing = preferences.getFloat("Vprobing", Vprobing);
    foundAny = true;
    Serial.printf("NVS: Carregado Vprobing = %.2f V\n", Vprobing);
  }
  if (preferences.isKey("fp")) {
    fp = preferences.getFloat("fp", fp);
    foundAny = true;
    Serial.printf("NVS: Carregado fp = %.2f\n", fp);
  }
  if (preferences.isKey("ton")) {
    ton = preferences.getUInt("ton", ton);
    toff = T/2 - ton;
    foundAny = true;
    Serial.printf("NVS: Carregado ton = %u ms (toff recalculado = %u ms)\n", ton, toff);
  }

  preferences.end();

  if (foundAny) {
    syncConfigurableVariables();
    Serial.println("NVS: Parâmetros restaurados da memória flash com sucesso.");
  } else {
    Serial.println("NVS: Nenhuma chave registrada. Mantendo parâmetros padrão do código.");
  }
}

// Grava um parâmetro individual na memória flash
void saveParamToFlash(const char* varName) {
  if (!preferences.begin(PREFS_NAMESPACE, false)) {
    Serial.println("NVS: Erro ao abrir namespace na flash para gravação!");
    return;
  }

  if (strcmp(varName, "Np") == 0) {
    preferences.putUChar("Np", Np);
  } else if (strcmp(varName, "tp") == 0) {
    preferences.putUInt("tp", tp);
  } else if (strcmp(varName, "Von") == 0) {
    preferences.putFloat("Von", Von);
  } else if (strcmp(varName, "Vprobing") == 0) {
    preferences.putFloat("Vprobing", Vprobing);
  } else if (strcmp(varName, "fp") == 0) {
    preferences.putFloat("fp", fp);
  } else if (strcmp(varName, "ton") == 0) {
    preferences.putUInt("ton", ton);
  }

  preferences.end();
  Serial.printf("NVS: Parâmetro '%s' gravado na flash.\n", varName);
}

void onVariableChanged(const char* varName) {
    if (strcmp(varName, "Np") == 0)       Np = (uint8_t)configurableVariables[0].intValue;
    if (strcmp(varName, "tp") == 0)       tp = (uint32_t)configurableVariables[1].intValue;
    if (strcmp(varName, "Von") == 0)      Von = configurableVariables[2].floatValue;
    if (strcmp(varName, "Vprobing") == 0) Vprobing = configurableVariables[3].floatValue;
    if (strcmp(varName, "fp") == 0)       fp = configurableVariables[4].floatValue;
    if (strcmp(varName, "ton") == 0)      {
        ton = (uint32_t)configurableVariables[5].intValue;
        toff = T/2 - ton;
    }
    Serial.printf("Variable %s updated via Bluetooth\n", varName);
    saveParamToFlash(varName);
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
  
  // Carrega parâmetros gravados na memória flash (NVS), com prioridade sobre os padrões
  loadParamsFromFlash();

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
