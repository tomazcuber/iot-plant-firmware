# Estrutura do Capítulo de Metodologia (Cap 4)

> Artefato de planejamento da tese *Monitoramento Distribuído para Agricultura Urbana*.
> Consolida as decisões tomadas na sessão de entrevista sobre como estruturar o capítulo
> de proposta e metodologia, a partir do estado atual do documento e das specs do firmware
> (`CONTEXT.md`, `docs/superpowers/specs/`, `docs/adr/`).

## Decisões de enquadramento (fixadas)

- **Estágio: híbrido.** A metodologia é escrita agora como **definitiva** (procedimentos fixos,
  no presente/definitivo: "o procedimento consiste em...", "cada firmware de referência isola...").
  Os números entram depois, incrementalmente, num capítulo de **Resultados** (irmão, fora deste capítulo).
- **Arquitetura de capítulos:** um **capítulo único fundido** (Cap 4 "Proposta e Metodologia").
  - Cap 4 — Proposta e Metodologia (este documento)
  - Cap 5 — Resultados (a escrever depois)
  - Cap 6 — Conclusão (hoje é o Cap 4; será renumerado)
- **Peso igual** entre validação funcional e avaliação energética — as duas metodologias
  têm profundidade equivalente.
- **Sequência interna:** proposta compartilhada primeiro → validação funcional → energia.
- **Fronteira funcional:** firmware → broker MQTT; o app Android entra apenas como
  **cliente mínimo** (demonstra provisionamento e streaming BLE), sem validar UI/funcionalidades.
- **Enquadramento das alegações energéticas:** o **diferencial** é o núcleo (comparações
  imunes ao overhead não-modelado); alegações **absolutas** (E_cycle em mJ, autonomia)
  entram como projeção secundária com o viés de ~1–8% explicitamente divulgado (segue [ADR-0001](../adr/0001-active-phase-boundary.md)).
- **Terminologia PT-BR:** fixada na tabela ao fim da [`CONTEXT.md`](../../CONTEXT.md).
  Destaques: *baseline firmware* → **firmware de referência**; *deep sleep* → **sono profundo**.

## Ganchos a pagar (do Cap 3.3)

A análise comparativa já promete três contrastes que a metodologia precisa substanciar:
arquitetura RTOS vs superloop, independência de plataforma (MQTT), e **eficiência energética**.
O Cap 4 deve fechar explicitamente esses três laços.

---

## Esqueleto do Cap 4

### 4.1 Visão geral da proposta
Arquitetura do sistema distribuído: nós (dispositivos ESP32-C6) → broker MQTT → app Android (cliente).
Situa os dois objetivos: protótipo distribuído (primário) e eficiência energética (secundário).
Fecha o gancho RTOS vs superloop em nível de arquitetura.

### 4.2 Modelo de energia
A equação como espinha dorsal:
`E_cycle = (I_sleep·t_sleep + I_sensor·t_sensor + I_wifi·t_wifi + I_ble·t_ble)·V`.
- Decompõe o ciclo em fases (sono profundo + fase ativa: sensores, WiFi, BLE).
- Distingue **corrente de consumo** (`I_*`, constante por configuração) de **duração de fase**
  (`t_*`, variável por ciclo).
- **Honestidade metodológica:** declarar a não-conservação de tempo (boot prologue + epílogo
  de sono com CPU ligada não-modelada, ~1% do E_cycle padrão, até ~8% no canto
  intervalo-longo/BLE-off) e a distinção diferencial vs absoluto. Fonte: [ADR-0001](../adr/0001-active-phase-boundary.md).

### 4.3 Proposta multi-firmware
1 firmware de produto (`apps/main`) + 4 firmwares de referência, cada um isolando um termo `I_*`:
- `static-sleep` → `I_sleep`
- `active-sensor` → `I_sensor` (laço apertado de leitura, rádios off — [ADR-0003](../adr/0003-active-sensor-tight-loop.md))
- `active-wifi` → `I_wifi`
- `active-ble` → `I_ble`

Justificar por que os `I_*` são medidos **externamente** (multímetro, corrente média em regime)
e por que todos rodam sobre Zephyr (mesma sobrecarga de SO do firmware de produto — [DD-SS-01](../superpowers/specs/2026-04-05-static-sleep-firmware-design.md)).
> **Corrigir antes de citar:** o PRD de `apps/main` (linha 18) lista só 3 baselines (omite `I_sensor`).
> São 4 — a `CONTEXT.md` e a ADR-0003 são a referência correta.

### 4.4 Firmware de produto (`apps/main`)
Descrição do protótipo: dois modos (provisionamento / operacional), o ciclo sono→despertar→fase
ativa→sono, provisionamento BLE, publicação MQTT (`sensors/raw` + `status`), streaming BLE
oportunístico, dois motivos de despertar (temporizador vs alerta de umidade do solo).
Serve o objetivo primário. Fecha o gancho de independência de plataforma (MQTT agnóstico).

### 4.5 Firmwares de referência
Detalhe de cada baseline e o que cada um mede. Enfatizar reprodutibilidade: mesma configuração
física de hardware (todos os sensores conectados) em todas as medições, incluindo o `I_sleep`
(que inclui o LM393 ~1–2 mA, dominante).

### 4.6 Validação funcional (end-to-end)
**Método:** demonstração instrumentada com **critérios de aceitação por requisito**.
- Execução controlada capturada: logs do `mosquitto_sub` de `sensors/raw` e `status`,
  provisionamento via app mínimo, captura da notificação BLE (`sensor_data`),
  re-provisionamento por botão.
- Tabela **requisito → evidência observável → critério de aprovação** (mapear os FR-MA relevantes).
- Fronteira: firmware → broker; app como cliente mínimo.

### 4.7 Medição energética (instrumentação e procedimento)
- **`I_*` externos:** procedimento com multímetro em série na linha VCC 5V, por firmware de
  referência (base em [static-sleep §Measurement Procedure](../superpowers/specs/2026-04-05-static-sleep-firmware-design.md)).
- **`t_*` on-device:** instrumentação por ciclo via `k_uptime_get()`, publicada no tópico `status`
  (`t_active_ms`, `t_sensor_ms`, `t_wifi_ms`, `t_ble_ms`, `t_sleep_ms`).
- **Metadados observados:** `wifi_rssi_dbm` (atribuição de variância do `t_wifi`, [DD-MA-13](../superpowers/specs/2026-04-05-main-firmware-design.md)),
  `cycle_count`, `wake_reason`. RSSI é capturado, mas **não** é eixo experimental.
- Calibração do sensor de solo (dois pontos, via Kconfig; ADC bruto sempre publicado) — registrar
  `CONFIG_SOIL_DRY_ADC`/`CONFIG_SOIL_WET_ADC`, modelo da sonda e VCC ([DD-MA-12](../superpowers/specs/2026-04-05-main-firmware-design.md)).
- Fonte de energia: **V = 5V** (bancada USB), regulador LDO incluído no sistema medido.

### 4.8 Desenho experimental
- **Fator controlado (fatorial):** intervalo de leitura (`read_interval_s`, ~3 níveis) × janela BLE
  on/off (`ble_window_s` = 0 vs >0). Alcançável só por provisionamento, sem reflash ([DD-MA-07](../superpowers/specs/2026-04-05-main-firmware-design.md)).
- **Comparação observacional:** timer vs evento (`wake_reason`), classificada por ciclo.
  Nota: a fase ativa é idêntica nos dois; a diferença energética real mora no `t_sleep` e no
  custo contínuo do comparador LM393 (embutido no `I_sleep`).
- V = 5V apenas (Li-Po fora de escopo).
- Reafirmar diferencial (núcleo) vs absoluto (projeção com ressalva).

---

## Fora de escopo desta sessão
- Redação do Cap 5 (Resultados).
- Eixo Li-Po (V=3,7V) e RSSI como fator manipulado.
- Validação da UI do app Android.
- Testes automatizados (ztest/twister).
