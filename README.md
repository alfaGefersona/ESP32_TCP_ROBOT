# 🧠 ESP32 — Controle de Múltiplos Motores DC via Wi-Fi (Access Point + Servidor TCP)

Este projeto demonstra o controle de **três motores DC independentes** utilizando o **ESP32** configurado como **Access Point Wi-Fi** e **Servidor TCP**.  
A comunicação é feita por meio de **mensagens JSON**, que determinam a direção e a velocidade de cada motor em tempo real.

---

## 🎯 Objetivos

- Criar um **Access Point Wi-Fi** com o ESP32;  
- Implementar um **servidor TCP** para receber comandos JSON;  
- Controlar **múltiplos motores DC via PWM (LEDC)**, com isolamento de canais;  
- Permitir controle individual ou simultâneo de motores A, B e C.

---

## 📂 Estrutura de Pastas

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

## 🔌 Hardware Utilizado

| Componente | Função | Observação |
|-------------|---------|------------|
| ESP32 DevKit | Microcontrolador principal | Responsável pelo Wi-Fi e PWM |
| Ponte H (L298N / L293D) | Interface de potência | Controla os motores DC |
| Motores DC | Atuadores | Três motores independentes |
| Fonte 5–12V | Alimentação | Energia dos motores e ESP32 |
| Jumpers | Conexões elétricas | Ligações entre ESP32 e ponte H |

---

## ⚙️ Mapeamento de Pinos

### Motor A — (OUT1 / OUT2)
| Função | GPIO | Descrição |
|--------|-------|------------|
| IN1 | GPIO 33 | Direção A |
| IN2 | GPIO 32 | Direção B |
| ENA | GPIO 14 | PWM — Canal LEDC 1 |

### Motor B — (OUT3 / OUT4)
| Função | GPIO | Descrição |
|--------|-------|------------|
| IN3 | GPIO 26 | Direção A |
| IN4 | GPIO 27 | Direção B |
| ENB | GPIO 25 | PWM — Canal LEDC 0 |

### Motor C — (OUT5 / OUT6)
| Função | GPIO | Descrição |
|--------|-------|------------|
| IN1 | GPIO 16 | Direção A |
| IN2 | GPIO 17 | Direção B |
| ENA | GPIO 18 | PWM — Canal LEDC 2 |

---

## ⚙️ Parâmetros Técnicos

| Parâmetro | Valor |
|------------|--------|
| Frequência PWM | 5 kHz |
| Resolução PWM | 8 bits (0–255) |
| Modo | LEDC_LOW_SPEED_MODE |
| Timer | LEDC_TIMER_0 |
| Canais | A: 1 / B: 0 / C: 2 |
| Comunicação | TCP (JSON via Wi-Fi) |

---

## 📡 Configuração Wi-Fi

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
| Máx. conexões | 1 |

Após iniciar o ESP32, conecte-se à rede **Robot** e envie comandos TCP diretamente.

---

## 🧩 Arquitetura do Sistema

### Inicialização (`app_main`)
- Inicializa a NVS (memória não volátil);
- Configura o **Access Point Wi-Fi** (`wifi_init_softap`);
- Define o **timer PWM global** (`ledc_timer_config`);
- Inicializa os motores A, B e C (`motor_init`);
- Cria a tarefa **`tcp_server_task`** para gerenciar conexões.

### Servidor TCP (`tcp_server_task`)
- Cria e escuta conexões TCP na porta `8080`;
- Recebe mensagens JSON com os campos `"motor"`, `"direction"` e `"speed"`;
- Decodifica e aplica o comando ao motor correspondente;
- Retorna resposta textual ao cliente.

### Controle dos Motores
As funções utilizam GPIOs e PWM via driver **LEDC**:

| Função | Descrição |
|--------|------------|
| `motor_forwardVM(&motorX, speed)` | Gira o motor para frente |
| `motor_backwardVM(&motorX, speed)` | Gira o motor para trás |
| `motor_stopVM(&motorX)` | Para o motor (PWM=0) |

---

## 💬 Formato da Comunicação

O cliente envia mensagens JSON via TCP.

### Exemplo 1 — Motor A para frente
```json
{"motor":1,"direction":"forward","speed":200}
```

### Exemplo 2 — Motor B ré
```json
{"motor":2,"direction":"backward","speed":150}
```

### Exemplo 3 — Motor C parar
```json
{"motor":3,"direction":"stop","speed":0}
```

### Exemplo 4 — Parar todos
```json
{"direction":"stop_all"}
```

### Respostas do Servidor
```
frente
re
parado
todos motores parados
comando inválido
```

---

## 🧪 Teste de Comunicação

1. **Conecte-se à rede Wi-Fi:**
   ```
   SSID: Robot
   Senha: 12345678
   ```

2. **Abra um cliente TCP**, como `netcat`:
   ```bash
   nc 192.168.4.1 8080
   ```

3. **Envie o comando:**
   ```json
   {"motor":1,"direction":"forward","speed":180}
   ```

4. **Receba a resposta:**
   ```
   frente
   ```

5. **Veja os logs no monitor serial:**
   ```
   I (1456) ESP32_TCP_MOTOR: motor: 1 | direcao: forward | velocidade: 180
   I (1458) ESP32_TCP_MOTOR: motor frente (speed=180)
   ```

---

## 🔌 Diagrama de Ligação

```
ESP32         PONTE H (L298N)
------        ----------------
GPIO 33  ---> IN1
GPIO 32  ---> IN2
GPIO 14  ---> ENA (PWM Motor A)

GPIO 26  ---> IN3
GPIO 27  ---> IN4
GPIO 25  ---> ENB (PWM Motor B)

GPIO 16  ---> IN5
GPIO 17  ---> IN6
GPIO 18  ---> ENA (PWM Motor C)

5V       ---> +5V
GND      ---> GND
```

---

## 🧰 Solução de Problemas

| Problema | Causa provável | Solução |
|-----------|----------------|----------|
| Motor A parou após adicionar Motor C | Conflito de canais/timers do LEDC | Use canais distintos ou timers separados |
| Wi-Fi não aparece | Falha no modo AP | Reinicie o ESP32 |
| Direção invertida | Pinos IN1/IN2 trocados | Inverta as conexões |
| Cliente desconecta | JSON malformado | Corrija o formato da mensagem |
| Duty não atua | PWM não atualizado | Confirme `ledc_update_duty()` após `set_duty()` |

---

## ⚙️ Compilação e Upload

1. Configure o alvo:
   ```bash
   idf.py set-target esp32
   ```

2. Compile:
   ```bash
   idf.py build
   ```

3. Faça upload:
   ```bash
   idf.py flash
   ```

4. Monitore:
   ```bash
   idf.py monitor
   ```
