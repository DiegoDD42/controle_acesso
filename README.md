


# Controle de Acesso por Senha (Controle Remoto IR) para Laboratório

Trabalho da VT — N202 Robótica e Automação Inteligentes (Ciência da Computação, 8º período)
Professor: Milton Miranda Neto · Apresentação: 11/11/2026

**Integrantes:** Diego Dantas Domingues, João Luiz Alves Mamede Netto, Luiz Gustavo Silva Bartholo, Thales Silveira de Queiroz

---

## 1. O problema

**Quem é afetado:** técnicos, professores e a instituição responsáveis por laboratórios com equipamentos de alto valor (notebooks, kits de robótica, impressora 3D).

**Onde acontece:** na porta do laboratório do campus.

**Como é tratado hoje:** chave física e caderno de assinaturas. A chave pode ser copiada, esquecida ou emprestada sem aviso, e o caderno pode ser preenchido de forma incorreta ou falsificado. Não existe registro confiável de quem entrou e quando.

**Por que importa:** perdas e danos ao patrimônio ficam sem responsável identificável, e a segurança do laboratório depende da boa-fé de quem usa a chave.

## 2. A solução

Uma porta com **senha de 4 teclas digitada num controle remoto infravermelho**: o controle funciona como uma chave eletrônica que não precisa de contato. Senha correta destrava a porta por alguns segundos e registra o acesso; senha errada aciona o buzzer e, após 3 erros, bloqueia o sistema por 15 s. Quem está dentro sai pelo botão interno. Como plano B, dois botões (A e B) na porta formam uma senha alternativa de 4 toques.

### Diagrama de blocos

```
 SENSORES                         CONTROLE                  ATUADORES
 Receptor IR KY-022 ───┐                                 ┌─ Servo (trava)
 Botões A e B (plano B)┼───►  Arduino Uno            ───►├─ LED verde / vermelho
 Botão de saída ───────┤      (máquina de estados)       ├─ Buzzer
 Sensor de porta ──────┘                                 └─ Log (Serial)
```

### Máquina de estados
<img width="1409" height="752" alt="Gemini_Generated_Image_ya8sxlya8sxlya8s" src="https://github.com/user-attachments/assets/a1cd105d-3c07-4de8-96cd-5baedf6f9759" />


As transições dependem do que os sensores percebem (teclas do controle, botões, porta), e não apenas de uma sequência temporizada.

## 3. Componentes

| Qtd | Componente | Função |
|---|---|---|
| 1 | Arduino Uno | Controlador |
| 1 | Receptor infravermelho KY-022 + controle remoto IR | Percepção: recebe as teclas da senha |
| 1 | Servo (SG90 ou similar) | Atuação: trava da porta |
| 1 | LED verde + resistor 220 Ω | Sinaliza acesso liberado |
| 1 | LED vermelho + resistor 220 Ω | Sinaliza trancado/bloqueado |
| 1 | Buzzer | Feedback sonoro |
| 4 | Push buttons | Saída interna, sensor de porta fechada, botões A e B (o quinto fica de reserva) |
| 1 | Fonte externa 5–6 V (ex.: 4 pilhas AA) | Alimentação do servo |
| 1 | Power bank | Alimentação do Arduino |
| — | Protoboard, jumpers, papelão | Montagem e maquete |

## 4. Esquema de ligação

| Componente | Pino do componente | Arduino |
|---|---|---|
| KY-022 (receptor IR) | S (sinal) | D8 |
| KY-022 | + (alimentação) | 5V |
| KY-022 | − | GND |
| Servo | Sinal | D6 |
| Servo | VCC | **+ da fonte externa** |
| Servo | GND | GND da fonte **e** GND do Arduino (terra comum) |
| LED verde | Ânodo (via 220 Ω) | D4 |
| LED vermelho | Ânodo (via 220 Ω) | D5 |
| Buzzer | Positivo | D7 |
| Botão de saída | Um terminal | D2 (outro terminal no GND) |
| Sensor de porta (botão) | Um terminal | D3 (outro terminal no GND) |
| Botão A da senha | Um terminal | A0 (outro terminal no GND) |
| Botão B da senha | Um terminal | A1 (outro terminal no GND) |

Os GNDs de LEDs, buzzer, botões, receptor IR, servo, pilhas e Arduino ficam todos juntos numa trilha GND da protoboard (terra comum). Confira os rótulos impressos na placa do KY-022, porque a ordem dos pinos muda entre fabricantes.

O sensor de porta é um botão que fica **pressionado quando a porta está fechada** (LOW = fechada). Se não houver sensor, use `#define USA_SENSOR_PORTA 0` no código.

> **Segurança:** o projeto usa apenas baixa tensão (máx. 12 V). Nenhuma parte é ligada à rede elétrica. O servo usa fonte externa com terra comum ao Arduino.

## 5. Tratamento de incertezas

| Técnica | Onde | Por quê |
|---|---|---|
| **Debounce (30 ms / 50 ms)** | Botões de saída, sensor de porta, A e B | O ruído mecânico gera vários "apertos" falsos num único aperto. Sem debounce, uma senha poderia ganhar teclas fantasmas. |
| **Filtro dos quadros IR** | Receptor KY-022 | O receptor capta ruído de lâmpadas e do sol. Ignoramos sinais de protocolo desconhecido, e quadros de "tecla segurada" (repetição) contam só uma vez. |
| **Timeout entre teclas (4 s)** | Digitação da senha | Uma senha incompleta é descartada, para a próxima pessoa não herdar teclas de quem desistiu. |

## 6. Padrões de projeto

- **State:** cada estado é uma classe (`EstTrancado`, `EstLiberado`, `EstPortaAberta`, `EstBloqueado`, `EstCadastro`), com suas próprias reações a senha, botão e tempo.
- **Observer:** `LedObserver`, `BuzzerObserver` e `LogObserver` assinam os eventos do `Controlador`. Os estados não conhecem LEDs nem buzzer, então é simples adicionar um novo atuador.

## 7. Como usar

1. Instale a Arduino IDE e, em *Gerenciar Bibliotecas*, a biblioteca **IRremote** (versão 4.x). `Servo` e `EEPROM` já vêm com a IDE.
2. Monte o circuito conforme a seção 4.
3. Abra `controle_acesso/controle_acesso.ino`, selecione *Arduino Uno* e a porta, e envie.
4. Abra o Monitor Serial em **9600 baud**. Aponte o controle para o receptor e aperte teclas: cada uma deve aparecer como `IR: comando 0x..` (se aparecer só "sinal desconhecido", o controle usa um protocolo não suportado; use os botões A e B).
5. **Primeiro uso:** digite 4 teclas, que viram a **senha MESTRE**. Anote qual é.
6. **Cadastrar senhas:** digite a mestre (LED verde pisca), depois cada nova senha de 4 teclas (bipe de confirmação). A mestre de novo, ou 10 s sem digitar, encerra o cadastro. As senhas ficam salvas na EEPROM.
7. **Uso normal:** senha correta → LED verde, servo destrava. Senha errada → 3 bipes. Três erros → bloqueio de 15 s (LED vermelho piscando). Botão interno → libera a porta.
8. **Apagar todas as senhas:** ligue o Arduino segurando o botão de saída.

**Ajustes:** ângulos do servo (`ANGULO_TRANCADO` / `ANGULO_LIBERADO`), tempos e as chaves `USA_*` estão no topo do `.ino`. Para a versão final, `DEPURACAO_IR 0` impede que o código das teclas apareça na Serial.

## 8. Plano de contingência

- **Infravermelho falhou** (pilha do controle, luz forte, receptor com defeito): use a senha de 4 toques nos **botões A e B** (por exemplo A-B-B-A). Ela entra na mesma máquina de estados e funciona junto com o IR, sem trocar nada no código.
- **Sem sensor de porta:** `USA_SENSOR_PORTA 0` (a porta tranca por tempo).
- **Qualquer falha ao vivo:** vídeo de 30 a 60 s da maquete funcionando.

## 9. Dificuldades, limitações e melhorias futuras

- Sem relógio de tempo real, o log usa o tempo desde que o Arduino ligou.
- O sinal IR não é criptografado e pode ser copiado; um uso real exigiria autenticação mais forte.
- O IRremote ocupa o Timer2 do Uno, que é o mesmo do `tone()`, então o buzzer foi acionado por `digitalWrite()`.
- Melhorias: display para mensagens, módulo RTC, envio de logs por Wi-Fi (ESP32), remoção individual de senhas.

## 10. Estrutura do repositório

```
controle_acesso/
├── controle_acesso/
│   └── controle_acesso.ino
└── README.md
```

## 11. Slides da Apresentação

https://docs.google.com/presentation/d/1A3pzQUE_RqnvG1pWvYBm2iH3yjBVG04yilHceKTW_hE/edit?usp=sharing
