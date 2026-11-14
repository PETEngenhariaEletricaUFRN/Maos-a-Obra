# Pedra-Papel-Tesoura

Um robozinho que joga pedra, papel e tesoura com você. Você bate a mão na mesa na frente dele três vezes (jo, ken, po), o sensor ultrassônico conta as batidas e na terceira ele sorteia a jogada e mostra no display.

## Componentes

- ESP32 DevKit 30 pinos (USB-C, CH340)
- Display OLED 0,96" I2C (SSD1306)
- Sensor ultrassônico HC-SR04
- Resistores 2,2k e 3,3k (0805)
- Capacitores 2x 100nF e 2x 10uF (0805)
- 2 barras de pino fêmea 1x4, uma reta (OLED) e uma 90° (sensor)

Tudo é alimentado pela USB do ESP32. O sensor usa os 5V do pino VIN e o display usa 3,3V. Como o ECHO do HC-SR04 sai em 5V, ele passa por um divisor (2,2k/3,3k) antes de chegar no ESP32, que fica com uns 3V.

## Ligações

| ESP32 | Vai para |
|---|---|
| GPIO21 | SDA do display |
| GPIO22 | SCL do display |
| GPIO5 | TRIG do sensor |
| GPIO18 | ECHO do sensor (pelo divisor) |
| 3V3 | VCC do display |
| VIN | VCC do sensor |
| GND | GND do display e do sensor |

## Pastas

- `Software/ESP-IDF`: código do ESP32
- `Hardware/PCB`: projeto da placa no KiCad 10 e os Gerbers
- `Hardware/Case`: arquivos da case para impressão 3D (STL, STEP e Fusion)
- `Hardware/Datasheets`: datasheets dos componentes

## Código

O código foi feito no ESP-IDF (usamos a versão 6.1, mas precisa ser a 5.3 ou mais nova). Para compilar e gravar:

```
cd Software/ESP-IDF
idf.py set-target esp32
idf.py build flash monitor
```

Se o robô estiver contando batida demais ou de menos, dá pra ajustar a distância e o tempo no começo do `main.c` (`BEAT_NEAR_CM`, `BEAT_FAR_CM`, `BEAT_TIMEOUT_MS` e `BEAT_LOCK_MS`).

## Placa

A placa tem 55,3 x 63,0 mm, contando a aba do sensor, e foi pensada para ser feita na fresadora, então os furos podem vir sem metalização. Por isso todos os componentes são soldados por baixo.

Em cima só tem uma trilha, que liga as duas únicas vias da placa: V1, logo abaixo do R1, e V2, logo acima do conector do sensor. Se os furos não forem metalizados, passa um pedaço de fio (perna de resistor serve) nas duas vias e solda dos dois lados. Se a placa for de uma face só, troca essa trilha por um fio isolado de V1 até V2.

Ordem de montagem: fios das vias, componentes SMD, ESP32 e as barras. Corta bem rente os fios das vias e coloca uma fita Kapton embaixo do ESP32, porque a trilha de cima passa por baixo dele.

## Case

São 4 peças para imprimir: as duas metades do corpo e os dois pés (em forma de esteira). Pode imprimir em PLA ou PETG, com camada de 0,2 mm e 20 a 30% de preenchimento.

Não precisa de parafuso. Encaixa a placa na metade esquerda, fecha com a direita e desliza os pés de frente pra trás até travar. Os pés que seguram as duas metades juntas.
