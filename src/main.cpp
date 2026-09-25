#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

// GPIO que controla a BASE do transistor 2N2222
const int rele = 26;

// Wi-Fi do simulador
const char* wifi = "Velxio-GUEST";
const char* senha = "";

// =============================
// HORARIOS DE IRRIGACAO
// =============================

// Horario 1: 06:00 ate 06:20
int inicio1 =8 * 60 +50;
int fim1    = 9 * 60;

// Horario 2: 13:30 ate 13:45
int inicio2 = 13 * 60 + 30;
int fim2    = 13 * 60 + 45;

// Horario 3: 18:00 ate 18:15
int inicio3 = 18 * 60;
int fim3    = 18 * 60 + 15;


void setup() {

  Serial.begin(115200);

  // GPIO 26 sera uma saida
  pinMode(rele, OUTPUT);

  // Comeca com o rele desligado
  digitalWrite(rele, LOW);

  Serial.println("Iniciando sistema de irrigacao...");

  // =============================
  // CONECTA AO WI-FI
  // =============================

  WiFi.begin(wifi, senha);

  Serial.println("Conectando ao Wi-Fi...");

  while (WiFi.status() != WL_CONNECTED) {

    Serial.println("Conectando...");
    delay(500);

  }

  Serial.println("Wi-Fi conectado!");

  // =============================
  // CONFIGURA HORARIO
  // Ceara/Brasilia = UTC-3
  // =============================

  configTime(
    -3 * 3600,
    0,
    "pool.ntp.org"
  );

  Serial.println("Sincronizando horario...");
}


void loop() {

  struct tm horario;

  // =============================
  // PEGA HORA ATUAL
  // =============================

  if (!getLocalTime(&horario)) {

    Serial.println("Erro ao obter horario!");

    // Seguranca:
    // se nao soubermos a hora,
    // mantemos a irrigacao desligada

    digitalWrite(rele, LOW);

    delay(1000);

    return;
  }


  int hora = horario.tm_hour;
  int minuto = horario.tm_min;
  int segundo = horario.tm_sec;


  // Transforma horario em minutos

  int horarioAtual =
      hora * 60 + minuto;


  // =============================
  // MOSTRA HORARIO NO SERIAL
  // =============================

  Serial.print("Hora atual: ");

  if (hora < 10) {
    Serial.print("0");
  }

  Serial.print(hora);
  Serial.print(":");

  if (minuto < 10) {
    Serial.print("0");
  }

  Serial.print(minuto);
  Serial.print(":");

  if (segundo < 10) {
    Serial.print("0");
  }

  Serial.println(segundo);


  // =============================
  // VERIFICA OS HORARIOS
  // =============================

  bool irrigar = false;


  // HORARIO 1

  if (
      horarioAtual >= inicio1 &&
      horarioAtual < fim1
     )
  {
    irrigar = true;
  }


  // HORARIO 2

  if (
      horarioAtual >= inicio2 &&
      horarioAtual < fim2
     )
  {
    irrigar = true;
  }


  // HORARIO 3

  if (
      horarioAtual >= inicio3 &&
      horarioAtual < fim3
     )
  {
    irrigar = true;
  }


  // =============================
  // CONTROLA O RELE
  // =============================

  if (irrigar == true) {

    // GPIO 26 HIGH
    // transistor liga
    // rele energiza
    // COM conecta com NO
    // LED/valvula liga

    digitalWrite(rele, HIGH);

    Serial.println("IRRIGACAO LIGADA");

  }

  else {

    // transistor desliga
    // rele desenergiza
    // LED/valvula desliga

    digitalWrite(rele, LOW);

    Serial.println("IRRIGACAO DESLIGADA");

  }


  Serial.println("-------------------------");

  delay(1000);
}