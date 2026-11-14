#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_random.h"

#define SDA 21
#define SCL 22
#define TRIG 5
#define ECHO 18
#define BEAT_NEAR_CM 10        /* mão abaixo disso = batida (descida) */
#define BEAT_FAR_CM 15         /* mão precisa subir acima disso para rearmar */
#define BEAT_TIMEOUT_MS 1500   /* sem nova batida nesse tempo = cancela a contagem */
#define BEAT_LOCK_MS 500       /* intervalo mínimo entre batidas (evita contar 2 numa só) */
#define SAMPLE_MS 30           /* período de leitura do sensor */
#define W 128
#define H 64
#define DELAY(ms) vTaskDelay(pdMS_TO_TICKS(ms))

/* janela da "roleta" (gacha) */
#define WX0 34
#define WX1 93
#define WY0 8
#define WY1 51
#define WCY 29
#define CH 44                  /* altura de uma célula da roleta */

static i2c_master_dev_handle_t oled;
static uint8_t fb[W * H / 8];
static const char *NAMES[] = {"PEDRA", "PAPEL", "TESOURA"};
static const char *CALL[] = {"JO", "KEN", "PO!"};

/* ---------- SSD1306 ---------- */
static void tx(uint8_t ctrl, const uint8_t *d, size_t n)
{
    uint8_t buf[W + 1] = {ctrl};
    memcpy(buf + 1, d, n);
    i2c_master_transmit(oled, buf, n + 1, 100);
}

static void oled_init(void)
{
    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0, .sda_io_num = SDA, .scl_io_num = SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &bus));

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x3C, .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dc, &oled));

    const uint8_t init[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14,
                            0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1,
                            0xDB, 0x40, 0xA4, 0xA6, 0xAF};
    tx(0x00, init, sizeof init);
}

static void invert(bool inv)
{
    uint8_t c = inv ? 0xA7 : 0xA6;
    tx(0x00, &c, 1);
}

static void flush(void)
{
    const uint8_t a[] = {0x21, 0, W - 1, 0x22, 0, 7};
    tx(0x00, a, sizeof a);
    for (int p = 0; p < 8; p++) tx(0x40, fb + p * W, W);
}

/* ---------- Gráficos ---------- */
static int ox, oy;                                  /* tremor da tela */
static int clip_x0 = 0, clip_y0 = 0, clip_x1 = W - 1, clip_y1 = H - 1;
static bool px_inv;                                 /* desenha com cores invertidas */

static void px(int x, int y, bool on)
{
    if (x < clip_x0 || x > clip_x1 || y < clip_y0 || y > clip_y1) return;
    x += ox; y += oy;
    if ((unsigned)x >= W || (unsigned)y >= H) return;
    on ^= px_inv;
    uint8_t m = 1 << (y % 8);
    if (on) fb[x + y / 8 * W] |= m; else fb[x + y / 8 * W] &= ~m;
}

static void line(int x0, int y0, int x1, int y1, bool on)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        px(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void fill(int x0, int y0, int x1, int y1, bool on)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) px(x, y, on);
}

static void disc(int cx, int cy, int r, bool on)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) px(cx + x, cy + y, on);
}

static void ring(int cx, int cy, int r) { disc(cx, cy, r, 1); disc(cx, cy, r - 2, 0); }

static void rect(int x0, int y0, int x1, int y1)
{
    line(x0, y0, x1, y0, 1); line(x1, y0, x1, y1, 1);
    line(x1, y1, x0, y1, 1); line(x0, y1, x0, y0, 1);
}

/* ---------- Sprites (sc = 16 é o tamanho normal; dá para esticar/encolher) ---------- */
static void hand(int t, int cx, int cy, int sc)
{
#define S(v) (((v) * sc + ((v) < 0 ? -8 : 8)) / 16)
    if (t == 0) {                                   /* pedra */
        disc(cx, cy, S(17), 1);
        line(cx + S(-8), cy + S(-5), cx + S(-1), cy + S(2), 0);
        line(cx + S(-1), cy + S(2), cx + S(7), cy + S(-3), 0);
        line(cx + S(-3), cy + S(9), cx + S(4), cy + S(5), 0);
        line(cx + S(2), cy + S(-12), cx + S(5), cy + S(-7), 0);
    } else if (t == 1) {                            /* papel */
        for (int k = 0; k < 2; k++)
            rect(cx - S(14) + k, cy - S(18) + k, cx + S(14) - k, cy + S(18) - k);
        line(cx + S(5), cy - S(18), cx + S(14), cy - S(9), 1);
        line(cx + S(5), cy - S(18), cx + S(5), cy - S(9), 1);
        line(cx + S(5), cy - S(9), cx + S(14), cy - S(9), 1);
        for (int i = 0; i < 4; i++)
            line(cx - S(9), cy + S(-3 + i * 6), cx + S(9), cy + S(-3 + i * 6), 1);
    } else {                                        /* tesoura */
        for (int k = 0; k < 2; k++) {
            line(cx + S(-8) + k, cy + S(12), cx + S(12) + k, cy - S(18), 1);
            line(cx + S(8) + k, cy + S(12), cx + S(-12) + k, cy - S(18), 1);
        }
        ring(cx + S(-9), cy + S(14), S(5));
        ring(cx + S(9), cy + S(14), S(5));
    }
#undef S
}

/* ---------- Texto (fonte 5x7, só as letras usadas) ---------- */
static const char LETTERS[] = "ADELOPRSTUJKN!";
static const uint8_t F[][5] = {
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x40,0x40,0x40,0x40}, {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
    {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F}, {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x04,0x08,0x10,0x7F}, {0x00,0x00,0x5F,0x00,0x00},
};

static void text(const char *s, int dx, int y, int sc, bool on)    /* centralizado */
{
    int x = dx + (W - (int)(strlen(s) * 6 - 1) * sc) / 2;
    for (; *s; s++, x += 6 * sc) {
        const char *p = strchr(LETTERS, *s);
        if (!p) continue;
        for (int i = 0; i < 5 * sc; i++)
            for (int j = 0; j < 7 * sc; j++)
                if (F[p - LETTERS][i / sc] >> (j / sc) & 1) px(x + i, y + j, on);
    }
}

/* texto branco com contorno preto (destaca sobre qualquer fundo) */
static void text_outline(const char *s, int y, int sc, int o)
{
    for (int dy = -o; dy <= o; dy += o)
        for (int dx = -o; dx <= o; dx += o)
            if (dx || dy) text(s, dx, y + dy, sc, 0);
    text(s, 0, y, sc, 1);
}

/* ---------- Efeitos ---------- */
static void lamp(int x, int y, bool lit)
{
    if (lit) fill(x, y, x + 1, y + 1, 1); else px(x, y, 1);
}

/* lâmpadas correndo na moldura, como em máquina de fliperama */
static void marquee(int ph)
{
    int i = 0;
    fill(0, 0, W - 1, 3, 0); fill(0, H - 4, W - 1, H - 1, 0);
    fill(0, 0, 3, H - 1, 0); fill(W - 4, 0, W - 1, H - 1, 0);
    for (int x = 1; x <= W - 3; x += 6, i++) lamp(x, 1, (i - ph) % 3 == 0);
    for (int y = 7; y <= H - 3; y += 6, i++) lamp(W - 3, y, (i - ph) % 3 == 0);
    for (int x = W - 9; x >= 1; x -= 6, i++) lamp(x, H - 3, (i - ph) % 3 == 0);
    for (int y = H - 9; y >= 7; y -= 6, i++) lamp(1, y, (i - ph) % 3 == 0);
}

/* raios girando atrás do texto (estilo programa de auditório japonês) */
static void rays(int ph)
{
    for (int i = 0; i < 10; i++)
        for (int k = 0; k < 3; k++) {
            float a = (i * 36 + k * 3 + ph * 5) * 0.0174533f;
            float c = cosf(a), s = sinf(a);
            line(64 + (int)(26 * c), 29 + (int)(26 * s),
                 64 + (int)(110 * c), 29 + (int)(110 * s), 1);
        }
}

/* confete */
#define NP 24
static float pxs[NP], pys[NP], pvx[NP], pvy[NP];

static void parts_init(void)
{
    for (int i = 0; i < NP; i++) {
        pxs[i] = W / 2; pys[i] = 23;
        pvx[i] = ((int)(esp_random() % 91) - 45) / 10.0f;
        pvy[i] = -1.5f - (esp_random() % 55) / 10.0f;
    }
}

static void parts_draw(void)
{
    for (int i = 0; i < NP; i++) {
        pvy[i] += 0.35f; pxs[i] += pvx[i]; pys[i] += pvy[i];
        int x = (int)pxs[i], y = (int)pys[i];
        if (y > H) continue;
        disc(x, y, 3, 0);
        if (i % 3 == 0) fill(x, y, x + 1, y + 1, 1);
        else if (i % 3 == 1) { line(x - 2, y, x + 2, y, 1); line(x, y - 2, x, y + 2, 1); }
        else { px(x, y, 1); px(x - 1, y - 1, 1); px(x + 1, y - 1, 1); px(x - 1, y + 1, 1); px(x + 1, y + 1, 1); }
    }
}

/* ---------- Cenas ---------- */
static const int8_t BOB[8] = {0, -1, -2, -2, -1, 0, 1, 1};

/* idle: símbolo "salta" ao aparecer e depois balança */
static void scene_idle(int sym, int age, int ph)
{
    static const int POP[6] = {7, 13, 20, 18, 15, 16};
    memset(fb, 0, sizeof fb);
    int sc = age < 6 ? POP[age] : 16;
    int bob = age < 6 ? 0 : BOB[(ph >> 1) & 7];
    hand(sym, W / 2, 23 + bob, sc);
    text_outline(NAMES[sym], 46, 2, 1);
    marquee(ph);
    flush();
}

/* contagem: JO / KEN / PO! estoura na tela, com tremor e raios */
static void scene_count(int n, int age, int ph)
{
    static const int POPT[5] = {3, 6, 4, 5, 5};
    static const int8_t SHK[5][2] = {{4, -3}, {-3, 3}, {2, 1}, {-1, -1}, {0, 0}};
    int a = age < 5 ? age : 4;
    memset(fb, 0, sizeof fb);
    ox = SHK[a][0]; oy = SHK[a][1];
    rays(ph);
    int sc = POPT[a];
    text_outline(CALL[n - 1], 29 - 7 * sc / 2, sc, 2);
    ox = oy = 0;
    fill(38, 50, 89, 60, 0);                        /* fundo preto atrás das bolinhas */
    for (int i = 0; i < 3; i++) {                   /* bolinhas de progresso */
        int x = W / 2 + (i - 1) * 16, r = (i == n - 1 && age < 3) ? 5 : 4;
        disc(x, 55, r + 1, 0);
        if (i < n) disc(x, 55, r, 1); else ring(x, 55, r);
    }
    marquee(ph);
    flush();
}

/* roleta: símbolos descendo na janela */
static void pointers(int ph)
{
    int h = (ph & 1) ? 8 : 6;
    for (int i = 0; i <= h; i++) {
        int half = h - i;
        line(17 + i, WCY - half, 17 + i, WCY + half, 1);
        line(110 - i, WCY - half, 110 - i, WCY + half, 1);
    }
}

static void scene_reel(float pos, int ph, float v)
{
    memset(fb, 0, sizeof fb);
    clip_x0 = WX0; clip_x1 = WX1; clip_y0 = WY0; clip_y1 = WY1;
    fill(WX0, WY0, WX1, WY1, 1);                    /* janela branca, símbolos pretos */
    px_inv = true;
    int base = (int)floorf(pos / CH);
    for (int c = base - 1; c <= base + 2; c++)
        hand(((c % 3) + 3) % 3, W / 2, WCY + (int)(pos - c * CH), 16);
    px_inv = false;
    if (v > 12)                                     /* riscos de velocidade */
        for (int i = 0; i < 8; i++) {
            int x = WX0 + esp_random() % (WX1 - WX0 + 1);
            int y = WY0 + esp_random() % (WY1 - WY0 + 1);
            line(x, y, x, y + 6 + (int)(esp_random() % 14), 0);
        }
    clip_x0 = 0; clip_y0 = 0; clip_x1 = W - 1; clip_y1 = H - 1;
    rect(WX0 - 2, WY0 - 2, WX1 + 2, WY1 + 2);
    rect(WX0 - 4, WY0 - 4, WX1 + 4, WY1 + 4);
    pointers(ph);
    marquee(ph);
    flush();
}

/* resultado: símbolo estoura, confete voa, nome com contorno */
static void scene_result(int sym, int age, int ph, bool confetti)
{
    static const int POPR[7] = {8, 14, 20, 18, 16, 17, 16};
    memset(fb, 0, sizeof fb);
    int sc = age < 7 ? POPR[age] : 16;
    int bob = age < 7 ? 0 : BOB[(ph >> 1) & 7];
    hand(sym, W / 2, 23 + bob, sc);
    if (confetti) parts_draw();
    text_outline(NAMES[sym], 46, 2, 1);
    marquee(ph);
    flush();
}

/* ---------- HC-SR04 ---------- */
static int dist_cm(void)                            /* -1 = sem eco */
{
    gpio_set_level(TRIG, 0); esp_rom_delay_us(2);
    gpio_set_level(TRIG, 1); esp_rom_delay_us(10);
    gpio_set_level(TRIG, 0);

    int64_t t0 = esp_timer_get_time();
    while (!gpio_get_level(ECHO))
        if (esp_timer_get_time() - t0 > 5000) return -1;
    int64_t s = esp_timer_get_time();
    while (gpio_get_level(ECHO))
        if (esp_timer_get_time() - s > 25000) return -1;
    return (int)((esp_timer_get_time() - s) / 58);
}

/* ---------- Detecção de batidas (task própria) ----------
 * Uma batida = mão desce abaixo de BEAT_NEAR_CM depois de ter subido acima de
 * BEAT_FAR_CM. Cada batida incrementa g_beats; g_armed indica que a mão já subiu.
 * Depois de uma batida, nenhuma outra é contada por BEAT_LOCK_MS. */
static volatile int g_beats;
static volatile bool g_armed;

static int med3(int a, int b, int c)
{
    if (a > b) { int t = a; a = b; b = t; }
    if (b > c) b = c;
    return a > b ? a : b;
}

static void sensor_task(void *arg)
{
    int h[3] = {400, 400, 400}, k = 0;
    bool armed = false;
    int64_t t_lock = 0;                                      /* instante da última batida */
    for (;;) {
        int d = dist_cm();
        h[k = (k + 1) % 3] = (d < 0 || d > 400) ? 400 : d;   /* sem eco = longe */
        int f = med3(h[0], h[1], h[2]);                      /* mediana de 3 */

        if (f > BEAT_FAR_CM) armed = true;
        else if (f <= BEAT_NEAR_CM && armed &&
                 esp_timer_get_time() - t_lock >= BEAT_LOCK_MS * 1000LL) {
            armed = false; g_beats++; t_lock = esp_timer_get_time();
        }

        g_armed = armed;
        DELAY(SAMPLE_MS);
    }
}

/* ---------- Gacha ---------- */
static int spin(int cur)                            /* devolve o símbolo sorteado */
{
    static const float BOUNCE[6] = {6, -4, 3, -2, 1, 0};
    const int N = 44;
    int r = esp_random() % 3;
    int d = ((r - cur) % 3 + 3) % 3;
    float start = cur * (float)CH;
    float travel = (9 + d) * (float)CH;             /* termina exatamente na célula r */

    for (int i = 1; i <= N; i++) {
        float t = i / (float)N, u = 1 - t;
        float pos = start + travel * (1 - u * u * u);       /* freia suave */
        float v = travel * 3 * u * u / N;                   /* px por quadro */
        scene_reel(pos, i, v);
        DELAY(5);
    }
    for (int i = 0; i < 6; i++) {                   /* "clunk" ao parar */
        scene_reel(start + travel + BOUNCE[i], N + i, 0);
        DELAY(10);
    }
    return r;
}

static void reveal(int sym)
{
    for (int i = 0; i < 2; i++) { invert(1); DELAY(60); invert(0); DELAY(60); }
    parts_init();
    for (int i = 0; i < 34; i++) scene_result(sym, i, i, true);
}

void app_main(void)
{
    gpio_reset_pin(TRIG); gpio_set_direction(TRIG, GPIO_MODE_OUTPUT);
    gpio_reset_pin(ECHO); gpio_set_direction(ECHO, GPIO_MODE_INPUT);
    oled_init();
    xTaskCreate(sensor_task, "sensor", 2048, NULL, 5, NULL);

    int cur = 0, count = 0, seen = 0, age = 0, cage = 0, ph = 0;
    int64_t t_last = esp_timer_get_time();          /* última troca de símbolo */
    int64_t t_beat = t_last;                        /* última batida */

    for (;;) {
        int64_t now = esp_timer_get_time();

        if (g_beats != seen) {                      /* nova batida: JO -> KEN -> PO! */
            seen = g_beats;
            count++;
            t_beat = now;
            cage = 0;

            if (count == 3) {                       /* PO! = revela o resultado */
                for (int i = 0; i < 5; i++) scene_count(3, i, ph++);
                int r = spin(cur);
                reveal(r);
                cur = r;
                printf("Resultado: %s\n", NAMES[cur]);

                /* mantém o resultado até a mão subir (mínimo de 0,6 s) */
                int64_t t_res = esp_timer_get_time();
                while (!(g_armed && esp_timer_get_time() - t_res > 600000)) {
                    scene_result(cur, 99, ph++, false);
                    DELAY(10);
                }

                seen = g_beats;                     /* descarta batidas durante o giro */
                count = 0;
                cur = (cur + 1) % 3;
                age = 0;
                t_last = esp_timer_get_time();
                continue;
            }
        }

        if (count > 0) {                            /* contagem em andamento */
            if (now - t_beat > BEAT_TIMEOUT_MS * 1000LL) { count = 0; age = 0; t_last = now; }
            else scene_count(count, cage++, ph++);
        } else {                                    /* idle: símbolos em loop, 0,5 s cada */
            if (now - t_last >= 500000) { cur = (cur + 1) % 3; age = 0; t_last = now; }
            scene_idle(cur, age++, ph++);
        }
        DELAY(10);
    }
}
