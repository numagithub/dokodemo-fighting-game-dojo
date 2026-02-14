/**
 * ========================================
 *   gamepad_display_v7 - SubCore2
 * ========================================
 * 
 * LCD描画 + 履歴管理
 * 
 * - 入力履歴の管理と表示
 * - コンボ表示（判定結果はSubCore3から受信）
 * - カメラプレビュー描画
 * - ボタンパネル描画
 * - 描画は1フレームに最大5行ずつ分散
 * 
 * v7: v5ベース（安定版から再スタート）
 */

#if (SUBCORE != 2)
#error "This sketch should be compiled for SubCore2"
#endif

#include <MP.h>

// LovyanGFXライブラリのインクルード
// ※ SubCore単体でコンパイルする場合は、Arduino IDEのライブラリマネージャーから
//    LovyanGFX をインストールする必要があります
#ifndef LGFX_AUTODETECT
  #define LGFX_AUTODETECT
#endif
#ifndef LGFX_USE_V1
  #define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>

//============================================================
// 定数定義（グループ化）
//============================================================

// --- 画面レイアウト ---
namespace Layout {
  // 画面分割
  static const int LEFT_AREA_W  = 120;   // 入力表示エリア幅
  static const int RIGHT_AREA_X = 120;   // プレビューエリア開始X
  
  // 現在入力行
  static const int CURRENT_W = LEFT_AREA_W;
  static const int CURRENT_H = 20;
  static const int CURRENT_Y = 30;
  
  // 履歴
  static const int HIST_Y    = 52;
  static const int HIST_ROWS = 12;  // 最終行を統計表示に使用
  static const int ROW_H     = 20;
  static const int MAX_DRAW_PER_FRAME = 5;
  
  // プレビュー（右上）- 2飛ばしで80x60
  static const int PREVIEW_W = 80;
  static const int PREVIEW_H = 60;
  static const int PREVIEW_X = RIGHT_AREA_X + 20;  // 中央寄せ (120-80)/2
  static const int PREVIEW_LABEL_Y = 32;
  static const int PREVIEW_Y = PREVIEW_LABEL_Y + 12;
  
  // コンボキャプチャ（プレビューの下）
  static const int COMBO_CAP_X = PREVIEW_X;
  static const int COMBO_LABEL_Y = PREVIEW_Y + PREVIEW_H + 10;
  static const int COMBO_CAP_Y = COMBO_LABEL_Y + 12;
  
  // 描画位置（行内）
  static const int FRAME_X  = 2;   // フレーム数
  static const int ARROW_X  = 18;  // 矢印
  static const int ARROW_Y  = 3;
  static const int ARROW_SZ = 14;
  static const int BTN_X    = 40;  // ボタン（矢印との隙間を広げた）
  static const int BTN_Y    = 2;
  static const int TEXT_Y   = 6;   // テキスト縦位置
  
  // ボタンパネル設定（右下）- コンボキャプチャの下
  static const int PANEL_Y      = COMBO_CAP_Y + PREVIEW_H + 10;  // コンボキャプチャの下
  static const int PANEL_R      = 7;     // ボタン半径（少し小さく）
  static const int PANEL_SP     = 18;    // ボタン間隔（少し狭く）
  // 十字キー位置
  static const int DPAD_CX      = RIGHT_AREA_X + 28;  // 十字キー中心X
  static const int DPAD_CY      = PANEL_Y + 22;       // 十字キー中心Y
  // フェイスボタン位置
  static const int FACE_CX      = RIGHT_AREA_X + 92;  // フェイスボタン中心X
  static const int FACE_CY      = PANEL_Y + 22;       // フェイスボタン中心Y
  // トリガーボタン位置
  static const int TRIG_Y       = PANEL_Y + 55;       // トリガーY
  static const int TRIG_L_X     = RIGHT_AREA_X + 22;  // 左トリガーX
  static const int TRIG_R_X     = RIGHT_AREA_X + 82;  // 右トリガーX
  
  // 統計表示位置（履歴エリアの下）
  static const int STATS_AREA_Y = HIST_Y + HIST_ROWS * ROW_H;  // 292
  
  // 左下: 反応速度テスト（2行: Action, React）
  static const int REACTION_STATS_X = 4;
  static const int REACTION_ACTION_Y = STATS_AREA_Y + 8;   // 300 Action行（上に移動）
  static const int REACTION_REACT_Y = STATS_AREA_Y + 20;   // 312 React行（12px間隔）
  
  // 右下: ボタン押下時間
  static const int BTN_STATS_X  = RIGHT_AREA_X + 5;
  static const int BTN_STATS_Y  = STATS_AREA_Y + 8;        // 300（Actionと同じ高さ）
  
  // 反応速度マーカー位置（"Key Display v7"の右側）
  static const int REACTION_MARKER_X = 110;  // "Key Display v7"の右側
  static const int REACTION_MARKER_Y = 17;   // テキストと同じ高さ（10 + フォント高さ/2）
  static const int REACTION_MARKER_R = 7;    // マーカー半径7px（小さく）
  
  // PadType表示位置（右上）
  static const int PAD_TYPE_X   = RIGHT_AREA_X + 5;
  static const int PAD_TYPE_Y   = 4;
  
  // FPS表示位置（PadTypeの下）
  static const int FPS_X        = RIGHT_AREA_X + 5;
  static const int FPS_Y        = 16;  // 1行: MainHz SC1Hz SC3Hz Dfps
}

// --- 色定義 ---
namespace Colors {
  static const uint16_t BG        = 0x0000;  // 背景黒
  static const uint16_t FRAME     = 0xFFFF;  // 白
  static const uint16_t DIR       = 0xFFFF;  // 白
  static const uint16_t COMBO     = 0xF800;  // 赤（波動拳）
  static const uint16_t SA        = 0x07E0;  // 緑（SA）
  static const uint16_t SHORYU    = 0xFD20;  // オレンジ（昇竜拳）
  static const uint16_t BG_COMBO  = 0x4000;  // 暗い赤
  static const uint16_t BG_SA     = 0x0320;  // 暗い緑
  static const uint16_t BG_SHORYU = 0x4200;  // 暗いオレンジ
}

// --- コンボ識別子 ---
enum ComboType : uint8_t {
  COMBO_NONE   = 0,
  COMBO_HADOU  = 1,  // 波動拳
  COMBO_SA     = 2,  // スーパーアーツ
  COMBO_SHORYU = 3   // 昇竜拳
};

// --- メッセージID ---
enum MsgId : int8_t {
  MSG_GAMEPAD      = 1,  // SubCore1からの入力データ
  MSG_DRAW         = 2,
  MSG_SET_BUFFER   = 3,
  MSG_CAMERA_READY = 4,
  MSG_PAD_STATUS   = 5,  // SubCore1からの接続状態
  MSG_HISTORY      = 7,  // SubCore3からの履歴データ（新規追加＋色更新）
  MSG_BUTTON_STATS = 8,  // SubCore3からのボタン押下時間統計
  MSG_FPS_SC1      = 9,  // SC1 FPS情報
  MSG_FPS_SC3      = 10, // SC3 FPS情報
  MSG_REACTION_START  = 11,  // 反応速度テスト開始
  MSG_REACTION_RESULT = 12,  // 反応速度テスト結果
  MSG_REACTION_MARKER_DRAWN = 14,  // マーカー描画完了（SubCore2→MainCore）
  MSG_FPS_MAIN     = 15   // MainCore Hz情報
};

// MSG_HISTORYデータ形式
// ■ 新規追加（bit31 = 0）: frame(10bit,0.1F単位)|dir(4)|btn(12)|color(4)|reserved(1)
// ■ 色更新（bit31 = 1）: startIdx(4)|endIdx(4)|color(4)|comboFrame(7)|reserved(12)
#define HIST_FLAG_UPDATE  0x80000000

// --- パッド種別 ---
enum PadType : uint8_t {
  PAD_DISCONNECTED = 0xFF,  // Pff: 非接続
  PAD_HID          = 0x01,  // P01: HID
  PAD_XINPUT       = 0x02   // P02: Xinput
};

// --- 画面状態 ---
enum ScreenState : uint8_t {
  SCREEN_WAITING,    // 待機画面（非接続）
  SCREEN_CONNECTED,  // 接続情報画面
  SCREEN_ACTIVE      // キーディス画面（通常）
};

// --- カメラ画像設定 ---
namespace Camera {
  static const int IMG_WIDTH  = 160;  // QQVGA
  static const int IMG_HEIGHT = 120;  // QQVGA
  static const int IMG_SIZE   = IMG_WIDTH * IMG_HEIGHT * 2;
  static const int SAMPLE_RATIO = 2;  // 縮小率（2飛ばし）
}

// --- ボタン定義（SF6スタイル） ---
// アイコン種別
enum IconType : uint8_t {
  ICON_TYPE_PUNCH = 0,
  ICON_TYPE_KICK  = 1,
  ICON_TYPE_OTHER = 2
};

// 強度別の色（SF6準拠）
namespace BtnColors {
  static const uint16_t LIGHT  = 0x5DFF;  // 水色（弱）
  static const uint16_t MEDIUM = 0xFFE0;  // 黄色（中）
  static const uint16_t HEAVY  = 0xF800;  // 赤（強）
  static const uint16_t OTHER  = 0xFFFF;  // 白（その他）
}

struct ButtonDef {
  uint16_t mask;      // ビットマスク
  uint16_t color;     // 表示色
  IconType iconType;  // アイコン種別
};

static const ButtonDef BUTTONS[] = {
  { 0x100, BtnColors::LIGHT,  ICON_TYPE_PUNCH },  // □ LP（弱パンチ）
  { 0x400, BtnColors::MEDIUM, ICON_TYPE_PUNCH },  // △ MP（中パンチ）
  { 0x020, BtnColors::HEAVY,  ICON_TYPE_PUNCH },  // R1 HP（強パンチ）
  { 0x200, BtnColors::LIGHT,  ICON_TYPE_KICK  },  // × LK（弱キック）
  { 0x800, BtnColors::MEDIUM, ICON_TYPE_KICK  },  // ○ MK（中キック）
  { 0x008, BtnColors::HEAVY,  ICON_TYPE_KICK  },  // R2 HK（強キック）
  { 0x010, BtnColors::OTHER,  ICON_TYPE_OTHER },  // L1
  { 0x004, BtnColors::OTHER,  ICON_TYPE_OTHER },  // L2
};
static const int BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);
static const int BUTTON_SPACING = 15;  // アイコン間隔

// --- 矢印ビットマップ (14x14, モノクロ) ---
#define ARROW_W 14
#define ARROW_H 14

// --- ボタンアイコン (14x14, モノクロ) ---
#define ICON_W 14
#define ICON_H 14

// パンチ（拳）14x14
static const uint8_t ICON_PUNCH[] PROGMEM = {
  0x07, 0x80,  0x18, 0x20,  0x20, 0x10,  0x45, 0x48,
  0x45, 0x48,  0x54, 0x08,  0x55, 0xE8,  0x50, 0x68,
  0x5F, 0xE8,  0x4F, 0xE8,  0x4F, 0xC8,  0x23, 0xD0,
  0x1B, 0xE0,  0x07, 0x80,
};

// キック（足）14x14
// 行0:  0 0 0 0 0 1 1 1 1 0 0 0 0 0
// 行1:  0 0 0 1 1 0 0 0 0 1 1 0 0 0
// 行2:  0 0 1 0 0 0 0 0 0 0 0 1 0 0
// 行3:  0 1 0 0 0 0 0 0 0 0 0 0 1 0
// 行4:  0 1 0 1 0 0 0 0 0 0 0 0 1 0
// 行5:  0 1 0 1 1 0 0 0 0 0 0 1 1 0
// 行6:  0 1 0 1 1 1 1 0 0 1 1 1 1 0
// 行7:  0 1 0 0 1 1 1 1 1 1 1 1 1 0
// 行8:  0 1 0 0 0 1 1 1 1 1 1 1 1 0
// 行9:  0 1 0 0 0 1 1 1 1 1 1 1 1 0
// 行10: 0 1 0 0 0 0 1 1 1 1 0 0 1 0
// 行11: 0 0 1 0 0 0 0 1 1 0 0 1 0 0
// 行12: 0 0 0 1 1 0 0 0 0 1 1 0 0 0
// 行13: 0 0 0 0 0 1 1 1 1 0 0 0 0 0
static const uint8_t ICON_KICK[] PROGMEM = {
  0x07, 0x80,  0x18, 0x60,  0x20, 0x10,  0x40, 0x08,
  0x50, 0x08,  0x58, 0x18,  0x5E, 0x78,  0x4F, 0xF8,
  0x47, 0xF8,  0x47, 0xF8,  0x43, 0xC8,  0x21, 0x90,
  0x18, 0x60,  0x07, 0x80,
};

// 上矢印 ↑ (左矢印を転置)
static const uint8_t ARROW_UP[] PROGMEM = {
  0x01, 0x00,  0x03, 0x80,  0x07, 0xC0,  0x0F, 0xE0,
  0x1F, 0xF0,  0x3F, 0xF8,  0x07, 0xC0,  0x07, 0xC0,
  0x07, 0xC0,  0x07, 0xC0,  0x07, 0xC0,  0x07, 0xC0,
  0x07, 0xC0,  0x07, 0xC0,
};

// 下矢印 ↓ (上矢印の上下反転)
static const uint8_t ARROW_DOWN[] PROGMEM = {
  0x07, 0xC0,  0x07, 0xC0,  0x07, 0xC0,  0x07, 0xC0,
  0x07, 0xC0,  0x07, 0xC0,  0x07, 0xC0,  0x07, 0xC0,
  0x3F, 0xF8,  0x1F, 0xF0,  0x0F, 0xE0,  0x07, 0xC0,
  0x03, 0x80,  0x01, 0x00,
};

// 左矢印 ←
static const uint8_t ARROW_LEFT[] PROGMEM = {
  0x00, 0x00,  0x00, 0x00,  0x04, 0x00,  0x0C, 0x00,
  0x1C, 0x00,  0x3F, 0xFC,  0x7F, 0xFC,  0xFF, 0xFC,
  0x7F, 0xFC,  0x3F, 0xFC,  0x1C, 0x00,  0x0C, 0x00,
  0x04, 0x00,  0x00, 0x00,
};

// 右矢印 → (左矢印の左右反転)
static const uint8_t ARROW_RIGHT[] PROGMEM = {
  0x00, 0x00,  0x00, 0x00,  0x00, 0x80,  0x00, 0xC0,
  0x00, 0xE0,  0xFF, 0xF0,  0xFF, 0xF8,  0xFF, 0xFC,
  0xFF, 0xF8,  0xFF, 0xF0,  0x00, 0xE0,  0x00, 0xC0,
  0x00, 0x80,  0x00, 0x00,
};

// 左上矢印 ↖
static const uint8_t ARROW_UP_L[] PROGMEM = {
  0x00, 0x00,  0xFF, 0x80,  0xFF, 0x00,  0xFE, 0x00,
  0xFF, 0x00,  0xFF, 0x80,  0xFF, 0xC0,  0xFF, 0xE0,
  0xDF, 0xF0,  0x8F, 0xF8,  0x07, 0xF0,  0x03, 0xE0,
  0x01, 0xC0,  0x00, 0x80,
};

// 右上矢印 ↗
static const uint8_t ARROW_UP_R[] PROGMEM = {
  0x00, 0x00,  0x07, 0xFC,  0x03, 0xFC,  0x01, 0xFC,
  0x03, 0xFC,  0x07, 0xFC,  0x0F, 0xFC,  0x1F, 0xFC,
  0x3F, 0xEC,  0x7F, 0xC4,  0x3F, 0x80,  0x1F, 0x00,
  0x0E, 0x00,  0x04, 0x00,
};

// 左下矢印 ↙
static const uint8_t ARROW_DOWN_L[] PROGMEM = {
  0x00, 0x80,  0x01, 0xC0,  0x03, 0xE0,  0x07, 0xF0,
  0x8F, 0xF8,  0xDF, 0xF0,  0xFF, 0xE0,  0xFF, 0xC0,
  0xFF, 0x80,  0xFF, 0x00,  0xFE, 0x00,  0xFF, 0x00,
  0xFF, 0x80,  0x00, 0x00,
};

// 右下矢印 ↘
static const uint8_t ARROW_DOWN_R[] PROGMEM = {
  0x04, 0x00,  0x0E, 0x00,  0x1F, 0x00,  0x3F, 0x80,
  0x7F, 0xC4,  0x3F, 0xEC,  0x1F, 0xFC,  0x0F, 0xFC,
  0x07, 0xFC,  0x03, 0xFC,  0x01, 0xFC,  0x03, 0xFC,
  0x07, 0xFC,  0x00, 0x00,
};

// ニュートラル (N)
static const uint8_t ARROW_NEUTRAL[] PROGMEM = {
  0x0F, 0x00,  0x30, 0xC0,  0x40, 0x20,  0x80, 0x10,
  0x90, 0x90,  0x98, 0x90,  0x94, 0x90,  0x92, 0x90,
  0x91, 0x90,  0x90, 0x90,  0x80, 0x10,  0x40, 0x20,
  0x30, 0xC0,  0x0F, 0x00,
};

// --- 方向値定義 ---
enum Direction : int {
  DIR_NEUTRAL = 0,
  DIR_UP      = 1,
  DIR_DOWN    = 2,
  DIR_LEFT    = 4,
  DIR_UP_L    = 5,
  DIR_DOWN_L  = 6,
  DIR_RIGHT   = 8,
  DIR_UP_R    = 9,
  DIR_DOWN_R  = 10
};

// 方向からビットマップを取得
const uint8_t* getArrowBitmap(int dir) {
  switch (dir) {
    case DIR_UP:     return ARROW_UP;
    case DIR_DOWN:   return ARROW_DOWN;
    case DIR_LEFT:   return ARROW_LEFT;
    case DIR_RIGHT:  return ARROW_RIGHT;
    case DIR_UP_L:   return ARROW_UP_L;
    case DIR_UP_R:   return ARROW_UP_R;
    case DIR_DOWN_L: return ARROW_DOWN_L;
    case DIR_DOWN_R: return ARROW_DOWN_R;
    default:         return ARROW_NEUTRAL;
  }
}

//============================================================
// LCD設定
//============================================================

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
public:
  LGFX(void) {
    {
      auto cfg = _bus.config();
      cfg.spi_mode = 0;
      cfg.spi_port = 4;
      cfg.freq_write = 20000000;
      cfg.freq_read = 16000000;
      cfg.pin_dc = 9;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 10;
      cfg.pin_rst = 8;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.bus_shared = true;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};

static LGFX lcd;
static LGFX_Sprite sprCurrent(&lcd);
static LGFX_Sprite sprHistRow(&lcd);

//============================================================
// 状態管理（構造体化）
//============================================================

// 入力状態
struct InputState {
  int frame;
  int dir;
  uint16_t btn;
  
  void reset() { frame = 0; dir = -1; btn = 0; }
  bool isValid() const { return dir >= 0; }
};

// コンボ状態（SubCore3から受信）
struct ComboState {
  ComboType type;       // コンボ種類
  int totalFrame;       // 合計フレーム
  bool pending;         // 表示待ち
  
  void reset() {
    type = COMBO_NONE;
    totalFrame = 0;
    pending = false;
  }
};

// 描画状態（差分描画用）
struct DrawState {
  int frame;
  int dir;
  uint16_t btn;
  
  void reset() { frame = -1; dir = -1; btn = 0xFFFF; }
  bool needsUpdate(int f, int d, uint16_t b) const {
    return (frame != f) || (dir != d) || (btn != b);
  }
  void update(int f, int d, uint16_t b) { frame = f; dir = d; btn = b; }
};

// 統計
struct Stats {
  unsigned long startTime;
  int camRecvCount;
  int drawRecvCount;
  int comboRecvCount;
  int inputChangeCount;  // 入力変化回数
  
  void reset() { startTime = millis(); camRecvCount = 0; drawRecvCount = 0; comboRecvCount = 0; inputChangeCount = 0; }
};

// デバッグ: 総入力変化カウント
static unsigned long totalInputChangeCount = 0;
static unsigned long lastInputTime = 0;  // 前回入力時刻（millis）

// グローバル状態
static InputState prevInput;
static ComboState currentCombo;
static DrawState lastDraw;
static Stats stats;

// 画面状態
static ScreenState currentScreen = SCREEN_WAITING;
static PadType currentPadType = PAD_DISCONNECTED;
static unsigned long connectedTime = 0;
static const unsigned long CONNECTED_DISPLAY_MS = 2000;

// 共有メモリ（カメラ画像）
uint16_t* sharedImageBuffer = NULL;
volatile bool cameraFrameReady = false;

// コンボキャプチャ
static uint16_t comboCapturedBuf[Layout::PREVIEW_W * Layout::PREVIEW_H];
volatile bool comboCaptureReady = false;
volatile bool comboCaptureRequest = false;
ComboType lastComboType = COMBO_NONE;

//============================================================
// 履歴データ
//============================================================

struct HistEntry {
  int frame;
  int dir;
  uint16_t btn;
  ComboType color;
  int comboTotalFrame;
  bool dirty;
  
  void reset() {
    frame = 0; dir = -1; btn = 0;
    color = COMBO_NONE; comboTotalFrame = 0; dirty = false;
  }
};

static HistEntry history[Layout::HIST_ROWS];
static int histCount = 0;

// ボタン押下時間統計
static uint16_t btnStatsAvgDuration = 0;  // 平均値（0.1ms単位）
static uint8_t btnStatsSampleCount = 0;   // サンプル数
static bool btnStatsUpdated = false;      // 更新フラグ
static bool hasReceivedBtnStats = false;  // 一度でも受信したか

// FPS情報
static int hzMain = 0;           // MainCore ループ周波数（kHz）
static int hzSc1 = 0;            // SC1 ループ周波数（kHz）
static int hzSc3 = 0;            // SC3 ループ周波数（kHz）
static int fpsDrawCount = 0;     // Draw FPSカウント
static int fpsDrawPerSec = 0;    // Draw FPS表示用
static unsigned long lastFpsTime = 0;
static bool fpsUpdated = false;

// 反応速度テスト
static bool reactionMarkerVisible = false;  // マーカー表示中
static uint16_t reactionMarkerColor = 0;    // マーカー色
static uint16_t reactionActionMs = 0;       // Action時間（ms）
static uint16_t reactionReactMs = 0;        // React時間（ms）
static bool hasReactionAction = false;      // Action結果を受信したか
static bool hasReactionReact = false;       // React結果を受信したか
static bool reactionActionDetected = false; // Action検出状態（表示色制御用）
static bool reactionReactDetected = false;  // React検出状態（表示色制御用）

//============================================================
// 関数プロトタイプ
//============================================================

void drawInputRow(LGFX_Sprite* spr, int frame, int dir, uint16_t btn, 
                  ComboType comboType, int comboFrame, int maxX);
void initButtonPanel();
void updateButtonPanel(int dir, uint16_t btn);
void drawButtonStats();
void drawWaitingScreen();
void drawConnectedScreen(PadType padType);
void initActiveScreen();
void changeScreen(ScreenState newState);
void onPadStatusChanged(PadType newType);
void drawPadTypeLabel();
void drawFpsInfo();
void drawReactionMarker(bool show, uint16_t color);
void drawReactionResult();

void setup() {
  using namespace Layout;
  
  MP.begin();
  MP.RecvTimeout(MP_RECV_POLLING);
  
  // LCD初期化
  lcd.init();
  lcd.setRotation(2);  // 上下逆（180度回転）
  
  // 現在入力スプライト
  sprCurrent.setColorDepth(16);
  sprCurrent.createSprite(CURRENT_W, CURRENT_H);
  
  // 履歴行スプライト
  sprHistRow.setColorDepth(16);
  sprHistRow.createSprite(LEFT_AREA_W, ROW_H);
  
  // 状態初期化
  prevInput.reset();
  currentCombo.reset();
  lastDraw.reset();
  stats.reset();
  
  // 初期画面: 待機画面
  currentScreen = SCREEN_WAITING;
  currentPadType = PAD_DISCONNECTED;
  drawWaitingScreen();
}

void loop() {
  static bool firstLoop = true;
  if (firstLoop) {
    MPLog("SC2: loop() started with MSG_REACTION_START support\n");
    firstLoop = false;
  }
  
  unsigned long now = millis();
  int8_t msgid;
  uint32_t data;
  
  // メッセージ受信（複数回処理）
  for (int i = 0; i < 16; i++) {  // 最大16メッセージ/ループ
    int ret = MP.Recv(&msgid, &data, 0);
    if (ret <= 0) break;
    
    // 接続状態メッセージ
    if (msgid == MSG_PAD_STATUS) {
      PadType newType = (PadType)(data & 0xFF);
      MPLog("SC2 received PAD_STATUS: 0x%02X\n", newType);
      onPadStatusChanged(newType);
    }
    else if (msgid == MSG_SET_BUFFER) {
      sharedImageBuffer = (uint16_t*)data;
      MPLog("Buffer addr=0x%08X\n", (uint32_t)sharedImageBuffer);
    }
    else if (msgid == MSG_CAMERA_READY) {
      if (sharedImageBuffer != NULL) {
        stats.camRecvCount++;
        cameraFrameReady = true;
      }
    }
    else if (msgid == MSG_HISTORY) {
      // SubCore3からの履歴データを受信
      if (currentScreen == SCREEN_WAITING) {
        currentPadType = PAD_XINPUT;
        changeScreen(SCREEN_ACTIVE);
      }
      
      if (currentScreen == SCREEN_ACTIVE) {
        // bit31で新規追加か色更新かを判定
        if (data & HIST_FLAG_UPDATE) {
          // ■ 色更新（bit31 = 1）
          // bit 30-27: startIdx, bit 26-23: endIdx, bit 22-19: color, bit 18-12: comboFrame
          int startIdx = (data >> 27) & 0xF;
          int endIdx = (data >> 23) & 0xF;
          ComboType color = (ComboType)((data >> 19) & 0xF);
          int comboFrame = (data >> 12) & 0x7F;
          
          // 履歴[startIdx]〜[endIdx]の色を更新
          for (int i = startIdx; i <= endIdx && i < histCount; i++) {
            history[i].color = color;
            history[i].dirty = true;
          }
          // コンボフレーム数を設定（startIdx行に表示）
          if (startIdx < histCount) {
            history[startIdx].comboTotalFrame = comboFrame;
          }
          
          stats.comboRecvCount++;
          
          // コンボキャプチャ要求
          comboCaptureRequest = true;
          lastComboType = color;
          currentCombo.type = color;
          currentCombo.totalFrame = comboFrame;
          currentCombo.pending = true;  // 現在入力表示の更新トリガー
          
          MPLog("SC2 COLOR: start=%d, end=%d, color=%d, frame=%d, histCount=%d\n", 
                startIdx, endIdx, color, comboFrame, histCount);
        }
        else {
          // ■ 新規追加（bit31 = 0）
          // bit 30-21: frame(10bit,0.1F単位), bit 20-17: dir, bit 16-5: btn, bit 4-1: color
          int histFrame = (data >> 21) & 0x3FF;  // 0.1F単位
          int histDir = (data >> 17) & 0xF;
          uint16_t histBtn = (data >> 5) & 0xFFF;
          ComboType histColor = (ComboType)((data >> 1) & 0xF);
          
          totalInputChangeCount++;
          stats.inputChangeCount++;
          
          // 履歴に追加（0.1F単位で保存）
          addHistory(histFrame, histDir, histBtn, histColor);
        }
      }
    }
    else if (msgid == MSG_BUTTON_STATS) {
      // ボタン押下時間統計を受信
      // bit 31-16: avgDuration（0.1ms単位）, bit 15-8: sampleCount
      btnStatsAvgDuration = (data >> 16) & 0xFFFF;
      btnStatsSampleCount = (data >> 8) & 0xFF;
      btnStatsUpdated = true;
      hasReceivedBtnStats = true;
    }
    else if (msgid == MSG_FPS_SC1) {
      // SC1 ループ周波数を受信（kHz）
      hzSc1 = data & 0xFFFF;
      fpsUpdated = true;
    }
    else if (msgid == MSG_FPS_SC3) {
      // SC3 ループ周波数を受信（kHz）
      hzSc3 = data & 0xFFFF;
      fpsUpdated = true;
    }
    else if (msgid == MSG_FPS_MAIN) {
      // MainCore ループ周波数を受信（kHz）
      hzMain = data & 0xFFFF;
      fpsUpdated = true;
    }
    else if (msgid == MSG_REACTION_START) {
      // 反応速度テスト開始（マーカー表示）
      MPLog("SC2: Received MSG_REACTION_START, screen=%d\n", currentScreen);
      
      // 画面がWAITINGの場合はACTIVEに遷移
      if (currentScreen == SCREEN_WAITING) {
        currentPadType = PAD_XINPUT;
        changeScreen(SCREEN_ACTIVE);
        MPLog("SC2: Auto-transitioned to ACTIVE for reaction test\n");
      }
      
      if (currentScreen == SCREEN_ACTIVE) {
        reactionMarkerColor = data & 0xFFFF;
        reactionMarkerVisible = true;
        reactionActionDetected = false;
        reactionReactDetected = false;
        MPLog("SC2: Drawing marker, color=0x%04X\n", reactionMarkerColor);
        drawReactionMarker(true, reactionMarkerColor);
        // 描画完了を MainCore に通知
        MP.Send(MSG_REACTION_MARKER_DRAWN, (uint32_t)0);
        MPLog("SC2: Sent MSG_REACTION_MARKER_DRAWN\n");
      } else {
        MPLog("SC2: Marker NOT drawn, screen=%d (need %d)\n", currentScreen, SCREEN_ACTIVE);
      }
    }
    else if (msgid == MSG_REACTION_RESULT) {
      // 反応速度テスト結果
      if (currentScreen == SCREEN_ACTIVE) {
        uint32_t resultType = data & 0xFF;  // bit0-1: 00=timeout, 01=Action, 02=React
        uint16_t resultMs = (data >> 16) & 0xFFFF;
        
        if (resultType == 0) {
          // タイムアウト（マーカー消去）
          if (reactionMarkerVisible) {
            reactionMarkerVisible = false;
            reactionActionDetected = false;
            reactionReactDetected = false;
            drawReactionMarker(false, 0);
            drawReactionResult();
          }
        }
        else if (resultType == 1) {
          // Action検出
          reactionActionMs = resultMs;
          hasReactionAction = true;
          reactionActionDetected = true;
          drawReactionResult();
        }
        else if (resultType == 2) {
          // React検出（マーカー消去）
          reactionReactMs = resultMs;
          hasReactionReact = true;
          reactionReactDetected = true;
          if (reactionMarkerVisible) {
            reactionMarkerVisible = false;
            drawReactionMarker(false, 0);
          }
          drawReactionResult();
          // 少し待ってから色を白に戻す
          delay(100);
          reactionActionDetected = false;
          reactionReactDetected = false;
          drawReactionResult();
        }
      }
    }
    else if (msgid == MSG_GAMEPAD) {
      // 入力変化データを受信（履歴表示用）
      MPLog("SC2: Received MSG_GAMEPAD, data=0x%08X\n", data);
      
      if (currentScreen == SCREEN_WAITING) {
        currentPadType = PAD_XINPUT;
        MPLog("Input received while waiting -> ACTIVE\n");
        changeScreen(SCREEN_ACTIVE);
      }
      
      if (currentScreen == SCREEN_ACTIVE) {
        totalInputChangeCount++;
        stats.inputChangeCount++;
        
        // データ形式: dir(8bit) | btn(16bit) | reserved(8bit)
        int dir = data & 0xFF;
        uint16_t btn = (data >> 8) & 0xFFFF;
        
        MPLog("SC2: dir=%d, btn=0x%03X\n", dir, btn);
        
        // フレーム計算（前回入力からの経過時間、1F単位）
        // MainCoreから約16.67ms（1F）ごとに送信される
        int frameUnits = 1;  // 最小1F
        if (lastInputTime > 0) {
          unsigned long elapsed = now - lastInputTime;
          frameUnits = (elapsed + 8) / 17;  // 17ms ≈ 1F、四捨五入
          if (frameUnits < 1) frameUnits = 1;    // 最小1F
          if (frameUnits > 99) frameUnits = 99;  // 最大99F
        }
        lastInputTime = now;
        
        // 0.1F単位に変換（表示互換性のため）
        int frameDecims = frameUnits * 10;
        
        // 履歴に追加
        addHistory(frameDecims, dir, btn, COMBO_NONE);
        
        // 現在入力を保持
        prevInput.frame = frameDecims;
        prevInput.dir = dir;
        prevInput.btn = btn;
        
        // 現在入力描画
        if (lastDraw.needsUpdate(frameDecims, dir, btn) || currentCombo.pending) {
          drawCurrentInput(frameDecims, dir, btn);
        }
        lastDraw.update(frameDecims, dir, btn);
        currentCombo.pending = false;
      }
    }
    else if (msgid == MSG_DRAW) {
      // 入力データを受信（現在入力の表示用・定期送信）
      if (currentScreen == SCREEN_WAITING) {
        currentPadType = PAD_XINPUT;
        MPLog("Input received while waiting -> ACTIVE\n");
        changeScreen(SCREEN_ACTIVE);
      }
      
      if (currentScreen == SCREEN_ACTIVE) {
        stats.drawRecvCount++;
        
        // データ形式: frame(10bit,0.1F単位) | btn(12) | dir(4) | reserved(6)
        int recvFrame = (data >> 22) & 0x3FF;  // 0.1F単位
        uint16_t recvBtn = (data >> 10) & 0xFFF;
        int recvDir = (data >> 6) & 0xF;
        
        // 現在入力を保持（履歴追加はMSG_GAMEPADで行う）
        prevInput.frame = recvFrame;
        prevInput.dir = recvDir;
        prevInput.btn = recvBtn;
        
        // 現在入力描画
        if (lastDraw.needsUpdate(recvFrame, recvDir, recvBtn) || currentCombo.pending) {
          drawCurrentInput(recvFrame, recvDir, recvBtn);
          lastDraw.update(recvFrame, recvDir, recvBtn);
          currentCombo.pending = false;
        }
        
        // ボタンパネル更新
        updateButtonPanel(recvDir, recvBtn);
        
        // ボタン押下時間統計更新
        if (btnStatsUpdated) {
          drawButtonStats();
        }
        
        // FPS表示更新
        if (fpsUpdated) {
          drawFpsInfo();
        }
        
        // 履歴描画
        drawDirtyRows();
        
        // 描画FPSカウント
        fpsDrawCount++;
      }
    }
  }  // メッセージ受信ループ終了
  
  // 1秒ごとに描画FPS更新
  if (now - lastFpsTime >= 1000) {
    fpsDrawPerSec = fpsDrawCount;
    fpsDrawCount = 0;
    lastFpsTime = now;
    if (currentScreen == SCREEN_ACTIVE) {
      drawFpsInfo();
    }
  }
  
  // 接続情報画面から一定時間後にACTIVE画面へ遷移
  if (currentScreen == SCREEN_CONNECTED) {
    if (now - connectedTime >= CONNECTED_DISPLAY_MS) {
      changeScreen(SCREEN_ACTIVE);
    }
  }
  
  // カメラプレビュー描画
  if (currentScreen == SCREEN_ACTIVE) {
    if (cameraFrameReady && sharedImageBuffer != NULL) {
      drawCameraPreview();
      cameraFrameReady = false;
    }
  }
  
  // 5秒ごとに統計出力
  if (now - stats.startTime >= 5000) {
    MPLog("SC2: Cam=%d/s, Draw=%d/s, Combo=%d/s, InputChg=%d/s (total=%lu)\n", 
          stats.camRecvCount / 5, stats.drawRecvCount / 5, stats.comboRecvCount / 5,
          stats.inputChangeCount / 5, totalInputChangeCount);
    stats.reset();
  }
  
  delayMicroseconds(100);
}

// 履歴追加
void addHistory(int frame, int dir, uint16_t btn, ComboType color) {
  for (int i = Layout::HIST_ROWS - 1; i > 0; i--) {
    history[i] = history[i - 1];
    history[i].dirty = true;
  }
  history[0].frame = frame;
  history[0].dir = dir;
  history[0].btn = btn;
  history[0].color = color;
  history[0].comboTotalFrame = 0;
  history[0].dirty = true;
  
  if (histCount < Layout::HIST_ROWS) histCount++;
}

// コンボ状態に応じた色を取得
void getComboColors(ComboType comboType, uint16_t& bgColor, uint16_t& textColor) {
  switch (comboType) {
    case COMBO_HADOU:
      bgColor = Colors::BG_COMBO;
      textColor = Colors::COMBO;
      break;
    case COMBO_SA:
      bgColor = Colors::BG_SA;
      textColor = Colors::SA;
      break;
    case COMBO_SHORYU:
      bgColor = Colors::BG_SHORYU;
      textColor = Colors::SHORYU;
      break;
    default:
      bgColor = Colors::BG;
      textColor = Colors::FRAME;
      break;
  }
}

// 現在入力描画
// ※現在入力行は履歴とは別なので、常に通常色を使用
// frameは0.1F単位（内部）、表示は整数
void drawCurrentInput(int frame, int dir, uint16_t btn) {
  using namespace Layout;
  
  // 現在入力行は常に通常色（黒背景、白文字）
  sprCurrent.fillScreen(Colors::BG);
  
  // フレーム数（整数表示、0.1F単位を整数に変換）
  int frameInt = (frame + 5) / 10;  // 四捨五入
  if (frameInt < 1) frameInt = 1;
  if (frameInt > 99) frameInt = 99;
  
  sprCurrent.setTextSize(1);
  sprCurrent.setTextColor(Colors::FRAME);
  sprCurrent.setCursor(FRAME_X, TEXT_Y);
  if (frameInt < 10) sprCurrent.print(" ");
  sprCurrent.print(frameInt);
  
  // 矢印
  drawArrowSpr(&sprCurrent, ARROW_X, ARROW_Y, ARROW_SZ, dir, Colors::DIR);
  
  // ボタン
  drawButtonCircle(&sprCurrent, BTN_X, BTN_Y, btn);
  
  // 現在入力行にはコンボフレーム数を表示しない
  // （履歴行で表示される）
  
  sprCurrent.pushSprite(0, CURRENT_Y);
}

// グラフィック矢印描画
void drawArrowSpr(LGFX_Sprite* spr, int x, int y, int sz, int dir, uint16_t col) {
  const uint8_t* bitmap = getArrowBitmap(dir);
  
  for (int row = 0; row < ARROW_H; row++) {
    uint8_t b0 = pgm_read_byte(&bitmap[row * 2]);
    uint8_t b1 = pgm_read_byte(&bitmap[row * 2 + 1]);
    uint16_t bits = (b0 << 8) | b1;
    
    for (int col_idx = 0; col_idx < ARROW_W; col_idx++) {
      if (bits & (0x8000 >> col_idx)) {
        spr->drawPixel(x + col_idx, y + row, col);
      }
    }
  }
}

// ボタンアイコン描画（1つ）
void drawButtonIconSingle(LGFX_Sprite* spr, int x, int y, IconType iconType, uint16_t color) {
  const uint8_t* bitmap;
  
  switch (iconType) {
    case ICON_TYPE_PUNCH:
      bitmap = ICON_PUNCH;
      break;
    case ICON_TYPE_KICK:
      bitmap = ICON_KICK;
      break;
    default:
      // その他は小さい円で表示
      spr->fillCircle(x + ICON_W/2, y + ICON_H/2, 5, color);
      return;
  }
  
  for (int row = 0; row < ICON_H; row++) {
    uint8_t b0 = pgm_read_byte(&bitmap[row * 2]);
    uint8_t b1 = pgm_read_byte(&bitmap[row * 2 + 1]);
    uint16_t bits = (b0 << 8) | b1;
    
    for (int col = 0; col < ICON_W; col++) {
      if (bits & (0x8000 >> col)) {
        spr->drawPixel(x + col, y + row, color);
      }
    }
  }
}

// ボタン描画（全ボタン）
int drawButtonCircle(LGFX_Sprite* spr, int x, int y, uint16_t btn) {
  int cx = x;
  int cy = y + 2;  // アイコン用に調整
  
  for (int i = 0; i < BUTTON_COUNT; i++) {
    if (btn & BUTTONS[i].mask) {
      drawButtonIconSingle(spr, cx, cy, BUTTONS[i].iconType, BUTTONS[i].color);
      cx += BUTTON_SPACING;
    }
  }
  
  return cx;
}

// 履歴描画
void drawDirtyRows() {
  int drawn = 0;
  for (int i = 0; i < histCount && i < Layout::HIST_ROWS; i++) {
    if (history[i].dirty) {
      drawHistoryRow(i);
      history[i].dirty = false;
      drawn++;
      if (drawn >= Layout::MAX_DRAW_PER_FRAME) return;
    }
  }
}

// 履歴1行描画（frameは0.1F単位、表示は整数）
void drawHistoryRow(int idx) {
  using namespace Layout;
  
  int y = HIST_Y + idx * ROW_H;
  
  uint16_t bgColor, textColor;
  getComboColors(history[idx].color, bgColor, textColor);
  
  sprHistRow.fillScreen(bgColor);
  
  // フレーム数（整数表示、0.1F単位を整数に変換、四捨五入）
  int frameInt = (history[idx].frame + 5) / 10;
  if (frameInt < 1) frameInt = 1;
  if (frameInt > 99) frameInt = 99;
  
  sprHistRow.setTextSize(1);
  sprHistRow.setTextColor(textColor);
  sprHistRow.setCursor(FRAME_X, TEXT_Y);
  if (frameInt < 10) sprHistRow.print(" ");
  sprHistRow.print(frameInt);
  
  drawArrowSpr(&sprHistRow, ARROW_X, ARROW_Y, ARROW_SZ, history[idx].dir, textColor);
  
  int btnEndX = drawButtonCircle(&sprHistRow, BTN_X, BTN_Y, history[idx].btn);
  
  // 技判定フレームは整数で表示
  if (history[idx].comboTotalFrame > 0 && btnEndX < 110) {
    uint16_t frameColor;
    switch (history[idx].color) {
      case COMBO_SA:     frameColor = Colors::SA; break;
      case COMBO_SHORYU: frameColor = Colors::SHORYU; break;
      default:           frameColor = Colors::COMBO; break;
    }
    sprHistRow.setTextSize(1);
    sprHistRow.setTextColor(frameColor);
    sprHistRow.setCursor(btnEndX + 2, TEXT_Y);
    sprHistRow.print(history[idx].comboTotalFrame);
    sprHistRow.print("F");
  }
  
  sprHistRow.pushSprite(0, y);
}

// プレビュー用バッファ
static uint16_t previewBuf[Layout::PREVIEW_W * Layout::PREVIEW_H];

// カメラプレビュー描画
void drawCameraPreview() {
  using namespace Layout;
  using namespace Camera;
  
  uint16_t* dst = previewBuf;
  uint16_t* srcRow = sharedImageBuffer;
  
  for (int py = 0; py < PREVIEW_H; py++) {
    uint16_t* src = srcRow;
    for (int px = 0; px < PREVIEW_W; px++) {
      *dst++ = *src;
      src += SAMPLE_RATIO;
    }
    srcRow += IMG_WIDTH * SAMPLE_RATIO;
  }
  
  // コンボキャプチャ要求
  if (comboCaptureRequest) {
    memcpy(comboCapturedBuf, previewBuf, sizeof(previewBuf));
    comboCaptureReady = true;
    comboCaptureRequest = false;
    drawComboCapturedImage();
  }
  
  lcd.setSwapBytes(true);
  lcd.pushImage(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, previewBuf);
  lcd.setSwapBytes(false);
}

// コンボキャプチャ画像描画
void drawComboCapturedImage() {
  using namespace Layout;
  
  if (!comboCaptureReady) return;
  
  uint16_t labelColor;
  const char* labelText;
  switch (lastComboType) {
    case COMBO_SA:
      labelColor = Colors::SA;
      labelText = "SA!";
      break;
    case COMBO_SHORYU:
      labelColor = Colors::SHORYU;
      labelText = "SHORYU!";
      break;
    default:
      labelColor = Colors::COMBO;
      labelText = "HADOU!";
      break;
  }
  
  lcd.fillRect(RIGHT_AREA_X, COMBO_LABEL_Y, 120, 15, Colors::BG);
  
  lcd.setTextSize(1);
  lcd.setTextColor(labelColor);
  lcd.setCursor(COMBO_CAP_X, COMBO_LABEL_Y);
  lcd.print(labelText);
  
  lcd.setSwapBytes(true);
  lcd.pushImage(COMBO_CAP_X, COMBO_CAP_Y, PREVIEW_W, PREVIEW_H, comboCapturedBuf);
  lcd.setSwapBytes(false);
  
  lcd.drawRect(COMBO_CAP_X - 1, COMBO_CAP_Y - 1, PREVIEW_W + 2, PREVIEW_H + 2, labelColor);
}

//============================================================
// ボタンパネル描画
//============================================================

static int lastPanelDir = -1;
static uint16_t lastPanelBtn = 0xFFFF;

void drawThickCircle(int cx, int cy, int r, uint16_t color) {
  lcd.drawCircle(cx, cy, r, color);
  lcd.drawCircle(cx, cy, r - 1, color);
  lcd.drawCircle(cx, cy, r - 2, color);
}

void initButtonPanel() {
  using namespace Layout;
  
  uint16_t outlineColor = 0x4208;
  
  drawThickCircle(DPAD_CX, DPAD_CY - PANEL_SP, PANEL_R, outlineColor);
  drawThickCircle(DPAD_CX, DPAD_CY + PANEL_SP, PANEL_R, outlineColor);
  drawThickCircle(DPAD_CX - PANEL_SP, DPAD_CY, PANEL_R, outlineColor);
  drawThickCircle(DPAD_CX + PANEL_SP, DPAD_CY, PANEL_R, outlineColor);
  
  drawThickCircle(FACE_CX, FACE_CY - PANEL_SP, PANEL_R, outlineColor);
  drawThickCircle(FACE_CX, FACE_CY + PANEL_SP, PANEL_R, outlineColor);
  drawThickCircle(FACE_CX - PANEL_SP, FACE_CY, PANEL_R, outlineColor);
  drawThickCircle(FACE_CX + PANEL_SP, FACE_CY, PANEL_R, outlineColor);
  
  drawThickCircle(TRIG_L_X, TRIG_Y, PANEL_R - 2, outlineColor);
  drawThickCircle(TRIG_L_X + 18, TRIG_Y, PANEL_R - 2, outlineColor);
  drawThickCircle(TRIG_R_X, TRIG_Y, PANEL_R - 2, outlineColor);
  drawThickCircle(TRIG_R_X + 18, TRIG_Y, PANEL_R - 2, outlineColor);
}

void drawPanelButton(int cx, int cy, int r, bool pressed, uint16_t color) {
  if (pressed) {
    lcd.fillCircle(cx, cy, r, color);
  } else {
    lcd.fillCircle(cx, cy, r, Colors::BG);
    drawThickCircle(cx, cy, r, 0x4208);
  }
}

void updateButtonPanel(int dir, uint16_t btn) {
  using namespace Layout;
  
  if (dir == lastPanelDir && btn == lastPanelBtn) return;
  
  uint16_t white = Colors::FRAME;
  
  bool up    = (dir == DIR_UP || dir == DIR_UP_L || dir == DIR_UP_R);
  bool down  = (dir == DIR_DOWN || dir == DIR_DOWN_L || dir == DIR_DOWN_R);
  bool left  = (dir == DIR_LEFT || dir == DIR_UP_L || dir == DIR_DOWN_L);
  bool right = (dir == DIR_RIGHT || dir == DIR_UP_R || dir == DIR_DOWN_R);
  
  bool prevUp    = (lastPanelDir == DIR_UP || lastPanelDir == DIR_UP_L || lastPanelDir == DIR_UP_R);
  bool prevDown  = (lastPanelDir == DIR_DOWN || lastPanelDir == DIR_DOWN_L || lastPanelDir == DIR_DOWN_R);
  bool prevLeft  = (lastPanelDir == DIR_LEFT || lastPanelDir == DIR_UP_L || lastPanelDir == DIR_DOWN_L);
  bool prevRight = (lastPanelDir == DIR_RIGHT || lastPanelDir == DIR_UP_R || lastPanelDir == DIR_DOWN_R);
  
  if (up != prevUp)    drawPanelButton(DPAD_CX, DPAD_CY - PANEL_SP, PANEL_R, up, white);
  if (down != prevDown)  drawPanelButton(DPAD_CX, DPAD_CY + PANEL_SP, PANEL_R, down, white);
  if (left != prevLeft)  drawPanelButton(DPAD_CX - PANEL_SP, DPAD_CY, PANEL_R, left, white);
  if (right != prevRight) drawPanelButton(DPAD_CX + PANEL_SP, DPAD_CY, PANEL_R, right, white);
  
  if ((btn & 0x400) != (lastPanelBtn & 0x400)) {
    drawPanelButton(FACE_CX, FACE_CY - PANEL_SP, PANEL_R, btn & 0x400, 0xF81F);
  }
  if ((btn & 0x200) != (lastPanelBtn & 0x200)) {
    drawPanelButton(FACE_CX, FACE_CY + PANEL_SP, PANEL_R, btn & 0x200, 0xF800);
  }
  if ((btn & 0x100) != (lastPanelBtn & 0x100)) {
    drawPanelButton(FACE_CX - PANEL_SP, FACE_CY, PANEL_R, btn & 0x100, 0x249F);
  }
  if ((btn & 0x800) != (lastPanelBtn & 0x800)) {
    drawPanelButton(FACE_CX + PANEL_SP, FACE_CY, PANEL_R, btn & 0x800, 0x07E0);
  }
  
  int tr = PANEL_R - 2;
  if ((btn & 0x010) != (lastPanelBtn & 0x010)) {
    drawPanelButton(TRIG_L_X, TRIG_Y, tr, btn & 0x010, 0x07FF);
  }
  if ((btn & 0x004) != (lastPanelBtn & 0x004)) {
    drawPanelButton(TRIG_L_X + 18, TRIG_Y, tr, btn & 0x004, 0xFFFF);
  }
  if ((btn & 0x020) != (lastPanelBtn & 0x020)) {
    drawPanelButton(TRIG_R_X, TRIG_Y, tr, btn & 0x020, 0xFD20);
  }
  if ((btn & 0x008) != (lastPanelBtn & 0x008)) {
    drawPanelButton(TRIG_R_X + 18, TRIG_Y, tr, btn & 0x008, 0xFFFF);
  }
  
  lastPanelDir = dir;
  lastPanelBtn = btn;
}

//============================================================
// ボタン押下時間統計表示
//============================================================

void drawButtonStats() {
  using namespace Layout;
  
  // 背景クリア
  lcd.fillRect(BTN_STATS_X, BTN_STATS_Y, 110, 10, Colors::BG);
  
  lcd.setTextColor(Colors::FRAME, Colors::BG);
  lcd.setFont(&fonts::Font0);  // 小さいフォント
  lcd.setCursor(BTN_STATS_X, BTN_STATS_Y);
  
  if (!hasReceivedBtnStats) {
    lcd.print("BtnAvg: --.- ms");
  } else {
    // avgDurationは0.1ms単位なので、小数点表示（最後の値を保持）
    int msInt = btnStatsAvgDuration / 10;
    int msDec = btnStatsAvgDuration % 10;
    lcd.printf("BtnAvg: %d.%d ms", msInt, msDec);
  }
  
  btnStatsUpdated = false;
}

//============================================================
// PadType表示（右上）
//============================================================

void drawPadTypeLabel() {
  using namespace Layout;
  
  // 背景クリア
  lcd.fillRect(PAD_TYPE_X, PAD_TYPE_Y, 110, 12, Colors::BG);
  
  if (currentPadType == PAD_DISCONNECTED) return;
  
  lcd.setFont(&fonts::Font0);  // 小さいフォント
  
  if (currentPadType == PAD_HID) {
    lcd.setTextColor(TFT_YELLOW, Colors::BG);
    lcd.setCursor(PAD_TYPE_X, PAD_TYPE_Y + 2);
    lcd.print("HID");
  } else if (currentPadType == PAD_XINPUT) {
    lcd.setTextColor(TFT_WHITE, Colors::BG);  // 白色に変更
    lcd.setCursor(PAD_TYPE_X, PAD_TYPE_Y + 2);
    lcd.print("X-Input");
  }
}

//============================================================
// FPS情報表示（右上、PadTypeの下）
//============================================================

void drawFpsInfo() {
  using namespace Layout;
  
  // 背景クリア
  lcd.fillRect(FPS_X, FPS_Y, 110, 14, Colors::BG);
  
  lcd.setFont(&fonts::Font0);  // 小さいフォント
  lcd.setTextColor(TFT_WHITE, Colors::BG);  // 白色
  
  // Main:kHz SC1:kHz SC3:kHz D:fps
  lcd.setCursor(FPS_X, FPS_Y + 2);
  lcd.printf("%dk %dk %dk %df", hzMain, hzSc1, hzSc3, fpsDrawPerSec);
  
  fpsUpdated = false;
}

//============================================================
// 反応速度テスト表示
//============================================================

void drawReactionMarker(bool show, uint16_t color) {
  using namespace Layout;
  
  if (show) {
    // マーカー表示（塗りつぶし円）
    lcd.fillCircle(REACTION_MARKER_X, REACTION_MARKER_Y, REACTION_MARKER_R, color);
  } else {
    // マーカー消去（背景色で塗りつぶし）
    lcd.fillCircle(REACTION_MARKER_X, REACTION_MARKER_Y, REACTION_MARKER_R, Colors::BG);
  }
}

void drawReactionResult() {
  using namespace Layout;
  
  // 2行描画: Action, React
  // Action行: Y = REACTION_ACTION_Y (300)
  // React行: Y = REACTION_REACT_Y (312)
  
  // 背景クリア（2行分、マーカーと重ならない幅）
  lcd.fillRect(REACTION_STATS_X, REACTION_ACTION_Y, 115, 24, Colors::BG);
  
  lcd.setFont(&fonts::Font0);  // 小さいフォント
  
  // === Action行 ===
  lcd.setCursor(REACTION_STATS_X, REACTION_ACTION_Y);
  lcd.setTextColor(hasReactionAction && reactionActionDetected ? TFT_GREEN : TFT_WHITE, Colors::BG);
  
  lcd.print("Action:");
  if (!hasReactionAction) {
    lcd.print("    - ms");  // スペース4つ + "- ms"
  } else {
    // 整数部と小数部に分ける
    int intPart = reactionActionMs / 10;
    int decPart = reactionActionMs % 10;
    
    // 4桁固定（右揃え）+ " ms"
    char buf[12];
    sprintf(buf, "%4d.%d ms", intPart, decPart);
    lcd.print(buf);
  }
  
  // === React行 ===
  lcd.setCursor(REACTION_STATS_X, REACTION_REACT_Y);
  lcd.setTextColor(hasReactionReact && reactionReactDetected ? TFT_GREEN : TFT_WHITE, Colors::BG);
  
  lcd.print("React:");
  if (!hasReactionReact) {
    lcd.print("     - ms");  // スペース5つ + "- ms"（"Action"より1文字短いので+1スペース）
  } else {
    // 整数部と小数部に分ける
    int intPart = reactionReactMs / 10;
    int decPart = reactionReactMs % 10;
    
    // 4桁固定（右揃え）+ " ms"（前に1スペース追加してActionと揃える）
    char buf[12];
    sprintf(buf, " %4d.%d ms", intPart, decPart);
    lcd.print(buf);
  }
}

//============================================================
// 画面状態管理
//============================================================

const char* getPadTypeName(PadType type) {
  switch (type) {
    case PAD_HID:    return "HID Controller";
    case PAD_XINPUT: return "Xinput Controller";
    default:         return "Unknown";
  }
}

void drawWaitingScreen() {
  lcd.fillScreen(Colors::BG);
  
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(2);
  lcd.setCursor(54, 120);
  lcd.print("Key Display");
  
  lcd.setTextColor(TFT_YELLOW);
  lcd.setTextSize(1);
  lcd.setCursor(45, 160);
  lcd.print("Waiting for controller...");
  
  lcd.setTextColor(TFT_DARKGREY);
  lcd.setCursor(50, 300);
  lcd.print("Status: Disconnected");
}

void drawConnectedScreen(PadType padType) {
  lcd.fillScreen(Colors::BG);
  
  lcd.setTextColor(TFT_GREEN);
  lcd.setTextSize(2);
  lcd.setCursor(40, 80);
  lcd.print("Connected!");
  
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(1);
  lcd.setCursor(30, 130);
  lcd.print("Controller Type:");
  
  lcd.setTextColor(TFT_CYAN);
  lcd.setTextSize(2);
  lcd.setCursor(30, 150);
  if (padType == PAD_HID) {
    lcd.print("HID");
  } else if (padType == PAD_XINPUT) {
    lcd.print("Xinput");
  }
  
  lcd.setTextColor(TFT_DARKGREY);
  lcd.setTextSize(1);
  lcd.setCursor(30, 180);
  lcd.printf("Code: P%02X", padType);
  
  lcd.setTextColor(TFT_YELLOW);
  lcd.setCursor(30, 220);
  lcd.print("Starting in a moment...");
  
  lcd.setTextColor(TFT_GREEN);
  lcd.setCursor(20, 300);
  lcd.print("Status: Connected");
}

void initActiveScreen() {
  using namespace Layout;
  
  lcd.fillScreen(Colors::BG);
  
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(1);
  lcd.setCursor(4, 10);
  lcd.print("Key Display v7");
  
  lcd.drawFastHLine(0, 28, LEFT_AREA_W, TFT_DARKGREY);
  lcd.drawFastHLine(0, 50, LEFT_AREA_W, TFT_DARKGREY);
  lcd.drawFastHLine(0, STATS_AREA_Y, LEFT_AREA_W, TFT_DARKGREY);  // 統計エリア区切り
  
  lcd.drawFastVLine(LEFT_AREA_W, 0, 320, TFT_DARKGREY);
  
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_CYAN);
  lcd.setCursor(PREVIEW_X, PREVIEW_LABEL_Y);
  lcd.print("LIVE");
  
  lcd.drawRect(PREVIEW_X - 1, PREVIEW_Y - 1, PREVIEW_W + 2, PREVIEW_H + 2, TFT_DARKGREY);
  
  lcd.setTextColor(TFT_DARKGREY);
  lcd.setCursor(COMBO_CAP_X, COMBO_LABEL_Y);
  lcd.print("(combo)");
  
  lcd.drawRect(COMBO_CAP_X - 1, COMBO_CAP_Y - 1, PREVIEW_W + 2, PREVIEW_H + 2, TFT_DARKGREY);
  
  for (int i = 0; i < HIST_ROWS; i++) {
    history[i].reset();
  }
  histCount = 0;
  
  prevInput.reset();
  currentCombo.reset();
  lastDraw.reset();
  comboCaptureReady = false;
  comboCaptureRequest = false;
  lastComboType = COMBO_NONE;
  
  initButtonPanel();
  
  sprCurrent.fillScreen(Colors::BG);
  sprCurrent.pushSprite(0, CURRENT_Y);
  
  // 右上にPadType表示
  drawPadTypeLabel();
  
  // FPS情報表示
  drawFpsInfo();
  
  // 統計表示（左下エリア）
  drawReactionResult();
  drawButtonStats();
}

void changeScreen(ScreenState newState) {
  if (currentScreen == newState) return;
  
  ScreenState oldState = currentScreen;
  currentScreen = newState;
  
  MPLog("Screen: %d -> %d\n", oldState, newState);
  
  switch (newState) {
    case SCREEN_WAITING:
      drawWaitingScreen();
      break;
    case SCREEN_CONNECTED:
      drawConnectedScreen(currentPadType);
      connectedTime = millis();
      break;
    case SCREEN_ACTIVE:
      initActiveScreen();
      break;
  }
}

void onPadStatusChanged(PadType newType) {
  PadType oldType = currentPadType;
  currentPadType = newType;
  
  MPLog("PadType: 0x%02X -> 0x%02X\n", oldType, newType);
  
  if (newType == PAD_DISCONNECTED) {
    changeScreen(SCREEN_WAITING);
  } else {
    changeScreen(SCREEN_CONNECTED);
  }
}
