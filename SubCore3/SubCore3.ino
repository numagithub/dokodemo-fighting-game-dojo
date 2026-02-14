/**
 * ========================================
 *   gamepad_display_v7 - SubCore3
 * ========================================
 * 
 * コマンド解析
 * 
 * - MainCoreから入力データを受信
 * - 波動拳、SA（スーパーアーツ）のコマンド判定
 * - 判定結果をMainCoreに送信
 * 
 * v7: v5ベース（安定版から再スタート）
 */

#if (SUBCORE != 3)
#error "This sketch should be compiled for SubCore3"
#endif

#include <MP.h>

//============================================================
// 定数定義
//============================================================

// メッセージID
#define MSG_GAMEPAD      1   // MainCoreからの入力データ
#define MSG_HISTORY      7   // 履歴データ（SubCore2へ送信）
#define MSG_BUTTON_STATS 8   // ボタン押下時間統計（1秒ごと）
#define MSG_FPS_SC3     10   // SC3 FPS情報
#define MSG_REACTION_START  11  // 反応速度テスト開始
#define MSG_REACTION_RESULT 12  // 反応速度テスト結果

// MSG_HISTORYデータ形式（32bit）
// ■ 新規追加（bit31 = 0）:
//   bit 31:    更新フラグ (0)
//   bit 30-21: frame (10bit, 0-999 = 0.0F-99.9F, 0.1F単位)
//   bit 20-17: dir (4bit)
//   bit 16-5:  btn (12bit)
//   bit 4-1:   color (4bit)
//   bit 0:     reserved
// ■ 色更新（bit31 = 1）:
//   bit 31:    更新フラグ (1)
//   bit 30-27: startIdx (4bit, 0-15: 色を付ける開始位置)
//   bit 26-23: endIdx (4bit, 0-15: 色を付ける終了位置)
//   bit 22-19: color (4bit)
//   bit 18-12: comboFrame (7bit, 0-127, 整数フレーム)
//   bit 11-0:  reserved
#define HIST_FLAG_UPDATE  0x80000000

// コンボ種別
enum ComboType : uint8_t {
  COMBO_NONE   = 0,
  COMBO_HADOU  = 1,  // 波動拳
  COMBO_SA     = 2,  // スーパーアーツ
  COMBO_SHORYU = 3   // 昇竜拳
};

// 方向値（4bitフラグ形式）
// bit 0 = UP (0x1), bit 1 = DOWN (0x2), bit 2 = LEFT (0x4), bit 3 = RIGHT (0x8)
enum Direction : int {
  DIR_NEUTRAL = 0,
  DIR_UP      = 1,
  DIR_DOWN    = 2,
  DIR_LEFT    = 4,
  DIR_UP_L    = 5,  // ↖
  DIR_DOWN_L  = 6,  // ↙
  DIR_RIGHT   = 8,
  DIR_UP_R    = 9,  // ↗
  DIR_DOWN_R  = 10  // ↘
};

// SOCDチェック（Simultaneous Opposite Cardinal Directions）
// 左右同時押し → ニュートラル、上下同時押し → ニュートラル
int applySOCD(int rawDir) {
  int dir = rawDir & 0xF;
  
  // 上下同時押し → 両方クリア
  if ((dir & 0x3) == 0x3) {
    dir &= ~0x3;
  }
  
  // 左右同時押し → 両方クリア
  if ((dir & 0xC) == 0xC) {
    dir &= ~0xC;
  }
  
  return dir;
}

// 波動拳パターン（↓ ↘ → + ボタン）
struct HadoukenPattern {
  int finalDir;  // 最終方向（→ or ←）
  int diagDir;   // 斜め（↘ or ↙）
  int downDir;   // 下（↓）
  const char* name;
};

static const HadoukenPattern HADOUKEN_PATTERNS[] = {
  { DIR_RIGHT, DIR_DOWN_R, DIR_DOWN, "RIGHT" },  // 右向き波動
  { DIR_LEFT,  DIR_DOWN_L, DIR_DOWN, "LEFT"  },  // 左向き波動
};
static const int HADOUKEN_PATTERN_COUNT = 2;

// 昇竜拳パターン（→ ↓ ↘ + ボタン）
struct ShoryukenPattern {
  int startDir;  // 開始方向（→ or ←）
  int downDir;   // 下（↓）
  int diagDir;   // 斜め（↘ or ↙）
  const char* name;
};

static const ShoryukenPattern SHORYUKEN_PATTERNS[] = {
  { DIR_RIGHT, DIR_DOWN, DIR_DOWN_R, "RIGHT" },  // 右向き昇竜
  { DIR_LEFT,  DIR_DOWN, DIR_DOWN_L, "LEFT"  },  // 左向き昇竜
};
static const int SHORYUKEN_PATTERN_COUNT = 2;

// 入力猶予の定数
static const int HADOUKEN_DIR_MAX_FRAMES = 11;  // 波動拳の方向キー完了猶予（↓↘→）
static const int HADOUKEN_BTN_MAX_FRAMES = 9;   // 波動拳のボタン猶予（最後の→からボタンまで）
static const int SA_DIR_MAX_FRAMES = 11;        // SAの方向キー完了猶予（各波動）
static const int SA_GAP_MAX_FRAMES = 12;        // SAの波動間ギャップ猶予
static const int SA_BTN_MAX_FRAMES = 10;        // SAのボタン猶予（最後の→からボタンまで）
static const int SHORYU_DIR_MAX_FRAMES = 11;    // 昇竜拳の方向キー完了猶予（→↓↘）
static const int SHORYU_BTN_MAX_FRAMES = 8;     // 昇竜拳のボタン猶予（↘からボタンまで）

//============================================================
// 履歴データ
//============================================================

#define MAX_HISTORY 16

struct InputEntry {
  int frame;
  int dir;
  uint16_t btn;
};

static InputEntry history[MAX_HISTORY];
static int histCount = 0;
static int lastDir = -1;
static uint16_t lastBtn = 0;
static unsigned long lastInputTime = 0;

// コンボ判定済みフラグ（重複判定防止）
static bool comboJudged = false;

// デバッグ: 入力カウント
static unsigned long inputRecvCount = 0;
static unsigned long inputChangeCount = 0;

// ループ周波数計測（Hz）
static unsigned long loopCountPerSec = 0;

// コンボ判定のクールダウン（全コンボ共通）
static unsigned long lastComboTime = 0;          // 最後にコンボ判定した時刻（millis）
static const unsigned long COMBO_COOLDOWN_MS = 800;  // コンボクールダウン時間（800ms）

// SA判定の追加クールダウン
static unsigned long lastSATime = 0;           // 最後にSA判定した時刻（millis）
static const unsigned long SA_COOLDOWN_MS = 500;  // SAクールダウン時間（500ms）

//============================================================
// ボタン押下時間計測
//============================================================

static const unsigned long BUTTON_OUTLIER_US = 200000;  // 200ms以上は外れ値
static const unsigned long STATS_SEND_INTERVAL = 1000; // 1秒ごとに送信

struct ButtonPressStats {
  unsigned long pressStartTime;  // 押された時刻（micros）
  bool isPressed;                // 現在押されているか
  
  // 移動平均
  uint16_t lastAvgDuration;      // 最後の平均値（0.1ms単位）
  bool hasNewPress;              // 新しい押下があったか
  uint16_t newPressDuration;     // 新しい押下時間（0.1ms単位）
  
  // 送信タイミング
  unsigned long lastSendTime;    // 最後に送信した時刻（millis）
};

static ButtonPressStats btnStats = {0, false, 0, false, 0, 0};

//============================================================
// コンボ判定結果
//============================================================

struct ComboResult {
  int endIdx;       // ↓のインデックス (-1=失敗)
  int totalFrames;  // 合計フレーム数（方向キーのみ）
  int buttonGrace;  // ボタン猶予（最後の→からボタンまで）
  
  bool isValid() const { return endIdx >= 0; }
};

//============================================================
// 関数プロトタイプ
//============================================================

void addHistory(int frame, int dir, uint16_t btn);
void checkCombo();
ComboResult checkHadoukenAt(int startIdx, const HadoukenPattern& pattern);
ComboResult checkShoryukenAt(int startIdx, const ShoryukenPattern& pattern);
bool checkSuperArts();
void sendComboColorUpdate(ComboType color, int startIdx, int endIdx, int comboFrame);

//============================================================
// メイン処理
//============================================================

void setup() {
  MP.begin();
  MP.RecvTimeout(MP_RECV_POLLING);
  
  MPLog("SubCore3: Combo analyzer started\n");
}

void loop() {
  unsigned long now = millis();
  unsigned long nowMicros = micros();
  loopCountPerSec++;
  
  int8_t msgid;
  uint32_t data;
  
  int ret = MP.Recv(&msgid, &data, 0);
  
  if (ret > 0 && msgid == MSG_GAMEPAD) {
    inputRecvCount++;
    int rawDir = data & 0xFF;
    uint16_t btn = (data >> 8) & 0xFFFF;
    
    // SOCDチェック適用（左右同時→N、上下同時→N）
    int dir = applySOCD(rawDir);
    
    // ボタン押下時間計測
    bool wasPressed = btnStats.isPressed;
    bool isPressed = (btn != 0);
    
    if (!wasPressed && isPressed) {
      // ボタンが押された
      btnStats.pressStartTime = nowMicros;
      btnStats.isPressed = true;
      MPLog("[SC3] Btn PRESS at %lu us\n", nowMicros);
    }
    else if (wasPressed && !isPressed) {
      // ボタンが離された
      unsigned long duration = nowMicros - btnStats.pressStartTime;
      btnStats.isPressed = false;
      
      MPLog("[SC3] Btn RELEASE: duration=%lu us (%s)\n", 
            duration, (duration < BUTTON_OUTLIER_US) ? "valid" : "outlier");
      
      // 200ms未満なら有効値として記録
      if (duration < BUTTON_OUTLIER_US) {
        // μs → 0.1ms単位に変換
        uint16_t durationDecims = duration / 100;
        if (durationDecims > 0xFFFF) durationDecims = 0xFFFF;
        
        // 移動平均: (lastAvg + newValue) / 2
        if (btnStats.lastAvgDuration == 0) {
          // 初回は新しい値をそのまま使用
          btnStats.newPressDuration = durationDecims;
        } else {
          // 2回目以降は移動平均
          btnStats.newPressDuration = (btnStats.lastAvgDuration + durationDecims) / 2;
        }
        btnStats.hasNewPress = true;
      }
    }
    
    // フレーム計算（前回入力からの経過時間、0.1F単位）
    // 1フレーム = 16.67ms, 0.1フレーム = 1.667ms
    int frameDecims = 10;  // 最小1.0F
    if (lastInputTime > 0) {
      unsigned long elapsed = now - lastInputTime;
      frameDecims = (elapsed * 10 / 17) + 10;  // 0.1F単位 (17ms ≈ 1F)
      if (frameDecims > 999) frameDecims = 999;  // 最大99.9F
    }
    
    // 入力が変化した場合のみ処理（SOCDチェック後の値で比較）
    if (dir != lastDir || btn != lastBtn) {
      inputChangeCount++;
      
      // 前回の入力を履歴に追加（コンボ判定も行う）
      if (lastDir >= 0) {
        addHistory(frameDecims, lastDir, lastBtn);
      }
      
      lastDir = dir;
      lastBtn = btn;
      lastInputTime = now;
    }
  }
  
  // 1秒ごとにボタン押下時間統計を送信
  if (now - btnStats.lastSendTime >= STATS_SEND_INTERVAL) {
    // 新しい押下があった場合のみ平均値を更新
    if (btnStats.hasNewPress) {
      btnStats.lastAvgDuration = btnStats.newPressDuration;
      btnStats.hasNewPress = false;
    }
    // ボタンが押されなければ lastAvgDuration をそのまま保持
    
    // デバッグログ
    MPLog("[SC3] STATS: recv=%lu, chg=%lu, avgMs=%d.%d\n", 
          inputRecvCount, inputChangeCount,
          btnStats.lastAvgDuration / 10, btnStats.lastAvgDuration % 10);
    
    // MSG_BUTTON_STATS送信（lastAvgDurationを送信）
    // bit 31-16: avgDuration（0.1ms単位）, bit 15-8: sampleCount（互換性のため1を送信）
    uint8_t sampleCount = (btnStats.lastAvgDuration > 0) ? 1 : 0;
    uint32_t statsData = ((uint32_t)btnStats.lastAvgDuration << 16) | 
                         ((sampleCount & 0xFF) << 8);
    MP.Send(MSG_BUTTON_STATS, statsData);
    
    // ループ周波数を送信（kHz単位）
    uint32_t loopHz = loopCountPerSec / 1000;  // kHz
    MP.Send(MSG_FPS_SC3, loopHz);
    loopCountPerSec = 0;
    
    btnStats.lastSendTime = now;
  }
  
  delayMicroseconds(100);
}

//============================================================
// 履歴管理
//============================================================

void addHistory(int frame, int dir, uint16_t btn) {
  // 履歴をシフト
  for (int i = MAX_HISTORY - 1; i > 0; i--) {
    history[i] = history[i - 1];
  }
  
  // 新しいエントリを追加
  history[0].frame = frame;
  history[0].dir = dir;
  history[0].btn = btn;
  
  if (histCount < MAX_HISTORY) histCount++;
  
  // ボタンがある場合はコンボ判定
  // ※履歴データは送信しない（SubCore3内部でのみ使用）
  // ※コンボ判定成功時のみ色更新メッセージを送信
  if (btn != 0) {
    checkCombo();
  }
}

//============================================================
// コンボ判定
//============================================================

// 現在入力でコンボ判定（履歴に追加する前）- 現在は使用しない
// void checkComboForCurrent(int frame, int dir, uint16_t btn) { ... }

// コンボ判定（履歴追加後）
void checkCombo() {
  if (histCount < 3) return;
  if (history[0].btn == 0) return;
  
  // 全コンボ共通のクールダウンチェック（300ms以内は再判定しない）
  unsigned long now = millis();
  if (now - lastComboTime < COMBO_COOLDOWN_MS) {
    return;
  }
  
  // SA判定を先にチェック
  if (checkSuperArts()) {
    lastComboTime = now;  // コンボ判定成功 → タイムスタンプ記録
    return;
  }
  
  // 昇竜拳判定（波動拳より先にチェック：↓↘→と→↓↘の誤判定を防ぐ）
  for (int p = 0; p < SHORYUKEN_PATTERN_COUNT; p++) {
    ComboResult result = checkShoryukenAt(0, SHORYUKEN_PATTERNS[p]);
    if (result.isValid()) {
      // ボタン猶予チェック（↘からボタンまで）
      if (result.buttonGrace > SHORYU_BTN_MAX_FRAMES) continue;
      
      // ボタン押下行も含めて色をつける
      sendComboColorUpdate(COMBO_SHORYU, 0, result.endIdx, result.totalFrames);
      MPLog("SHORYUKEN %s! %dF (btnGrace=%d, end=%d)\n", 
            SHORYUKEN_PATTERNS[p].name, result.totalFrames, result.buttonGrace, result.endIdx);
      lastComboTime = now;  // コンボ判定成功 → タイムスタンプ記録
      return;
    }
  }
  
  // 波動拳判定
  for (int p = 0; p < HADOUKEN_PATTERN_COUNT; p++) {
    ComboResult result = checkHadoukenAt(0, HADOUKEN_PATTERNS[p]);
    if (result.isValid()) {
      // ボタン猶予チェック（最後の→からボタンまで）
      if (result.buttonGrace > HADOUKEN_BTN_MAX_FRAMES) continue;
      
      // ボタン押下行も含めて色をつける
      sendComboColorUpdate(COMBO_HADOU, 0, result.endIdx, result.totalFrames);
      MPLog("HADOUKEN %s! %dF (btnGrace=%d, end=%d)\n", 
            HADOUKEN_PATTERNS[p].name, result.totalFrames, result.buttonGrace, result.endIdx);
      lastComboTime = now;  // コンボ判定成功 → タイムスタンプ記録
      return;
    }
  }
}

// 波動拳パターンをチェック（指定indexから）
// startIdx=0: ボタン押下行からチェック（通常の波動拳判定）
// startIdx>0: SA判定用（1回目の波動拳、ボタン押下なし）
// 
// 0.1F単位のフレームを整数フレームに変換（四捨五入）
inline int toIntFrame(int frameDecims) {
  return (frameDecims + 5) / 10;
}

// 技フレーム = ↘→の時間のみ
// ・↓のフレームはカウントしない（押しっぱなしでもOK）
// ・ボタン押下のフレームはカウントしない
// ・ボタン猶予（最後の→からボタンまで）はカウントしない
// ※内部計算は0.1F単位で行い、最後に整数に変換
ComboResult checkHadoukenAt(int startIdx, const HadoukenPattern& pattern) {
  ComboResult result = { -1, 0 };
  
  if (startIdx >= histCount) return result;
  
  int idx = startIdx;
  int totalDecims = 0;    // 0.1F単位で累積
  int buttonGraceDecims = 0;  // ボタン猶予（0.1F単位）
  // startIdx == 0 は checkCombo() から呼ばれた場合（btn != 0 のとき）
  // なので、startIdx == 0 なら常にボタン押下として扱う
  bool isButtonPress = (startIdx == 0);
  
  // 最初が最終方向か
  if (history[idx].dir != pattern.finalDir) return result;
  
  // history[0]の最終方向（→）のフレームは技フレームに含める
  totalDecims += history[idx].frame;
  idx++;
  
  if (isButtonPress) {
    // 連続する最終方向（→）があればボタン猶予として扱う
    // （例: ↘→で一度止まって、再度→を押してからボタンを押した場合）
    while (idx < histCount && history[idx].dir == pattern.finalDir) {
      buttonGraceDecims += history[idx].frame;
      idx++;
      if (toIntFrame(buttonGraceDecims) > HADOUKEN_BTN_MAX_FRAMES) return result;
    }
  }
  
  // 連続する最終方向をスキップ（フレームはカウント）
  while (idx < histCount && history[idx].dir == pattern.finalDir) {
    totalDecims += history[idx].frame;
    idx++;
    if (toIntFrame(totalDecims) > HADOUKEN_DIR_MAX_FRAMES) return result;
  }
  
  // 斜めを探す（フレームはカウント）
  if (idx >= histCount || history[idx].dir != pattern.diagDir) return result;
  totalDecims += history[idx].frame;
  if (toIntFrame(totalDecims) > HADOUKEN_DIR_MAX_FRAMES) return result;
  idx++;
  
  // 下を探す（↓のフレームはカウントしない：押しっぱなしでもOK）
  if (idx >= histCount || history[idx].dir != pattern.downDir) return result;
  
  // 最後に整数フレームに変換
  int totalFrames = toIntFrame(totalDecims);
  if (totalFrames > HADOUKEN_DIR_MAX_FRAMES) return result;
  
  result.endIdx = idx;
  result.totalFrames = totalFrames;
  result.buttonGrace = toIntFrame(buttonGraceDecims);
  return result;
}

// 昇竜拳パターンをチェック（→ ↓ ↘ + ボタン）
// 許容パターン：
//   → ↓ ↘ + ボタン（正式）
//   → ↘ ↓ ↘ + ボタン（ショートカット）
//   → N ↓ ↘ + ボタン（途中にニュートラル）
// 技フレーム = →から↘までの時間（途中のN/↘含む）
// ボタン猶予 = 最後の↘からボタンまでの時間
ComboResult checkShoryukenAt(int startIdx, const ShoryukenPattern& pattern) {
  ComboResult result = { -1, 0, 0 };
  
  if (startIdx >= histCount) return result;
  
  int idx = startIdx;
  int totalDecims = 0;    // 0.1F単位で累積
  int buttonGraceDecims = 0;  // ボタン猶予（0.1F単位）
  bool isButtonPress = (startIdx == 0);
  
  // 最初が斜め（↘ or ↙）か
  if (history[idx].dir != pattern.diagDir) return result;
  
  // history[0]の斜めのフレームは技フレームに含める
  totalDecims += history[idx].frame;
  idx++;
  
  if (isButtonPress) {
    // 連続する斜め（↘）やニュートラルがあればボタン猶予として扱う
    while (idx < histCount && 
           (history[idx].dir == pattern.diagDir || history[idx].dir == DIR_NEUTRAL)) {
      buttonGraceDecims += history[idx].frame;
      idx++;
      if (toIntFrame(buttonGraceDecims) > SHORYU_BTN_MAX_FRAMES) return result;
    }
  }
  
  // ↓を探す（途中の↘やニュートラルをスキップしながらフレーム加算）
  while (idx < histCount && 
         (history[idx].dir == pattern.diagDir || history[idx].dir == DIR_NEUTRAL)) {
    totalDecims += history[idx].frame;
    idx++;
    if (toIntFrame(totalDecims) > SHORYU_DIR_MAX_FRAMES) return result;
  }
  
  // ↓があるか
  if (idx >= histCount || history[idx].dir != pattern.downDir) return result;
  totalDecims += history[idx].frame;
  if (toIntFrame(totalDecims) > SHORYU_DIR_MAX_FRAMES) return result;
  idx++;
  
  // →を探す（途中の↘やニュートラルをスキップしながらフレーム加算）
  while (idx < histCount && 
         (history[idx].dir == pattern.diagDir || history[idx].dir == DIR_NEUTRAL)) {
    totalDecims += history[idx].frame;
    idx++;
    if (toIntFrame(totalDecims) > SHORYU_DIR_MAX_FRAMES) return result;
  }
  
  // →があるか
  if (idx >= histCount || history[idx].dir != pattern.startDir) return result;
  
  // 最後に整数フレームに変換
  int totalFrames = toIntFrame(totalDecims);
  if (totalFrames > SHORYU_DIR_MAX_FRAMES) return result;
  
  result.endIdx = idx;
  result.totalFrames = totalFrames;
  result.buttonGrace = toIntFrame(buttonGraceDecims);
  return result;
}

// SA判定（履歴のみ）
bool checkSuperArts() {
  if (histCount < 6) return false;
  
  // クールダウンチェック（500ms以内は再判定しない）
  unsigned long now = millis();
  if (now - lastSATime < SA_COOLDOWN_MS) {
    return false;
  }
  
  ComboResult second;
  const HadoukenPattern* pattern = nullptr;
  
  for (int p = 0; p < HADOUKEN_PATTERN_COUNT; p++) {
    second = checkHadoukenAt(0, HADOUKEN_PATTERNS[p]);
    if (second.isValid()) {
      pattern = &HADOUKEN_PATTERNS[p];
      break;
    }
  }
  
  if (!pattern) return false;
  if (history[0].btn == 0) return false;
  
  // SAのボタン猶予チェック（最後の→からボタンまで）
  if (second.buttonGrace > SA_BTN_MAX_FRAMES) return false;
  
  int gapDecims = 0;  // 0.1F単位
  int firstStartIdx = second.endIdx + 1;
  
  while (firstStartIdx < histCount && 
         (history[firstStartIdx].dir == DIR_DOWN_L || 
          history[firstStartIdx].dir == DIR_DOWN_R ||
          history[firstStartIdx].dir == DIR_NEUTRAL) &&
         toIntFrame(gapDecims) <= SA_GAP_MAX_FRAMES) {
    gapDecims += history[firstStartIdx].frame;
    firstStartIdx++;
  }
  
  int gapFrames = toIntFrame(gapDecims);
  if (gapFrames > SA_GAP_MAX_FRAMES) return false;
  if (firstStartIdx >= histCount) return false;
  
  ComboResult first = checkHadoukenAt(firstStartIdx, *pattern);
  if (!first.isValid()) return false;
  
  // 2回目の↓のフレームを加算（1回目と2回目の間の入力）
  int secondDownFrame = toIntFrame(history[second.endIdx].frame);
  
  // SA技フレーム = 1回目(→↘) + ギャップ + 2回目↓ + 2回目(↘→)
  // ※1回目の↓はカウントしない（押しっぱなしでもOK）
  // ※ボタン猶予はカウントしない
  // first.totalFramesには1回目の→↘が含まれている（startIdx>0なので最初の→もカウント済み）
  int totalFrames = first.totalFrames + gapFrames + secondDownFrame + second.totalFrames;
  
  // SA成功 → タイムスタンプを記録
  lastSATime = now;
  
  // SAは1回目の波動拳の終端まで色を付ける（ボタン押下行も含む）
  sendComboColorUpdate(COMBO_SA, 0, first.endIdx, totalFrames);
  MPLog("SUPER ARTS %s! %dF (1st=%d, gap=%d, 2ndDown=%d, 2nd=%d, btnGrace=%d)\n", 
        pattern->name, totalFrames, first.totalFrames, gapFrames, secondDownFrame, second.totalFrames, second.buttonGrace);
  return true;
}

// 履歴の色更新をMainCore経由でSubCore2に送信
// データ形式（色更新: bit31=1）:
//   bit 30-27: startIdx, bit 26-23: endIdx, bit 22-19: color, bit 18-12: comboFrame
void sendComboColorUpdate(ComboType color, int startIdx, int endIdx, int comboFrame) {
  uint32_t data = HIST_FLAG_UPDATE |
                  ((startIdx & 0xF) << 27) |
                  ((endIdx & 0xF) << 23) |
                  ((color & 0xF) << 19) |
                  ((comboFrame & 0x7F) << 12);
  
  int ret = MP.Send(MSG_HISTORY, data);
  if (ret < 0) {
    MPLog("[SC3] MSG_HISTORY color update send failed: %d\n", ret);
  }
}
