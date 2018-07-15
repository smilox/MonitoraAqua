#include <WiFi.h> //lib para configuração do Wifi
#include <ArduinoOTA.h> //lib do ArduinoOTA 
#include <ESPmDNS.h> //lib necessária para comunicação network
#include <WiFiUdp.h> //lib necessária para comunicação network

/*  ########## TASK ##########   */
//variaveis que indicam o núcleo
static uint8_t taskCoreZero = 0;
static uint8_t taskCoreOne  = 1;

/*  ########## MQTT ##########   */
#include <PubSubClient.h>

//constantes e variáveis globais
long lastConnectionTime;
char EnderecoAPIThingSpeak[] = "api.thingspeak.com";
String ChaveEscritaThingSpeak = "TXP64G9YUISIWKOC";
#define INTERVALO_ENVIO_THINGSPEAK  1000 * 60/*(segundos)*/ * 30/*(minutos)*/  //intervalo entre envios de dados ao ThingSpeak (em ms)
//WiFiClient client;


/*  ########## Sensor Temperatura ##########   */
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 14 // Data wire is plugged into GPIO 14 on the ESP32
#define TEMPERATURE_PRECISION 12 // Lower resolution
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
int numberOfDevices;
DeviceAddress tempDeviceAddress;

/*  ######### Visor OLED ######### */
#include "SSD1306.h"
#define SDA_PIN 5// GPIO5 -> SDA
#define SCL_PIN 4// GPIO4 -> SCL
#define SSD_ADDRESS 0x3c
SSD1306  display(SSD_ADDRESS, SDA_PIN, SCL_PIN);


const char* ssid = "198"; //nome da rede
const char* password = "biazzi_18"; //senha da rede

hw_timer_t *timer = NULL; //faz o controle do temporizador (interrupção por tempo)

float Temperatura;
int ErroCount = 0;
int contador = 0;

//PROTOTYPES
void IRAM_ATTR resetModule();
void startOTA();
void endOTA();
void errorOTA(ota_error_t error);
void printAddress(DeviceAddress deviceAddress);
void getTemp();
void drawProgressBar();
void THINGSPEAKRequest(float field1Data);
void pubTemp( void * pvParameters );

void setup()
{

  Serial.begin(9600);

  Serial.println("Booting");

  display.init(); // Available default fonts: ArialMT_Plain_10, ArialMT_Plain_16, ArialMT_Plain_24
  display.setFont(ArialMT_Plain_10);
  display.drawString(7, 5,  "Inicializando...");
  display.display();
  //define wifi como station (estação)
  WiFi.mode(WIFI_STA);

  //inicializa wifi
  WiFi.begin(ssid, password);
  display.drawString(7, 15,  "WiFi:");
  display.display();
  //enquanto o wifi não for conectado aguarda
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    //caso falha da conexão, reinicia wifi
    Serial.println("Connection Failed! Rebooting...");

    delay(5000);
    ESP.restart();
  }
  display.drawString(35, 15,  "OK");
  display.display();
  ArduinoOTA.setHostname("myesp32");
  ArduinoOTA.setPassword("123");

  ArduinoOTA.onStart( startOTA ); //startOTA é uma função criada para simplificar o código
  ArduinoOTA.onEnd( endOTA ); //endOTA é uma função criada para simplificar o código
  ArduinoOTA.onProgress( progressOTA ); //progressOTA é uma função criada para simplificar o código
  ArduinoOTA.onError( errorOTA );//errorOTA é uma função criada para simplificar o código
  ArduinoOTA.begin();

  //exibe pronto e o ip utilizado pelo ESP
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());


  display.drawString(7, 25,  "Sensor Temp:");
  display.display();
  // Start up the library
  sensors.begin();

  // Grab a count of devices on the wire
  numberOfDevices = sensors.getDeviceCount();

  // locate devices on the bus
  Serial.print("Locating devices...");

  Serial.print("Found ");
  Serial.print(numberOfDevices, DEC);
  Serial.println(" devices.");

  // report parasite power requirements
  Serial.print("Parasite power is: ");
  if (sensors.isParasitePowerMode()) Serial.println("ON");
  else Serial.println("OFF");

  // Loop through each device, print out address
  for (int i = 0; i < numberOfDevices; i++)
  {
    // Search the wire for address
    if (sensors.getAddress(tempDeviceAddress, i))
    {
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: ");
      printAddress(tempDeviceAddress);
      Serial.println();

      Serial.print("Setting resolution to ");
      Serial.println(TEMPERATURE_PRECISION, DEC);

      // set the resolution to TEMPERATURE_PRECISION bit (Each Dallas/Maxim device is capable of several different resolutions)
      sensors.setResolution(tempDeviceAddress, TEMPERATURE_PRECISION);

      Serial.print("Resolution actually set to: ");
      Serial.print(sensors.getResolution(tempDeviceAddress), DEC);
      Serial.println();
    } else {
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");
    }
  }
  display.drawString(80, 25,  "OK");
  display.display();


  for (int i = 0; i <= 100; i++) {
    // desenha a progress bar
    /*
       drawProgressBar(x, y, width, height, value);
      parametros (p):
        p1: x       --> coordenada X no plano cartesiano
        p2: y       --> coordenada Y no plano cartesiano
        p3: width   --> comprimento da barra de progresso
        p4: height  --> altura da barra de progresso
        p5: value   --> valor que a barra de progresso deve assumir

    */
    display.drawProgressBar(10, 40, 100, 10, i);
    display.display();

    delay(50);
  }

  //cria uma tarefa que será executada na função coreTaskZero, com prioridade 1 e execução no núcleo 0
  //coreTaskZero: piscar LED e contar quantas vezes
  xTaskCreatePinnedToCore(
    pubTemp,   /* função que implementa a tarefa */
    "pubTemp", /* nome da tarefa */
    10000,      /* número de palavras a serem alocadas para uso com a pilha da tarefa */
    NULL,       /* parâmetro de entrada para a tarefa (pode ser NULL) */
    5,          /* prioridade da tarefa (0 a N) */
    NULL,       /* referência para a tarefa (pode ser NULL) */
    taskCoreZero);         /* Núcleo que executará a tarefa */

  delay(500); //tempo para a tarefa iniciar
  /* ########## Watchdog ########## */
  /*     hw_timer_t * timerBegin(uint8_t num, uint16_t divider, bool countUp);
     num: é a ordem do temporizador. Podemos ter quatro temporizadores, então a ordem pode ser [0,1,2,3].
    divider: É um prescaler (reduz a frequencia por fator). Para fazer um agendador de um segundo,
    usaremos o divider como 80 (clock principal do ESP32 é 80MHz). Cada instante será T = 1/(80) = 1us
    countUp: True o contador será progressivo
  */
  timer = timerBegin(0, 80, true); //timerID 0, div 80
  //timer, callback, interrupção de borda
  timerAttachInterrupt(timer, &resetModule, true);
  //timer, tempo (us), repetição
  timerAlarmWrite(timer, 20 * 1000000, true); // Microsegundo aguardar 10 segundos
  timerAlarmEnable(timer); //habilita a interrupção
  timerWrite(timer, 0); //reseta o temporizador (alimenta o watchdog)
}

//#######################################################################################

void IRAM_ATTR resetModule() {
  ets_printf("(watchdog) reiniciar\n"); //imprime no log
  esp_restart_noos(); //reinicia o chip
}

//funções de exibição dos estágios de upload (start, progress, end e error) do ArduinoOTA
void startOTA()
{
  String type;

  //caso a atualização esteja sendo gravada na memória flash externa, então informa "flash"
  if (ArduinoOTA.getCommand() == U_FLASH)
    type = "flash";
  else  //caso a atualização seja feita pela memória interna (file system), então informa "filesystem"
    type = "filesystem"; // U_SPIFFS

  //exibe mensagem junto ao tipo de gravação
  Serial.println("Start updating " + type);
}

//exibe mensagem
void endOTA()
{
  Serial.println("\nEnd");
}

//exibe progresso em porcentagem
void progressOTA(unsigned int progress, unsigned int total)
{
  timerWrite(timer, 0); //reseta o temporizador (alimenta o watchdog)

  display.clear();
  display.setFont(ArialMT_Plain_10);
  contador = progress / (total / 100);
  drawProgressBar();
  display.display();

  Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
}

//caso aconteça algum erro, exibe especificamente o tipo do erro
void errorOTA(ota_error_t error)
{
  Serial.printf("Error[%u]: ", error);

  if (error == OTA_AUTH_ERROR)
    Serial.println("Auth Failed");
  else if (error == OTA_BEGIN_ERROR)
    Serial.println("Begin Failed");
  else if (error == OTA_CONNECT_ERROR)
    Serial.println("Connect Failed");
  else if (error == OTA_RECEIVE_ERROR)
    Serial.println("Receive Failed");
  else if (error == OTA_END_ERROR)
    Serial.println("End Failed");
}
// function to print a device address
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

//função para setar a temperatra
void getTemp()
{

}

//função para desenhar a progress bar no display
void drawProgressBar() {

  Serial.print(">> ");
  Serial.println(contador);

  // desenha a progress bar
  /*
     drawProgressBar(x, y, width, height, value);
    parametros (p):
      p1: x       --> coordenada X no plano cartesiano
      p2: y       --> coordenada Y no plano cartesiano
      p3: width   --> comprimento da barra de progresso
      p4: height  --> altura da barra de progresso
      p5: value   --> valor que a barra de progresso deve assumir

  */
  display.drawProgressBar(10, 32, 100, 10, contador);

  // configura o alinhamento do texto que será escrito
  //nesse caso alinharemos o texto ao centro
  display.setTextAlignment(TEXT_ALIGN_CENTER);

  //escreve o texto de porcentagem
  /*
     drawString(x,y,text);
    parametros (p):
     p1: x      --> coordenada X no plano cartesiano
     p2: y      --> coordenada Y no plano cartesiano
     p3: string --> texto que será exibido
  */
  display.drawString(64, 15, String(contador) + "%");

  //se o contador está em zero, escreve a string "valor mínimo"
  if (contador == 0) {
    display.drawString(64, 45, "Atualizando");
  }
  //se o contador está em 100, escreve a string "valor máximo"
  else if (contador == 100) {
    display.drawString(64, 45, "Atualizado");
  }
}
//Função: envia informações ao ThingSpeak
//Parâmetros: String com a  informação a ser enviada
//Retorno: nenhum

void THINGSPEAKRequest(float field1Data) {

  WiFiClient client;

  if (!client.connect(EnderecoAPIThingSpeak, 80)) {

    Serial.println("connection failed");
    lastConnectionTime = millis();
    client.stop();
    return;
  }

  else {

    // create data string to send to ThingSpeak
    String data = "field1=" + String(field1Data); //shows how to include additional field data in http post

    // POST data to ThingSpeak
    if (client.connect(EnderecoAPIThingSpeak, 80)) {

      client.println("POST /update HTTP/1.1");
      client.println("Host: api.thingspeak.com");
      client.println("Connection: close");
      client.println("User-Agent: ESP32WiFi/1.1");
      client.println("X-THINGSPEAKAPIKEY: " + ChaveEscritaThingSpeak);
      client.println("Content-Type: application/x-www-form-urlencoded");
      client.print("Content-Length: ");
      client.print(data.length());
      client.print("\n\n");
      client.print(data);

      Serial.println("RSSI = " + String(field1Data));
      lastConnectionTime = millis();
    }
  }
  client.stop();
}

void pubTemp( void * pvParameters ) {

  String taskMessage = "Task running on core ";
  taskMessage = taskMessage + xPortGetCoreID();
  Serial.println(taskMessage);  //log para o serial monitor

  while (true) {
     sensors.requestTemperatures();
  delay(1000);
  float oldTemp = Temperatura;
  Temperatura = sensors.getTempC(tempDeviceAddress);
  
  if (Temperatura == -127) {
    Serial.println("Problema de Leitura no sensor de temperatura");

    //display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);
    display.drawString(30, 45, "Erro de Leitura");
    display.display();
    delay(2000);

    ErroCount ++;
    Temperatura = oldTemp;
  } else {
    ErroCount = 0;
  }

  if (ErroCount >= 5) {
    Serial.print("Reiniciando por erro no sensor de temperatura");
    Serial.println("");
    esp_restart_noos(); //reinicia o chip
  }
  }
}

//#######################################################################################

void loop()
{
  char FieldUmidade[20];

  timerWrite(timer, 0); //reseta o temporizador (alimenta o watchdog)

  //getTemp();

  //Handle é descritor que referencia variáveis no bloco de memória
  //Ele é usado como um "guia" para que o ESP possa se comunicar com o computador pela rede
  ArduinoOTA.handle();

  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(7, 5,  "Temp: " + String(Temperatura) + "ºC");
  //display.drawString(20, 31,  String(buff));
  display.display();

  //verifica se está conectado no WiFi e se é o momento de enviar dados ao ThingSpeak
  if ((millis() - lastConnectionTime) > INTERVALO_ENVIO_THINGSPEAK)
  {
    THINGSPEAKRequest(Temperatura);
  }
  delay(1000);

}
