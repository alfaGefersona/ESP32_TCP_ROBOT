# ESP32 — Controle de Motores DC via Wi-Fi (Access Point + Servidor TCP)

Este projeto demonstra o controle de **dois motores DC** utilizando o **ESP32** configurado como **Access Point Wi-Fi** e **Servidor TCP**.  
A comunicação é feita por meio de **mensagens JSON**, que determinam a direção e velocidade de cada motor em tempo real.

---

## Objetivo

- Criar um **Access Point Wi-Fi** com o ESP32;
- Implementar um **servidor TCP** para receber comandos de um cliente remoto;
- Controlar **motores DC via PWM (modulação por largura de pulso)**;

---

##  Estrutura de Pastas

```
esp32-tcp-motor/
├── main/
│   ├── main.c                # Código principal (Wi-Fi, TCP e controle dos motores)
│   ├── CMakeLists.txt        # Configuração de build do módulo main
│   └── component.mk          # (Opcional)
│
├── build/                    # Gerado automaticamente após compilação
├── CMakeLists.txt            # Configuração global do projeto
├── sdkconfig                 # Configuração do projeto (gerada após menuconfig)
└── README.md                 # Este arquivo
```

---

##  Hardware Utilizado

| Componente | Função | Observação |
|-------------|---------|------------|
| ESP32 DevKit | Microcontrolador principal | Responsável pelo Wi-Fi e PWM |
| Ponte H (L298N ou L293D) | Interface de potência | Controla os motores DC |
| Motores DC | Atuadores | Dois motores independentes |
| Fonte 5–12V | Alimentação | Energia dos motores e ESP32 |
| Jumpers | Conexões elétricas | Ligações entre ESP32 e ponte H |

---

## Mapeamento de Pinos

### Motor A — (OUT1 / OUT2)
| Função | GPIO | Descrição |
|--------|-------|------------|
| IN1 | GPIO 33 | Controle de direção A |
| IN2 | GPIO 32 | Controle de direção B |
| ENA | GPIO 14 | PWM (Canal LEDC 1) |

### Motor B — (OUT3 / OUT4)
| Função | GPIO | Descrição |
|--------|-------|------------|
| IN3 | GPIO 26 | Controle de direção A |
| IN4 | GPIO 27 | Controle de direção B |
| ENB | GPIO 25 | PWM (Canal LEDC 0) |

---

## Parâmetros Técnicos

| Parâmetro | Valor |
|------------|--------|
| Frequência PWM | 5 kHz |
| Resolução PWM | 8 bits (0–255) |
| Timer | LEDC_TIMER_0 |
| Modo | LEDC_LOW_SPEED_MODE |
| Canal Motor A | LEDC_CHANNEL_1 |
| Canal Motor B | LEDC_CHANNEL_0 |

---

## Configuração Wi-Fi

O ESP32 atua como **Access Point**, criando sua própria rede sem fio.  
Os parâmetros estão definidos no código principal (`main.c`).

| Parâmetro | Valor |
|------------|--------|
| SSID | `Robot` |
| Senha | `12345678` |
| Canal | 1 |
| Modo | Access Point |
| IP padrão | `192.168.4.1` |
| Porta TCP | `8080` |
| Máx. conexões | 4 |

Após iniciar o ESP32, conecte-se diretamente à rede **Robot** para enviar comandos TCP.

---

##  Arquitetura do Sistema

### Inicialização (`app_main`)
- Inicializa a memória NVS;
- Configura o modo **Access Point Wi-Fi** (`wifi_init_softap`);
- Define e inicia o **timer PWM** (`ledc_timer_config`);
- Inicializa os motores A e B (`motor_init`);
- Cria a tarefa `tcp_server_task` para gerenciar a comunicação TCP.

### Servidor TCP (`tcp_server_task`)
- Cria e escuta conexões TCP na porta `8080`;
- Recebe mensagens JSON;
- Extrai os parâmetros `"direction"` e `"speed"`;
- Controla o motor conforme o comando recebido;
- Retorna resposta textual ao cliente.

### Controle dos Motores
As funções utilizam GPIOs e PWM via driver **LEDC**:
| Função | Descrição |
|--------|------------|
| `motor_forward(speed)` | Gira o motor B para frente |
| `motor_backward(speed)` | Gira o motor B para trás |
| `motor_stop()` | Para o motor B |
| `motor_forwardVM(&motorA, speed)` | Controla o motor A via struct |
| `motor_backwardVM(&motorA, speed)` | Movimento reverso do motor A |
| `motor_stopVM(&motorA)` | Parada suave (PWM=0) |

---

## Formato da Comunicação

O cliente envia mensagens JSON para o ESP32 via TCP.

### Exemplo 1 — Frente
```json
{"direction":"forward","speed":200}
```

### Exemplo 2 — Ré
```json
{"direction":"backward","speed":150}
```

### Exemplo 3 — Parar
```json
{"direction":"stop","speed":0}
```

### Respostas do Servidor
```
Frente
Ré
Parado
Comando inválido
```

---

##  Teste de Comunicação

1. **Conecte-se à rede Wi-Fi** criada pelo ESP32:
   ```
   SSID: Robot
   Senha: 12345678
   ```

2. **Use um terminal TCP** (como `netcat`, PuTTY ou SocketTest):
   ```bash
   nc 192.168.4.1 8080
   ```

3. **Envie um comando JSON**:
   ```json
   {"direction":"forward","speed":180}
   ```

4. **Receba a resposta textual**:
   ```
   Frente
   ```

5. **Observe os logs no monitor serial**:
   ```
   I (1456) ESP32_TCP_MOTOR: Direção: forward | Velocidade: 180
   I (1458) ESP32_TCP_MOTOR: Motor frente (speed=180)
   ```

---

## Diagrama de Ligação

```
ESP32        PONTE H (L298N)
------       ----------------
GPIO 33 ---> IN1
GPIO 32 ---> IN2
GPIO 14 ---> ENA (PWM Motor A)

GPIO 26 ---> IN3
GPIO 27 ---> IN4
GPIO 25 ---> ENB (PWM Motor B)

5V     ---> +5V
GND    ---> GND
```

---

## Solução de Problemas

| Problema | Causa provável | Solução |
|-----------|----------------|----------|
| Wi-Fi não aparece | Falha na inicialização do AP | Reinicie o ESP32 e verifique logs |
| Motor não gira | PWM não configurado corretamente | Verifique GPIOs e `motor_init()` |
| Direção invertida | Pinos IN1/IN2 trocados | Inverta as ligações na ponte H |
| Cliente desconecta | JSON inválido | Envie comandos bem formatados |
| Motor não para | Duty PWM != 0 | Verifique `motor_stop()` |

---

## ⚙️ Compilação e Upload

1. Configure o alvo:
   ```bash
   idf.py set-target esp32
   ```

2. Compile o projeto:
   ```bash
   idf.py build
   ```

3. Faça o upload:
   ```bash
   idf.py flash
   ```

4. Inicie o monitor serial:
   ```bash
   idf.py monitor
   ```

---

