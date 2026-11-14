# Pedra-Papel-Tesoura

## Componentes

- ESP32 DevKit 30 pinos (USB-C)
- Display OLED 0,96" I2C
- Sensor ultrassônico HC-SR04
- Resistores 2,2k e 3,3k (divisor do ECHO)
- Capacitores 2x 100nF e 2x 10uF
- 2 barras de pino fêmea 1x4

## Ligações

| ESP32 | Vai para |
|---|---|
| GPIO21 | SDA do display |
| GPIO22 | SCL do display |
| GPIO5 | TRIG do sensor |
| GPIO18 | ECHO do sensor (pelo divisor) |
| 3V3 | VCC do display |
| VIN | VCC do sensor |
| GND | GND |

## Código

Feito no ESP-IDF (versão 5.3 ou mais nova).

```
cd Software/ESP-IDF
idf.py set-target esp32
idf.py build flash monitor
```

## Placa

Feita para fresadora, com todos os componentes soldados por baixo. A trilha de cima liga as duas vias; se os furos não forem metalizados, passa um fio nelas e solda dos dois lados.

## Case

4 peças em PLA ou PETG: as duas metades do corpo e os dois pés. Encaixa a placa, fecha as metades e desliza os pés até travar.
