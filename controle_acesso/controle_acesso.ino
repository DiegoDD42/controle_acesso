/*
 * CONTROLE DE ACESSO POR SENHA - Maquete de laboratório
 * N202 - Robótica e Automação Inteligentes
 *
 * Hardware: Arduino Uno + receptor infravermelho KY-022 com controle remoto
 *           + servo (trava) + LED verde + LED vermelho + buzzer + 5 push buttons.
 *
 * Como funciona:
 *   A pessoa digita uma SENHA DE 4 TECLAS no controle remoto IR. Senha correta
 *   destrava a porta; senha errada aciona o alarme e, após 3 erros, bloqueia.
 *   Plano B: os botões A e B (A0 e A1) formam uma senha de 4 toques, caso o
 *   infravermelho falhe (luz forte, pilha do controle, etc.).
 *
 * Arquitetura:
 *   - Padrão STATE    : cada estado da porta é uma classe (TRANCADO, LIBERADO,
 *                       PORTA_ABERTA, BLOQUEADO, CADASTRO).
 *   - Padrão OBSERVER : LED, buzzer e log Serial "assinam" os eventos do
 *                       controlador, sem que os estados conheçam esses atuadores.
 *
 * Tratamento de incertezas:
 *   - Debounce nos botões (saída, sensor de porta, A e B): evita leituras falsas
 *     causadas por ruído mecânico.
 *   - Filtro dos quadros IR: ignora quadros de "tecla segurada" (repetição) e
 *     sinais de protocolo desconhecido (ruído de lâmpadas e luz do sol).
 *   - Timeout entre teclas: pausa longa descarta a senha incompleta.
 *
 * Primeiro uso: com a EEPROM vazia, a PRIMEIRA senha digitada vira a senha MESTRE.
 * A mestre, digitada com a porta trancada, entra no modo CADASTRO: a próxima
 * senha nova passa a ser autorizada. Para apagar tudo, ligue o Arduino
 * segurando o botão de saída.
 *
 * Atenção: a biblioteca IRremote usa o Timer2 do Uno, o mesmo da função tone().
 * Por isso o buzzer aqui NÃO usa tone(): é acionado com digitalWrite().
 * Biblioteca necessária: IRremote (versão 4.x), instalada pelo Gerenciador.
 */

// ===================== CONFIGURAÇÃO =====================

#define USA_IR_REMOTO     1   // senha pelo controle remoto IR (KY-022)
#define USA_SENHA_BOTOES  1   // plano B: senha pelos botões A e B
#define USA_SENSOR_PORTA  1   // 1 = botão de porta (estado PORTA_ABERTA); 0 = só tempo
#define BUZZER_ATIVO      1   // 1 = buzzer ativo (com oscilador); 0 = buzzer passivo
#define DEPURACAO_IR      1   // 1 = mostra o código de cada tecla na Serial

#if USA_IR_REMOTO
#include <IRremote.hpp>
#endif
#include <Servo.h>
#include <EEPROM.h>

// Pinos
const uint8_t PIN_IR           = 8;   // KY-022, pino S (sinal)
const uint8_t PIN_SERVO        = 6;
const uint8_t PIN_LED_VERDE    = 4;
const uint8_t PIN_LED_VERMELHO = 5;
const uint8_t PIN_BUZZER       = 7;
const uint8_t PIN_BOTAO_SAIDA  = 2;   // botão interno (para GND)
const uint8_t PIN_SENSOR_PORTA = 3;   // botão: LOW = porta FECHADA
const uint8_t PIN_SENHA_A      = A0;  // botão A da senha (para GND)
const uint8_t PIN_SENHA_B      = A1;  // botão B da senha (para GND)

// Ângulos do servo (ajuste na maquete)
const uint8_t ANGULO_TRANCADO  = 0;
const uint8_t ANGULO_LIBERADO  = 90;

// Tempos (ms)
const unsigned long TEMPO_LIBERADO_MS = 5000;   // espera alguém abrir a porta
const unsigned long ALERTA_PORTA_MS   = 15000;  // porta aberta por muito tempo
const unsigned long BLOQUEIO_MS       = 15000;  // bloqueio após tentativas inválidas
const unsigned long CADASTRO_MS       = 10000;  // janela do modo cadastro
const unsigned long SENHA_TIMEOUT_MS  = 4000;   // pausa máxima entre teclas
const uint8_t       TENTATIVAS_MAX    = 3;

// EEPROM: [0]=magic [1..4]=senha mestre [5]=qtd senhas [6..]=senhas (4 bytes cada)
const uint8_t EEPROM_MAGIC = 0xA5;
const uint8_t END_MAGIC    = 0;
const uint8_t END_MESTRE   = 1;
const uint8_t END_QTD      = 5;
const uint8_t END_LISTA    = 6;
const uint8_t MAX_SENHAS   = 10;

// ===================== TIPOS BÁSICOS =====================

// Senha de 4 teclas. Cada byte é o código de uma tecla
// (comando do controle IR, ou 1/2 para os botões A/B).
struct Senha {
  byte b[4];
  bool igual(const Senha& o) const { return memcmp(b, o.b, 4) == 0; }
};

enum Evento : uint8_t {
  EV_TRANCOU,
  EV_LIBEROU,
  EV_PORTA_ABERTA,
  EV_PORTA_ESQUECIDA,
  EV_NEGADO,
  EV_BLOQUEOU,
  EV_CADASTRO_INICIO,
  EV_CADASTRADO,
  EV_JA_CADASTRADO,
  EV_TECLA            // tecla registrada (bipe curto)
};

// ===================== OBSERVER (interface) =====================

class Observador {
 public:
  virtual void aoEvento(Evento ev) = 0;
  virtual void atualizar() {}   // para efeitos não bloqueantes (piscar, bipes)
};

// ===================== DEBOUNCE =====================
// Entrada com pull-up interno: botão pressionado = LOW = "ativo".
// A leitura só é aceita após ficar estável por 'intervalo' ms.

class Debounce {
  uint8_t pin;
  unsigned long intervalo;
  bool estavel;
  bool ultimaLeitura;
  bool pressionado;
  unsigned long tMudanca;
 public:
  Debounce(uint8_t p, unsigned long ms) : pin(p), intervalo(ms),
    estavel(false), ultimaLeitura(false), pressionado(false), tMudanca(0) {}

  void begin() {
    pinMode(pin, INPUT_PULLUP);
    estavel = ultimaLeitura = (digitalRead(pin) == LOW);
    tMudanca = millis();
  }

  void update() {
    bool leitura = (digitalRead(pin) == LOW);
    if (leitura != ultimaLeitura) {          // mudou: reinicia a contagem
      ultimaLeitura = leitura;
      tMudanca = millis();
    }
    if ((millis() - tMudanca >= intervalo) && (leitura != estavel)) {
      estavel = leitura;                     // estável o bastante: aceita
      if (estavel) pressionado = true;       // registra a borda de descida
    }
  }

  bool ativo() const { return estavel; }

  // Retorna true UMA vez por aperto
  bool foiPressionado() {
    bool r = pressionado;
    pressionado = false;
    return r;
  }
};

// ===================== SENHAS AUTORIZADAS (EEPROM) =====================

class ListaSenhas {
  bool comparar(int end, const Senha& s) {
    for (uint8_t j = 0; j < 4; j++)
      if (EEPROM.read(end + j) != s.b[j]) return false;
    return true;
  }
 public:
  bool temMestre() { return EEPROM.read(END_MAGIC) == EEPROM_MAGIC; }

  void definirMestre(const Senha& s) {
    for (uint8_t i = 0; i < 4; i++) EEPROM.update(END_MESTRE + i, s.b[i]);
    EEPROM.update(END_QTD, 0);
    EEPROM.update(END_MAGIC, EEPROM_MAGIC);
  }

  bool ehMestre(const Senha& s) { return temMestre() && comparar(END_MESTRE, s); }

  bool autorizada(const Senha& s) {
    uint8_t qtd = EEPROM.read(END_QTD);
    if (qtd > MAX_SENHAS) qtd = 0;
    for (uint8_t i = 0; i < qtd; i++)
      if (comparar(END_LISTA + i * 4, s)) return true;
    return false;
  }

  bool adicionar(const Senha& s) {
    uint8_t qtd = EEPROM.read(END_QTD);
    if (qtd >= MAX_SENHAS) return false;
    for (uint8_t j = 0; j < 4; j++) EEPROM.update(END_LISTA + qtd * 4 + j, s.b[j]);
    EEPROM.update(END_QTD, qtd + 1);
    return true;
  }

  void apagarTudo() {
    EEPROM.update(END_MAGIC, 0);
    EEPROM.update(END_QTD, 0);
  }
};

// ===================== STATE (interface) =====================

class Controlador;

class EstadoBase {
 public:
  virtual const __FlashStringHelper* nome() = 0;
  virtual void entrar(Controlador& c) {}
  virtual void atualizar(Controlador& c) {}
  virtual void aoReceberSenha(Controlador& c, const Senha& s) {}
  virtual void aoBotaoSaida(Controlador& c) {}
};

// ===================== CONTROLADOR (contexto) =====================

class Controlador {
  static const uint8_t MAX_OBS = 4;
  Observador* obs[MAX_OBS];
  uint8_t nObs;
  EstadoBase* estado;
  uint8_t seq[4];            // teclas da senha em andamento
  uint8_t nSeq;
  unsigned long tUltimaTecla;

  // Senha completa: valida e entrega ao estado atual
  void processarSenha(const Senha& s) {
    Serial.print(F("[")); Serial.print(millis() / 1000); Serial.println(F("s] Senha digitada."));

    if (!senhas.temMestre()) {   // primeiro uso: esta senha vira a mestre
      senhas.definirMestre(s);
      Serial.println(F("Senha MESTRE definida."));
      notificar(EV_CADASTRADO);
      return;
    }
    estado->aoReceberSenha(*this, s);
  }

  // Acumula teclas de qualquer origem (controle IR ou botões A/B)
  void registrarTecla(uint8_t codigo) {
    seq[nSeq++] = codigo;
    tUltimaTecla = millis();
    notificar(EV_TECLA);
    if (nSeq < 4) return;

    Senha s;
    memcpy(s.b, seq, 4);
    nSeq = 0;
    processarSenha(s);
  }

  // Pausa longa entre teclas: descarta a senha incompleta
  void verificarTimeoutSenha() {
    if (nSeq > 0 && millis() - tUltimaTecla > SENHA_TIMEOUT_MS) {
      nSeq = 0;
      Serial.println(F("Senha: tempo esgotado, recomece."));
    }
  }

#if USA_IR_REMOTO
  void lerIR() {
    if (!IrReceiver.decode()) return;
    // copia o que interessa e libera o receptor para o próximo quadro
    bool repetido = IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT;
    bool valido   = IrReceiver.decodedIRData.protocol != UNKNOWN;
    uint8_t cmd   = IrReceiver.decodedIRData.command;
    IrReceiver.resume();

    if (!valido) {                    // ruído ou protocolo não suportado
#if DEPURACAO_IR
      Serial.println(F("IR: sinal desconhecido ignorado."));
#endif
      return;
    }
    if (repetido) return;             // tecla segurada: conta só o primeiro quadro
#if DEPURACAO_IR
    Serial.print(F("IR: comando 0x")); Serial.println(cmd, HEX);
#endif
    registrarTecla(cmd);
  }
#endif

#if USA_SENHA_BOTOES
  void lerBotoesAB() {
    botaoA.update();
    botaoB.update();
    if (botaoA.foiPressionado()) registrarTecla(1);
    else if (botaoB.foiPressionado()) registrarTecla(2);
  }
#endif

 public:
  Servo servo;
  ListaSenhas senhas;
  Debounce botao;
  Debounce porta;
  Debounce botaoA;
  Debounce botaoB;
  uint8_t tentativas;
  unsigned long inicioEstado;

  // Estados (preenchidos em setup)
  EstadoBase *sTrancado, *sLiberado, *sPortaAberta, *sBloqueado, *sCadastro;

  Controlador() : nObs(0), estado(nullptr), nSeq(0), tUltimaTecla(0),
                  botao(PIN_BOTAO_SAIDA, 30), porta(PIN_SENSOR_PORTA, 50),
                  botaoA(PIN_SENHA_A, 30), botaoB(PIN_SENHA_B, 30),
                  tentativas(0), inicioEstado(0) {}

  void registrar(Observador* o) { if (nObs < MAX_OBS) obs[nObs++] = o; }

  void notificar(Evento ev) {
    for (uint8_t i = 0; i < nObs; i++) obs[i]->aoEvento(ev);
  }

  void mudarPara(EstadoBase* novo) {
    estado = novo;
    inicioEstado = millis();
    Serial.print(F("[")); Serial.print(millis() / 1000); Serial.print(F("s] Estado: "));
    Serial.println(estado->nome());
    estado->entrar(*this);
  }

  unsigned long tempoNoEstado() const { return millis() - inicioEstado; }

  void trancar()    { servo.write(ANGULO_TRANCADO); }
  void destrancar() { servo.write(ANGULO_LIBERADO); }

  void begin() {
    Serial.begin(9600);
#if USA_IR_REMOTO
    IrReceiver.begin(PIN_IR, DISABLE_LED_FEEDBACK);
#endif
    servo.attach(PIN_SERVO);
    botao.begin();
    porta.begin();
    botaoA.begin();
    botaoB.begin();

    // Botão de saída segurado ao ligar: apaga todas as senhas
    if (botao.ativo()) {
      senhas.apagarTudo();
      Serial.println(F("Todas as senhas foram apagadas."));
    }
    if (!senhas.temMestre())
      Serial.println(F("Sem senha mestre. Digite 4 teclas: serao a senha MESTRE."));
    mudarPara(sTrancado);
  }

  // Chamado a cada volta do loop
  void atualizar() {
    botao.update();
    porta.update();
    verificarTimeoutSenha();
#if USA_IR_REMOTO
    lerIR();
#endif
#if USA_SENHA_BOTOES
    lerBotoesAB();
#endif
    if (botao.foiPressionado()) estado->aoBotaoSaida(*this);

    estado->atualizar(*this);
    for (uint8_t i = 0; i < nObs; i++) obs[i]->atualizar();
  }
};

// ===================== ESTADOS CONCRETOS =====================

// TRANCADO: porta fechada e travada, aguardando senha ou botão de saída
class EstTrancado : public EstadoBase {
 public:
  const __FlashStringHelper* nome() override { return F("TRANCADO"); }
  void entrar(Controlador& c) override {
    c.trancar();
    c.notificar(EV_TRANCOU);
  }
  void aoReceberSenha(Controlador& c, const Senha& s) override {
    if (c.senhas.ehMestre(s)) {                    // mestre -> modo cadastro
      c.mudarPara(c.sCadastro);
    } else if (c.senhas.autorizada(s)) {           // senha correta: libera
      c.tentativas = 0;
      c.mudarPara(c.sLiberado);
    } else {                                       // senha errada
      c.tentativas++;
      c.notificar(EV_NEGADO);
      if (c.tentativas >= TENTATIVAS_MAX) c.mudarPara(c.sBloqueado);
    }
  }
  void aoBotaoSaida(Controlador& c) override { c.mudarPara(c.sLiberado); }
};

// LIBERADO: servo destravado, aguardando a pessoa abrir a porta
class EstLiberado : public EstadoBase {
 public:
  const __FlashStringHelper* nome() override { return F("LIBERADO"); }
  void entrar(Controlador& c) override {
    c.destrancar();
    c.notificar(EV_LIBEROU);
  }
  void atualizar(Controlador& c) override {
#if USA_SENSOR_PORTA
    if (!c.porta.ativo()) {                        // sensor: porta abriu
      c.mudarPara(c.sPortaAberta);
      return;
    }
#endif
    if (c.tempoNoEstado() >= TEMPO_LIBERADO_MS)    // ninguém abriu: tranca de novo
      c.mudarPara(c.sTrancado);
  }
};

// PORTA_ABERTA: só tranca quando o sensor confirma que a porta fechou
class EstPortaAberta : public EstadoBase {
  unsigned long ultimoAlerta;
 public:
  const __FlashStringHelper* nome() override { return F("PORTA_ABERTA"); }
  void entrar(Controlador& c) override {
    ultimoAlerta = millis();
    c.notificar(EV_PORTA_ABERTA);
  }
  void atualizar(Controlador& c) override {
    if (c.porta.ativo()) {                         // porta fechou
      c.mudarPara(c.sTrancado);
      return;
    }
    if (millis() - ultimoAlerta >= ALERTA_PORTA_MS) {  // esquecida aberta
      ultimoAlerta = millis();
      c.notificar(EV_PORTA_ESQUECIDA);
    }
  }
};

// BLOQUEADO: senhas erradas demais. Senhas são ignoradas até o fim do bloqueio.
// O botão de saída continua funcionando (quem está dentro precisa sair).
class EstBloqueado : public EstadoBase {
 public:
  const __FlashStringHelper* nome() override { return F("BLOQUEADO"); }
  void entrar(Controlador& c) override {
    c.trancar();
    c.notificar(EV_BLOQUEOU);
  }
  void aoReceberSenha(Controlador& c, const Senha& s) override { c.notificar(EV_NEGADO); }
  void aoBotaoSaida(Controlador& c) override { c.mudarPara(c.sLiberado); }
  void atualizar(Controlador& c) override {
    if (c.tempoNoEstado() >= BLOQUEIO_MS) {
      c.tentativas = 0;
      c.mudarPara(c.sTrancado);
    }
  }
};

// CADASTRO: a próxima senha nova vira autorizada. Mestre de novo = cancela.
class EstCadastro : public EstadoBase {
 public:
  const __FlashStringHelper* nome() override { return F("CADASTRO"); }
  void entrar(Controlador& c) override { c.notificar(EV_CADASTRO_INICIO); }
  void aoReceberSenha(Controlador& c, const Senha& s) override {
    if (c.senhas.ehMestre(s)) {
      c.mudarPara(c.sTrancado);
    } else if (c.senhas.autorizada(s)) {
      c.notificar(EV_JA_CADASTRADO);
    } else if (c.senhas.adicionar(s)) {
      c.notificar(EV_CADASTRADO);
      c.inicioEstado = millis();                   // renova a janela
    } else {
      c.notificar(EV_NEGADO);                      // memória cheia
    }
  }
  void atualizar(Controlador& c) override {
    if (c.tempoNoEstado() >= CADASTRO_MS) c.mudarPara(c.sTrancado);
  }
};

// ===================== OBSERVERS CONCRETOS =====================

// LED: vermelho fixo = trancado | verde fixo = liberado/aberta
//      vermelho piscando = bloqueado | verde piscando = cadastro
class LedObserver : public Observador {
  enum : uint8_t { M_VERM, M_VERDE, M_PISCA_VERM, M_PISCA_VERDE };
  uint8_t modo;
  bool fase;
  unsigned long t;

  void aplicar() {
    bool verde = (modo == M_VERDE) || (modo == M_PISCA_VERDE && fase);
    bool verm  = (modo == M_VERM)  || (modo == M_PISCA_VERM && fase);
    digitalWrite(PIN_LED_VERDE, verde);
    digitalWrite(PIN_LED_VERMELHO, verm);
  }
 public:
  LedObserver() : modo(M_VERM), fase(true), t(0) {}
  void begin() {
    pinMode(PIN_LED_VERDE, OUTPUT);
    pinMode(PIN_LED_VERMELHO, OUTPUT);
    aplicar();
  }
  void aoEvento(Evento ev) override {
    switch (ev) {
      case EV_TRANCOU:         modo = M_VERM; break;
      case EV_LIBEROU:
      case EV_PORTA_ABERTA:    modo = M_VERDE; break;
      case EV_BLOQUEOU:        modo = M_PISCA_VERM; break;
      case EV_CADASTRO_INICIO: modo = M_PISCA_VERDE; break;
      default: return;
    }
    fase = true;
    t = millis();
    aplicar();
  }
  void atualizar() override {
    if ((modo == M_PISCA_VERM || modo == M_PISCA_VERDE) && millis() - t >= 250) {
      fase = !fase;
      t = millis();
      aplicar();
    }
  }
};

// Buzzer: padrões [liga, desliga, liga, ...] em ms, sem delay() e sem tone()
// (tone() usa o Timer2, que a biblioteca IRremote precisa).
//  - Buzzer ativo : basta nível HIGH.
//  - Buzzer passivo: onda quadrada gerada por software (~2 kHz); o som sai mais áspero.
class BuzzerObserver : public Observador {
  const uint16_t* padrao;
  uint8_t total, idx;
  unsigned long t;
  bool somLigado;
  bool nivel;
  unsigned long tOnda;

  void passo() {
    somLigado = (idx % 2 == 0);
    if (somLigado && BUZZER_ATIVO) digitalWrite(PIN_BUZZER, HIGH);
    else if (!somLigado) digitalWrite(PIN_BUZZER, LOW);
  }
  void parar() {
    somLigado = false;
    digitalWrite(PIN_BUZZER, LOW);
  }
  void tocar(const uint16_t* p, uint8_t n) {
    padrao = p; total = n; idx = 0; t = millis();
    passo();
  }
 public:
  BuzzerObserver() : padrao(nullptr), total(0), idx(0), t(0),
                     somLigado(false), nivel(false), tOnda(0) {}
  void begin() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
  }

  void aoEvento(Evento ev) override {
    static const uint16_t P_OK[]        = {100, 60, 100};
    static const uint16_t P_NEGADO[]    = {150, 100, 150, 100, 150};
    static const uint16_t P_BLOQUEIO[]  = {900};
    static const uint16_t P_ESQUECIDA[] = {200, 200, 200, 200, 200};
    static const uint16_t P_CAD_INI[]   = {80, 80, 80};
    static const uint16_t P_CAD_OK[]    = {100, 60, 300};
    static const uint16_t P_JA[]        = {300};
    static const uint16_t P_TECLA[]     = {40};
    #define TOCAR(p) tocar(p, sizeof(p) / sizeof(p[0]))
    switch (ev) {
      case EV_TECLA:           TOCAR(P_TECLA); break;
      case EV_LIBEROU:         TOCAR(P_OK); break;
      case EV_NEGADO:          TOCAR(P_NEGADO); break;
      case EV_BLOQUEOU:        TOCAR(P_BLOQUEIO); break;
      case EV_PORTA_ESQUECIDA: TOCAR(P_ESQUECIDA); break;
      case EV_CADASTRO_INICIO: TOCAR(P_CAD_INI); break;
      case EV_CADASTRADO:      TOCAR(P_CAD_OK); break;
      case EV_JA_CADASTRADO:   TOCAR(P_JA); break;
      default: break;
    }
    #undef TOCAR
  }
  void atualizar() override {
    if (total == 0) return;
    if (somLigado && !BUZZER_ATIVO && micros() - tOnda >= 250) {   // buzzer passivo
      nivel = !nivel;
      digitalWrite(PIN_BUZZER, nivel);
      tOnda = micros();
    }
    if (millis() - t >= padrao[idx]) {
      idx++;
      if (idx >= total) { parar(); total = 0; return; }
      t = millis();
      passo();
    }
  }
};

// Log pela Serial: registro de o quê e quando
class LogObserver : public Observador {
 public:
  void aoEvento(Evento ev) override {
    if (ev == EV_TECLA) return;   // uma tecla isolada não vai para o log
    Serial.print(F("[")); Serial.print(millis() / 1000); Serial.print(F("s] EVENTO: "));
    switch (ev) {
      case EV_TRANCOU:         Serial.println(F("porta trancada")); break;
      case EV_LIBEROU:         Serial.println(F("ACESSO LIBERADO")); break;
      case EV_PORTA_ABERTA:    Serial.println(F("porta aberta")); break;
      case EV_PORTA_ESQUECIDA: Serial.println(F("ALERTA: porta aberta por muito tempo")); break;
      case EV_NEGADO:          Serial.println(F("ACESSO NEGADO")); break;
      case EV_BLOQUEOU:        Serial.println(F("BLOQUEADO: senhas erradas demais")); break;
      case EV_CADASTRO_INICIO: Serial.println(F("modo cadastro: digite a nova senha")); break;
      case EV_CADASTRADO:      Serial.println(F("senha cadastrada")); break;
      case EV_JA_CADASTRADO:   Serial.println(F("senha ja cadastrada")); break;
      default: break;
    }
  }
};

// ===================== INSTÂNCIAS GLOBAIS =====================

Controlador    ctrl;
EstTrancado    estTrancado;
EstLiberado    estLiberado;
EstPortaAberta estPortaAberta;
EstBloqueado   estBloqueado;
EstCadastro    estCadastro;

LedObserver    ledObs;
BuzzerObserver buzzerObs;
LogObserver    logObs;

void setup() {
  ctrl.sTrancado    = &estTrancado;
  ctrl.sLiberado    = &estLiberado;
  ctrl.sPortaAberta = &estPortaAberta;
  ctrl.sBloqueado   = &estBloqueado;
  ctrl.sCadastro    = &estCadastro;

  ledObs.begin();
  buzzerObs.begin();
  ctrl.registrar(&logObs);
  ctrl.registrar(&ledObs);
  ctrl.registrar(&buzzerObs);

  ctrl.begin();
}

void loop() {
  ctrl.atualizar();
}
