#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define MAX_PERIODOS 10

//definição dos pinos
#define rele 26
#define botao 27
#define led 12

// Wi-Fi do simulador
const char* wifi = "Velxio-GUEST";
const char* senha = "";

const char* TB_HOST = "thingsboard.cloud";
const int TB_PORT = 1883;
const char* TB_TOKEN = "";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// -------- ESTRUTURA DOS PERÍODOS --------

// Os horários são armazenados como minutos
// decorridos desde a meia-noite.

struct Periodo {
    int inicio;
    int fim;
};

Periodo periodos[MAX_PERIODOS];
int quantidadePeriodos = 0;

bool travado;
bool ledOnline = false;
bool ligado = false;
bool manualAtivo = false;
bool estadoBotaoAnterior = LOW;
bool configuracaoRecebida = false;

void enviarestadoControle();

// -------- CONVERSÃO DE HORÁRIOS --------

bool converterHorario(const char* texto, int& minutos) {
    if (texto == nullptr) {
        return false;
    }

    // Exige exatamente o formato HH:MM.
    if (strlen(texto) != 5 || texto[2] != ':') {
        return false;
    }

    for (int i : {0, 1, 3, 4}) {
        if (texto[i] < '0' || texto[i] > '9') {
            return false;
        }
    }

    int hora = (texto[0] - '0') * 10
             + (texto[1] - '0');

    int minuto = (texto[3] - '0') * 10
               + (texto[4] - '0');

    if (hora > 23 || minuto > 59) {
        return false;
    }

    minutos = hora * 60 + minuto;
    return true;
}

// -------- INTERPRETAÇÃO DO JSON --------

bool interpretarPeriodos(JsonVariantConst valor) {
    // Aceita tanto um array JSON quanto
    // uma string contendo um array JSON.

    JsonDocument documentoTexto;

    if (valor.is<const char*>()) {
        DeserializationError erro = deserializeJson(
            documentoTexto,
            valor.as<const char*>()
        );

        if (erro) {
            Serial.println("JSON de periodos invalido.");
            return false;
        }

        valor = documentoTexto.as<JsonVariantConst>();
    }

    configuracaoRecebida = true;

    if (!valor.is<JsonArrayConst>()) {
        Serial.println("Periodos deve ser uma lista.");
        return false;
    }

    JsonArrayConst lista = valor.as<JsonArrayConst>();

    if (lista.size() > MAX_PERIODOS) {
        Serial.println("Quantidade de periodos excedida.");
        return false;
    }

    // Primeiro valida tudo em um vetor temporário.
    // Só substitui os horários atuais se tudo
    // estiver correto.

    Periodo novos[MAX_PERIODOS];
    int quantidade = 0;

    for (JsonVariantConst item : lista) {
        if (!item.is<JsonObjectConst>()) {
            return false;
        }

        const char* inicio = item["inicio"];
        const char* fim = item["fim"];

        int minutoInicio;
        int minutoFim;

        if (!converterHorario(inicio, minutoInicio) ||
            !converterHorario(fim, minutoFim)) {
            Serial.println("Horario invalido.");
            return false;
        }

        if (minutoInicio == minutoFim) {
            Serial.println("Inicio e fim iguais.");
            return false;
        }

        novos[quantidade].inicio = minutoInicio;
        novos[quantidade].fim = minutoFim;

        quantidade++;
    }

    // Todos os períodos são válidos.
    quantidadePeriodos = quantidade;

    for (int i = 0; i < quantidade; i++) {
        periodos[i] = novos[i];
    }

    Serial.printf(
        "Configuracao recebida: %d periodos\n",
        quantidadePeriodos
    );

    for (int i = 0; i < quantidadePeriodos; i++) {
        Serial.printf(
            "Periodo %d: %d ate %d minutos\n",
            i + 1,
            periodos[i].inicio,
            periodos[i].fim
        );
    }

    return true;
}

// -------- RECEBIMENTO MQTT --------

void enviarEstadoControle() {

    String mensagem = "{\"manualAtivo\":";
    mensagem += manualAtivo ? "true" : "false";

    mensagem += ",\"ligado\":";
    mensagem += ligado ? "true" : "false";

    mensagem += "}";

    bool sucesso = mqtt.publish(
        "v1/devices/me/attributes",
        mensagem.c_str()
    );

    Serial.print("Estado enviado: ");
    Serial.println(mensagem);

    Serial.print("Publish: ");
    Serial.println(
        sucesso ? "OK" : "FALHOU"
    );
}

void receberMensagem(char* topico, byte* payload, unsigned int tamanho)
{
    Serial.println("=== MQTT RECEBIDO ===");

    Serial.print("Topico: ");
    Serial.println(topico);

    Serial.print("Mensagem: ");
    Serial.write(payload, tamanho);
    Serial.println();

    JsonDocument documento;

    DeserializationError erro = deserializeJson(
        documento,
        payload,
        tamanho
    );

    if (erro) {
        Serial.println("Erro ao interpretar mensagem MQTT.");
        return;
    }

    // =========================
    // VERIFICA SE É UM RPC
    // =========================

    const char* metodo = documento["method"];

    if (metodo != nullptr) {

        // =========================
        // MODO MANUAL
        // =========================

        if (strcmp(metodo, "modoManual") == 0) {

            manualAtivo = true;

            Serial.println("RPC: modo manual ativado");

            enviarEstadoControle();

            return;
        }

        // =========================
        // MODO AUTOMÁTICO
        // =========================

        if (strcmp(metodo, "modoAutomatico") == 0) {

            manualAtivo = false;
            ligado = false;

            digitalWrite(rele, LOW);

            Serial.println("RPC: modo automatico ativado");
            Serial.println("Valvula desligada.");

            return;
        }

        // =========================
        // LIGA/DESLIGA VÁLVULA
        // =========================

        if (strcmp(metodo, "toggleValvula") == 0) {

            if (manualAtivo && !travado) {

                ligado = !ligado;

                Serial.println(
                    ligado
                    ? "RPC: VALVULA LIGADA"
                    : "RPC: VALVULA DESLIGADA"
                );

                enviarEstadoControle();

            } else {

                Serial.println(
                    "RPC: comando ignorado"
                );
            }

            return;
        }
    }

    // =========================
    // ATRIBUTOS
    // =========================

    JsonVariantConst raiz =
        documento.as<JsonVariantConst>();

    JsonVariantConst dados =
        raiz["shared"].is<JsonObjectConst>()
        ? raiz["shared"]
        : raiz;

    // =========================
    // PERÍODOS
    // =========================

    if (!dados["periodos"].isNull()) {

        interpretarPeriodos(
            dados["periodos"]
        );
    }

    // =========================
    // TRAVAMENTO
    // =========================

    if (dados["travado"].is<bool>()) {

        travado =
            dados["travado"].as<bool>();

        Serial.print("travado recebido: ");
        Serial.println(
            travado ? "TRUE" : "FALSE"
        );
    }

    // =========================
    // MODO MANUAL/AUTOMÁTICO
    // =========================

    if (dados["manualAtivo"].is<bool>()) {

        manualAtivo =
            dados["manualAtivo"].as<bool>();

        Serial.print("manualAtivo recebido: ");

        Serial.println(
            manualAtivo
            ? "TRUE - MANUAL"
            : "FALSE - AUTOMATICO"
        );

        // Ao receber automático pelo Shared Attribute,
        // desliga imediatamente a válvula.
        if (!manualAtivo) {

            ligado = false;
            digitalWrite(rele, LOW);

            Serial.println(
                "Modo automatico recebido."
            );

            Serial.println(
                "Valvula desligada."
            );
        }
    }
}

// -------- ENVIO MQTT --------

void enviarLedOnline() {
    String mensagem = "{\"ledOnline\":";
    mensagem += ledOnline ? "true" : "false";
    mensagem += "}";

    mqtt.publish(
        "v1/devices/me/attributes",
        mensagem.c_str()
    );
}

// -------- CONEXÃO THINGSBOARD --------

void conectarThingsBoard() {
    while (!mqtt.connected()) {
        if (WiFi.status() != WL_CONNECTED) {
            return;
        }

        Serial.println("Conectando ao ThingsBoard...");

        if (mqtt.connect(
            "ESP32_Irrigacao",
            TB_TOKEN,
            nullptr
        )) {
            Serial.println("ThingsBoard conectado.");

            enviarEstadoControle();

            // Alterações feitas no dashboard.
            mqtt.subscribe(
                "v1/devices/me/attributes"
            );

            // Respostas às solicitações de atributos.
            mqtt.subscribe(
                "v1/devices/me/attributes/response/+"
            );

            mqtt.subscribe(
                "v1/devices/me/rpc/request/+"
            );

            // Recupera os períodos já salvos.
            mqtt.publish(
                "v1/devices/me/attributes/request/1",
                "{\"sharedKeys\":\"periodos,travado\"}"
            );

        } else {
            Serial.printf(
                "Falha MQTT: %d\n",
                mqtt.state()
            );

            delay(5000);
        }
    }
}

// -------- VERIFICAÇÃO DOS PERÍODOS --------

bool estaDentroDeAlgumPeriodo(int horarioAtual) {
    for (int i = 0; i < quantidadePeriodos; i++) {
        int inicio = periodos[i].inicio;
        int fim = periodos[i].fim;

        if (inicio < fim) {
            // Exemplo: 08:00 até 12:00
            if (horarioAtual >= inicio && horarioAtual < fim) {
                return true;
            }
        } else {
            // Exemplo: 22:00 até 06:00
            if (horarioAtual >= inicio || horarioAtual < fim) {
                return true;
            }
        }
    }

    return false;
}

//FUNCIONAMENTO DO BOTÃO

void alternarManual() {

    manualAtivo = true;

    ligado = !digitalRead(rele);

    Serial.println("Modo manual ativado.");

    enviarEstadoControle();
}

void ativarAutomatico() {
    manualAtivo = false;

    Serial.println("Modo automatico ativado");

    enviarEstadoControle();
}

void fBotao() {

    
    bool estadoBotao = digitalRead(botao);

    // Detecta apenas o momento em que o botão é pressionado
    if (estadoBotao == HIGH && estadoBotaoAnterior == LOW) {

        alternarManual();
        enviarEstadoControle();
        delay(50);  // debounce simples
    }

    estadoBotaoAnterior = estadoBotao;
  
}

// FUNCIONAMENTO DO LED
void fLeds() {
if(digitalRead(rele)==HIGH){
    digitalWrite(led, HIGH);
    ledOnline = true;
}else{
    digitalWrite(led, LOW);
    ledOnline = false;
}
enviarLedOnline();
}

void setup() {

  Serial.begin(115200);

  // GPIO 26 sera uma saida
  pinMode(rele, OUTPUT);
  pinMode(botao, INPUT_PULLDOWN);
  pinMode(led, OUTPUT);

  // Comeca com o rele desligado
  digitalWrite(rele, LOW);
  estadoBotaoAnterior=digitalRead(botao);

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

struct tm horario;

if (getLocalTime(&horario, 10000)) {
    Serial.println("Horario sincronizado com sucesso!");

    Serial.printf(
        "Data e hora: %02d/%02d/%04d %02d:%02d:%02d\n",
        horario.tm_mday,
        horario.tm_mon + 1,
        horario.tm_year + 1900,
        horario.tm_hour,
        horario.tm_min,
        horario.tm_sec
    );
} else {
    Serial.println("Falha ao sincronizar horario!");
}


  mqtt.setServer(TB_HOST, TB_PORT);
  mqtt.setCallback(receberMensagem);
  mqtt.setBufferSize(2048);

  conectarThingsBoard();
}

void loop() {

    // Tenta manter a conexão com o ThingsBoard
    conectarThingsBoard();

    // Processa mensagens MQTT
    mqtt.loop();


    // =====================================================
    // O TRAVAMENTO SÓ VALE SE ESTIVER CONECTADO AO
    // THINGSBOARD
    // =====================================================

    bool travadoTB = mqtt.connected() && travado;


    // =====================================================
    // OBTÉM O HORÁRIO
    // =====================================================

    struct tm horario;

    if (!getLocalTime(&horario)) {
        Serial.println("Falha ao obter horario");
        return;
    }

    int hora = horario.tm_hour;
    int minuto = horario.tm_min;

    int horarioAtual = hora * 60 + minuto;


    // =====================================================
    // BOTÃO FÍSICO
    // =====================================================

    fBotao();


    // =====================================================
    // VERIFICA O HORÁRIO PROGRAMADO
    // =====================================================

    bool horarioAtivo =
        estaDentroDeAlgumPeriodo(horarioAtual);


    // =====================================================
    // CONTROLE DA VÁLVULA
    // =====================================================

    if (travadoTB) {

        // -------------------------------------------------
        // TRAVADO PELO THINGSBOARD
        // -------------------------------------------------

        digitalWrite(rele, LOW);
        ligado = false;

    }
    else if (manualAtivo) {

        // -------------------------------------------------
        // MODO MANUAL
        // -------------------------------------------------
        // Funciona mesmo sem internet.

        if (ligado) {
            digitalWrite(rele, HIGH);
        }
        else {
            digitalWrite(rele, LOW);
        }

    }
    else if (!configuracaoRecebida) {

        // -------------------------------------------------
        // AUTOMÁTICO SEM CONFIGURAÇÃO
        // -------------------------------------------------

        digitalWrite(rele, LOW);

    }
    else {

        // -------------------------------------------------
        // MODO AUTOMÁTICO
        // -------------------------------------------------

        if (horarioAtivo) {
            digitalWrite(rele, HIGH);
        }
        else {
            digitalWrite(rele, LOW);
             ligado=false;
            enviarEstadoControle();
        }
    }


    // =====================================================
    // LED / STATUS DA VÁLVULA
    // =====================================================

    fLeds();

    delay(200);
}

// ADICIONAR MEDIDOR DE VAZÃO E PLUVIÔMETRO NO DASHBOARD MEDIDOR DE VAZÃO E PLUVIÔMETRO NO CÓDIGO
// TROCAR MQTT POR MQTT + TLS NO THINGSBOARD 