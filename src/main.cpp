// ===========================================================================
//  Desk Cat  -  a Nyan Cat break companion for the ideaspark ESP32
// ===========================================================================
//  Helper streams ~6x/sec:  idle=<s>  k=<0/1>  m=<0/1>  t=<YYYY-MM-DDThh:mm:ss>
//
//  A faithful Nyan Cat sprite (blitted from a pixel grid) flies on a starry
//  blue sky with an animated staircase rainbow while you work; dozes when idle;
//  demands a break after 30 min. Press BOOT anytime to start the 5-minute rest;
//  a fresh 30-min focus begins when it ends. Overlays: work timer (top-left), live ISO
//  date/time (top-right), and bottom work-pressure bar.
// ===========================================================================
#include <Arduino.h>
#include <TFT_eSPI.h>

#ifndef WORK_SECONDS
#define WORK_SECONDS (30 * 60)
#endif
#ifndef BREAK_SECONDS
#define BREAK_SECONDS (5 * 60)
#endif
#ifndef WORK_IDLE_SECONDS
#define WORK_IDLE_SECONDS 5
#endif
#ifndef IDLE_AWAY_SECONDS
#define IDLE_AWAY_SECONDS 30
#endif
#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 0
#endif

static const uint32_t WORK_MS        = (uint32_t)WORK_SECONDS * 1000UL;
static const uint32_t BREAK_MS       = (uint32_t)BREAK_SECONDS * 1000UL;
static const uint32_t PULSE_AFTER_MS = ((uint32_t)WORK_SECONDS + 5UL * 60UL) * 1000UL;
static const uint32_t SERIAL_TIMEOUT = 8000;
static const int CELL = 3, OX = 126, BASE_OY = 44;   // sprite cell size + origin (smaller cat)
#define STATUS_BAR_X 6
#define STATUS_BAR_Y 126
#define STATUS_BAR_W 228
#define STATUS_BAR_H 8
#define NSTARS 22

TFT_eSPI  tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

// --- Nyan cat sprite grid (33 x 20), '.' = transparent ---
#define CAT_W 33
#define CAT_H 20
static const char* CAT[CAT_H] = {
  "........XXXXXXXXXXXXXXXXX........",
  ".......XtttttttttttttttttX.......",
  "......XtttffffffffffffftttX......",
  "......XttffffffsffsfffffttX......",
  "......XtffsfffffffffffffftX......",
  "......XtfffffffffffXXfsfftX..XX..",
  "......XtffffffffffXggXffftX.XggX.",
  "......XtffffffsfffXgggXfftXXgggX.",
  "......XtffffffffffXggggXXXXggggX.",
  "......XtfffsffffffXggggggggggggX.",
  "....XXXtfffffffsfXggggggggggggggX",
  "..XXggXtfsfffffffXgggwXgggggwXggX",
  ".XggggXtfffffffffXgggXXgggXgXXggX",
  "XggXXXXtfffffsfffXgttgggggggggttX",
  "XggX..XttfsffffffXgttgXggXggXgttX",
  ".XX...XtttffffffffXgggXXXXXXXggX.",
  "......XXtttttttttttXggggggggggX..",
  ".....XggXXXXXXXXXXXXXXXXXXXXXX...",
  ".....XggX.XggX......XggX.XggX....",
  ".....XXX...XXX.......XXX..XXX....",
};

enum Mood { BOOT, ACTIVE, SLEEPY, BREAK_DUE, ON_BREAK, DONE };
static Mood mood = BOOT;

static uint16_t idleSeconds  = 0;
static bool     kbdActive    = false;
static bool     mouseActive  = false;
static uint32_t lastSerialMs = 0;
static bool     connected    = false;
static bool     gUsing = false, gPresent = false, gAway = false;
static char     dateStr[12] = "";
static char     timeStr[10] = "";

static uint32_t activeMs = 0, breakLeft = 0;
static bool     overworking = false;
static bool     bootJustPressed = false;
static uint32_t lastTick = 0, moodSince = 0;
static uint32_t frameMs = 0, lastCaptionMs = 0, moveMs = 0;
static uint8_t  captionIdx = 0;

static float    starX[NSTARS];
static uint8_t  starY[NSTARS], starSz[NSTARS];
static float    driftX = 0, driftY = 0, driftVX = 0.020f, driftVY = 0.013f;

static uint16_t cBg, cStar, cBlk, cGrey, cTart, cFrost, cSprk, cCheek, cWhite,
                cText, cGreen, cAmber, cMuted, cDim, cBarBack, cBarGreen,
                cBarYellow, cBarOrange, cBarRed;
static uint16_t rb[6];

#define ARRN(a) (sizeof(a) / sizeof(a[0]))
static const char* capBoot[]   = {"hello?", "run the helper", "boot me up"};
static const char* capReady[]  = {"press BOOT to focus", "ready when you are", "tap BOOT to start", "let's focus?"};
static const char* capActive[] = {"nyan nyan~", "grinding, huh?", "i'm watching", "workaholic.", "good human"};
static const char* capIdle[]   = {"still there?", "type something", "you idle?", "anyone home?", "...waiting"};
static const char* capSleepy[] = {"...boring", "did you leave?", "hellooo?", "so lonely", "zzz"};
static const char* capDue[]    = {"press BOOT", "break time!", "your spine called", "save and rest", "BOOT = break"};
static const char* capGrumpy[] = {"press BOOT already", "still going?!", "rude.", "red zone!", "BREAK. NOW."};
static const char* capBreak[]  = {"break started", "hands off laptop", "stay put...", "breathe."};
static const char* capNag[]    = {"NOPE. sit down.", "break's NOT over!", "back to the couch!"};
static const char* capDone[]   = {"break complete", "move mouse to restart", "welcome back?"};

static void setMood(Mood m);
static void readButton();
static bool takeBootPress();
static void startBreak();
static void readSerial();
static void updateManual(uint32_t dt, bool bootPressed, uint32_t now);
static void updateState();
static void render();

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
  tft.init();
  tft.setRotation(1);
  randomSeed(micros());

  cBg    = tft.color565(0, 85, 170);
  cStar  = tft.color565(255, 255, 255);
  cBlk   = tft.color565(0, 0, 0);
  cGrey  = tft.color565(170, 170, 170);
  cTart  = tft.color565(255, 170, 170);
  cFrost = tft.color565(255, 170, 255);
  cSprk  = tft.color565(255, 85, 170);
  cCheek = tft.color565(255, 170, 170);
  cWhite = TFT_WHITE;
  cText  = tft.color565(232, 236, 248);
  cGreen = tft.color565(150, 240, 180);
  cAmber = tft.color565(255, 210, 96);
  cMuted = tft.color565(186, 196, 222);
  cDim   = tft.color565(96, 104, 140);
  cBarBack = tft.color565(0, 45, 96);
  cBarGreen = tft.color565(30, 230, 80);
  cBarYellow = tft.color565(255, 232, 0);
  cBarOrange = tft.color565(255, 140, 0);
  cBarRed = tft.color565(255, 32, 32);
  rb[0] = tft.color565(255, 0, 0);    rb[1] = tft.color565(255, 170, 0);
  rb[2] = tft.color565(255, 255, 0);  rb[3] = tft.color565(85, 255, 0);
  rb[4] = tft.color565(0, 170, 255);  rb[5] = tft.color565(85, 85, 255);

  for (int i = 0; i < NSTARS; i++) { starX[i] = random(0, 240); starY[i] = random(2, 133); starSz[i] = random(1, 4); }

  spr.setColorDepth(16);
  spr.createSprite(240, 135);
  lastTick = moodSince = frameMs = moveMs = millis();
}

void loop() {
  readButton();
  readSerial();
  updateState();

  uint32_t now = millis();
  float mdt = now - moveMs; moveMs = now;
  float sp = gUsing ? 0.07f : 0.012f;
  for (int i = 0; i < NSTARS; i++) {
    starX[i] -= sp * mdt;
    if (starX[i] < -3) { starX[i] += 246; starY[i] = random(2, 133); }
  }

  if (mood == SLEEPY) {                              // idle: no rainbow, nap & drift through space
    driftX += driftVX * mdt;  driftY += driftVY * mdt;
    if (driftX < -124) { driftX = -124; driftVX =  fabsf(driftVX); }
    if (driftX >   15) { driftX =   15; driftVX = -fabsf(driftVX); }
    if (driftY <  -10) { driftY =  -10; driftVY =  fabsf(driftVY); }
    if (driftY >   14) { driftY =   14; driftVY = -fabsf(driftVY); }
  } else {                                           // ease back to the launch spot
    float k = 0.006f * mdt; if (k > 1) k = 1;
    driftX += -driftX * k;  driftY += -driftY * k;
  }

  if (now - frameMs > 40) { frameMs = now; render(); }
  if (now - lastCaptionMs > 3500) { lastCaptionMs = now; captionIdx++; }

  static uint32_t hbMs = 0;                          // heartbeat so the PC can see we're alive
  if (now - hbMs > 1000) {
    hbMs = now;
    Serial.printf("[cat] link=%d mood=%d idle=%d k=%d m=%d\n",
                  connected ? 1 : 0, (int)mood, idleSeconds, kbdActive ? 1 : 0, mouseActive ? 1 : 0);
  }
}

static void setMood(Mood m) {
  if (m != mood) { mood = m; moodSince = millis(); captionIdx = 0; lastCaptionMs = millis(); }
}

static void readButton() {
  static bool lastRaw = HIGH;
  static bool stable = HIGH;
  static uint32_t changedAt = 0;

  bool raw = digitalRead(BOOT_BUTTON_PIN);
  uint32_t now = millis();
  if (raw != lastRaw) {
    lastRaw = raw;
    changedAt = now;
  }
  if (now - changedAt > 35 && raw != stable) {
    stable = raw;
    if (stable == LOW) bootJustPressed = true;
  }
}

static bool takeBootPress() {
  bool pressed = bootJustPressed;
  bootJustPressed = false;
  return pressed;
}

static void startBreak() {
  breakLeft = BREAK_MS;
  overworking = false;
  setMood(ON_BREAK);
}

static void readSerial() {
  static char buf[64];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      buf[len] = 0;
      if (len > 0) {
        char* p;
        if ((p = strstr(buf, "idle="))) {
          idleSeconds = (uint16_t)atoi(p + 5);
          lastSerialMs = millis(); connected = true;
          if ((p = strstr(buf, "k="))) kbdActive   = atoi(p + 2);
          if ((p = strstr(buf, "m="))) mouseActive = atoi(p + 2);
          if ((p = strstr(buf, "t="))) {
            char tmp[24]; int i = 0; char* q = p + 2;
            while (q[i] && q[i] != ' ' && i < 23) { tmp[i] = q[i]; i++; }
            tmp[i] = 0;
            char* T = strchr(tmp, 'T');
            if (T) { *T = 0; strncpy(dateStr, tmp, 11); dateStr[11] = 0; strncpy(timeStr, T + 1, 9); timeStr[9] = 0; }
          }
        } else if (strcmp(buf, "break") == 0) {
          activeMs = WORK_MS;
          overworking = false;
          setMood(BREAK_DUE);
        } else if (strcmp(buf, "reset") == 0) {
          activeMs = 0;
          breakLeft = 0;
          overworking = false;
          setMood(BOOT);
        } else if (strcmp(buf, "boot") == 0) {
          bootJustPressed = true;       // simulate a BOOT press (testing)
        }
      }
      len = 0;
    } else if (len < sizeof(buf) - 1) buf[len++] = c;
  }
}

// Standalone mode (no helper): a wall-clock 30-min focus <-> 5-min rest loop.
// BOOT starts the rest now from anywhere; during a rest, BOOT ends it early.
static void updateManual(uint32_t dt, bool bootPressed, uint32_t now) {
  (void)now;
  if (bootPressed) {
    if (mood == ON_BREAK) { breakLeft = 0; activeMs = 0; setMood(ACTIVE); }   // end rest -> focus
    else                  { startBreak(); }                                    // BOOT -> rest now
  } else switch (mood) {
    case BOOT:
    case SLEEPY:
    case DONE:      activeMs = 0; breakLeft = 0; overworking = false; setMood(ACTIVE); break;  // focus
    case ACTIVE:    activeMs += dt; if (activeMs >= WORK_MS) { overworking = false; setMood(BREAK_DUE); } break;
    case BREAK_DUE: activeMs += dt; overworking = activeMs >= PULSE_AFTER_MS; break;
    case ON_BREAK:  if (breakLeft > dt) breakLeft -= dt;
                    else { breakLeft = 0; activeMs = 0; setMood(ACTIVE); } break;   // rest done -> fresh 30-min focus
  }
}

static void updateState() {
  uint32_t now = millis();
  uint32_t dt = now - lastTick;
  lastTick = now;
  bool bootPressed = takeBootPress();

  if (connected && now - lastSerialMs > SERIAL_TIMEOUT) { connected = false; kbdActive = mouseActive = false; }
  gUsing  = connected && (idleSeconds < WORK_IDLE_SECONDS || kbdActive || mouseActive);
  gPresent = connected && idleSeconds < IDLE_AWAY_SECONDS;
  gAway   = connected && idleSeconds >= IDLE_AWAY_SECONDS;

  if (!connected) { updateManual(dt, bootPressed, now); return; }   // standalone Pomodoro

  // BOOT = take the 5-min rest now (interrupt from any working state); during a
  // rest, BOOT ends it early. After a rest, a fresh 30-min focus begins.
  if (bootPressed) {
    if (mood == ON_BREAK) { breakLeft = 0; activeMs = 0; setMood(ACTIVE); }
    else                  { startBreak(); }
  } else switch (mood) {
    case BOOT:      activeMs = 0; overworking = false; setMood(gAway ? SLEEPY : ACTIVE); break;
    case ACTIVE:    if (gUsing) { activeMs += dt; if (activeMs >= WORK_MS) { overworking = false; setMood(BREAK_DUE); } }
                    if (gAway) setMood(SLEEPY); break;
    case SLEEPY:    if (gPresent) setMood(ACTIVE); break;
    case BREAK_DUE: if (gUsing) activeMs += dt; overworking = activeMs >= PULSE_AFTER_MS; break;
    case ON_BREAK:  if (breakLeft > dt) breakLeft -= dt;
                    else { breakLeft = 0; activeMs = 0; setMood(ACTIVE); } break;   // rest done -> fresh focus
    case DONE:      if (gUsing) { activeMs = 0; setMood(ACTIVE); } break;
  }
}

// ---------------------------------------------------------------------------
//  drawing
// ---------------------------------------------------------------------------
static void drawStars() {
  for (int i = 0; i < NSTARS; i++) {
    int x = (int)starX[i], y = starY[i], s = starSz[i];
    spr.fillRect(x - s, y - 1, 2 * s + 1, 3, cStar);
    spr.fillRect(x - 1, y - s, 3, 2 * s + 1, cStar);
  }
}

static uint16_t catColor(char ch) {
  switch (ch) {
    case 'X': return cBlk;   case 'g': return cGrey;  case 't': return cTart;
    case 'f': return cFrost; case 's': return cSprk;  case 'c': return cCheek;
    case 'w': return cWhite;
  }
  return cFrost;
}

static void blitCat(int ox, int oy) {
  for (int r = 0; r < 18; r++) {        // rows 18-19 (paws) are animated by drawLegs()
    const char* row = CAT[r];
    for (int c = 0; c < CAT_W; c++) {
      char ch = row[c];
      if (ch == '.') continue;
      spr.fillRect(ox + c * CELL, oy + r * CELL, CELL, CELL, catColor(ch));
    }
  }
}

static void heart(int x, int y, uint16_t col) {
  spr.fillCircle(x - 2, y, 2, col); spr.fillCircle(x + 2, y, 2, col);
  spr.fillTriangle(x - 3, y + 1, x + 3, y + 1, x, y + 5, col);
}

// 4 paws that paddle while flying (rows 18-19 of the sprite, drawn live)
static void drawLegs(int ox, int oy, bool moving) {
  static const int legCol[4] = {5, 10, 20, 25};
  int phase = (millis() / 110) % 2;
  for (int i = 0; i < 4; i++) {
    int x = ox + legCol[i] * CELL;
    int lift = (moving && ((i + phase) % 2)) ? CELL : 0;   // alternate paws kick up
    int y = oy + 18 * CELL - lift;
    spr.fillRect(x, y, 4 * CELL, CELL, cBlk);              // X..X
    spr.fillRect(x + CELL, y, 2 * CELL, CELL, cGrey);      // .gg.
    spr.fillRect(x, y + CELL, 3 * CELL, CELL, cBlk);       // XXX (paw)
  }
}

static void drawNyan(int ox, int oy, bool flying) {
  if (flying) {                                   // staircase rainbow, scales with the sprite
    int bandH = 2 * CELL;
    int top   = BASE_OY + 3 * CELL;
    int seg   = 8 * CELL;
    int rightEdge = ox + 6 * CELL;                // tuck behind the tail
    int ph = (millis() / 130) % 2;
    for (int x = 0, i = 0; x < rightEdge; x += seg, i++) {
      int off = ((i + ph) % 2) ? 0 : bandH / 2;
      int w = (x + seg > rightEdge) ? (rightEdge - x) : seg;
      for (int b = 0; b < 6; b++) spr.fillRect(x, top + off + b * bandH, w, bandH, rb[b]);
    }
  }
  blitCat(ox, oy);
  drawLegs(ox, oy, flying);

  spr.setTextDatum(TL_DATUM);
  int catRight = ox + CAT_W * CELL;
  if (mood == SLEEPY || mood == ON_BREAK) {       // napping: closed eyes + drifting zzz
    static const int eyeCol[2] = {21, 28};
    for (int e = 0; e < 2; e++) {                  // close the eyes
      int ex = ox + eyeCol[e] * CELL, ey = oy + 11 * CELL;
      spr.fillRect(ex - CELL, ey, 3 * CELL, 2 * CELL, cGrey);
      spr.fillRect(ex - CELL, ey + CELL, 3 * CELL, CELL, cBlk);
    }
    int zb = ((millis() / 400) % 2) ? 0 : 3;
    spr.setTextColor(cMuted, cBg);
    spr.drawString("z", catRight - 14, oy + 4 - zb, 2);
    spr.drawString("Z", catRight - 4, oy - 8 - zb, 4);
  } else if (mood == BREAK_DUE) {
    int b = ((millis() / 180) % 2) ? 0 : 4;
    spr.setTextColor(cAmber, cBg); spr.drawString("!", catRight - 8, oy - 12 - b, 4);
  } else if (flying && mood == ACTIVE) {          // occasional floating heart while working
    uint32_t tt = millis() % 6000;
    if (tt < 1600) heart(catRight - 16, oy + 2 - (int)(tt / 90), cSprk);
  }
}

static void drawIndicators(int x, int y) {
  uint16_t kc = (connected && kbdActive) ? cGreen : cDim;
  spr.fillRect(x, y, 26, 13, kc);
  for (int r = 0; r < 2; r++) for (int i = 0; i < 4; i++) spr.fillRect(x + 3 + i * 5, y + 3 + r * 4, 3, 2, cBg);
  spr.fillRect(x + 6, y + 10, 12, 2, cBg);
  int mx = x + 36, my = y + 7;
  uint16_t mc = (connected && mouseActive) ? cGreen : cDim;
  spr.fillCircle(mx, my - 4, 5, mc); spr.fillCircle(mx, my + 2, 5, mc); spr.fillRect(mx - 5, my - 4, 10, 6, mc);
  spr.drawLine(mx, my - 8, mx, my - 2, cBg);
}

static const char* currentCaption(uint16_t& col) {
  const char** a; uint8_t n;
  switch (mood) {
    case ACTIVE:    if (connected && !gUsing) { a = capIdle; n = ARRN(capIdle); col = cMuted; }
                    else { a = capActive; n = ARRN(capActive); col = cGreen; } break;
    case SLEEPY:    if (!connected) { a = capReady; n = ARRN(capReady); col = cText; }
                    else            { a = capSleepy; n = ARRN(capSleepy); col = cMuted; } break;
    case BREAK_DUE: if (overworking) { a = capGrumpy; n = ARRN(capGrumpy); } else { a = capDue; n = ARRN(capDue); } col = cAmber; break;
    case ON_BREAK:  if (gUsing) { a = capNag; n = ARRN(capNag); col = cAmber; } else { a = capBreak; n = ARRN(capBreak); col = cGreen; } break;
    case DONE:      a = capDone; n = ARRN(capDone); col = cGreen; break;
    default:        a = capBoot; n = ARRN(capBoot); col = cMuted; break;
  }
  return a[captionIdx % n];
}

static String mmss(uint32_t ms) {
  uint32_t s = ms / 1000; char b[8];
  sprintf(b, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
  return String(b);
}

static uint32_t workLeftMs() {
  return (activeMs >= WORK_MS) ? 0 : (WORK_MS - activeMs);
}

static uint16_t blendRgb(uint8_t r0, uint8_t g0, uint8_t b0,
                         uint8_t r1, uint8_t g1, uint8_t b1,
                         uint32_t n, uint32_t d) {
  if (d == 0) return tft.color565(r1, g1, b1);
  if (n > d) n = d;
  uint8_t r = r0 + ((int32_t)r1 - r0) * (int32_t)n / (int32_t)d;
  uint8_t g = g0 + ((int32_t)g1 - g0) * (int32_t)n / (int32_t)d;
  uint8_t b = b0 + ((int32_t)b1 - b0) * (int32_t)n / (int32_t)d;
  return tft.color565(r, g, b);
}

static uint16_t statusBarColor() {
  if (activeMs <= WORK_MS) return cBarGreen;

  uint32_t over = activeMs - WORK_MS;
  uint32_t span = PULSE_AFTER_MS - WORK_MS;
  if (over >= span) {
    return ((millis() / 260) % 2) ? cBarRed : cBarOrange;
  }

  uint32_t third = span / 3;
  if (over < third) {
    return blendRgb(30, 230, 80, 255, 232, 0, over, third);
  }
  if (over < third * 2) {
    return blendRgb(255, 232, 0, 255, 140, 0, over - third, third);
  }
  return blendRgb(255, 140, 0, 255, 32, 32, over - third * 2, span - third * 2);
}

static void drawStatusBar() {
  int pulse = (mood == BREAK_DUE && activeMs >= PULSE_AFTER_MS && ((millis() / 260) % 2)) ? 1 : 0;
  int x = STATUS_BAR_X;
  int y = STATUS_BAR_Y - pulse;
  int w = STATUS_BAR_W;
  int h = STATUS_BAR_H + pulse;
  int innerW = w - 2;
  int fillW; uint16_t fillCol;
  if (mood == ON_BREAK) {                 // rest: drain the bar back down (reverse)
    fillW = (int)((uint64_t)breakLeft * innerW / BREAK_MS);
    fillCol = cBarGreen;
  } else {                                // work: fill up, green -> red
    uint32_t shownMs = (activeMs > WORK_MS) ? WORK_MS : activeMs;
    fillW = (int)((uint64_t)shownMs * innerW / WORK_MS);
    fillCol = statusBarColor();
  }

  spr.drawRect(x, y, w, h, cBlk);
  spr.fillRect(x + 1, y + 1, innerW, h - 2, cBarBack);
  if (fillW > 0) spr.fillRect(x + 1, y + 1, fillW, h - 2, fillCol);
  if (mood == BREAK_DUE && activeMs >= PULSE_AFTER_MS) spr.drawRect(x - 1, y - 1, w + 2, h + 2, cWhite);
}

static void render() {
  spr.fillSprite(cBg);
  drawStars();

  bool flying = connected ? gUsing : (mood == ACTIVE);   // helper: real activity; standalone: focus session
  static const int BOB[4] = {-3, 0, 3, 0};
  int bob = flying ? BOB[(millis() / 130) % 4]
                   : (mood == ON_BREAK ? (((millis() / 700) % 2) ? 0 : 2) : 0);  // slow breathing during a rest
  drawNyan(OX + (int)driftX, BASE_OY + bob + (int)driftY, flying);

  spr.setTextDatum(TL_DATUM);
  if (mood == ON_BREAK)       { spr.setTextColor(cAmber, cBg); spr.drawString(mmss(breakLeft), 6, 4, 2); }
  else if (mood == BREAK_DUE) { spr.setTextColor(cAmber, cBg); spr.drawString(String("+") + mmss(activeMs - WORK_MS), 6, 4, 2); }
  else if (mood == DONE)      { spr.setTextColor(cGreen, cBg); spr.drawString("done", 6, 4, 2); }
  else if (mood == ACTIVE)    { spr.setTextColor((connected && !gUsing) ? cMuted : cText, cBg); spr.drawString(mmss(workLeftMs()), 6, 4, 2); }
  else if (connected)         { spr.setTextColor(cMuted, cBg); spr.drawString(mmss(workLeftMs()), 6, 4, 2); }
  else                        { spr.setTextColor(cMuted, cBg); spr.drawString("BOOT = focus", 6, 4, 2); }
  if (connected) drawIndicators(6, 20);

  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(cText, cBg);  spr.drawString(timeStr[0] ? timeStr : "--:--:--", 234, 4, 2);
  spr.setTextColor(cMuted, cBg); spr.drawString(dateStr[0] ? dateStr : "----------", 234, 19, 2);

  uint16_t col; const char* line = currentCaption(col);
  spr.setTextDatum(BC_DATUM);
  spr.setTextColor(col, cBg);
  spr.drawString(line, 120, 123, 2);
  drawStatusBar();

  spr.pushSprite(0, 0);
}
