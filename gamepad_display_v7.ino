/**
 * ========================================
 *   gamepad_display_v7 - MainCore
 * ========================================
 * 
 * ゲームパッド入力表示システム
 * 
 * コア構成:
 * - MainCore: カメラストリーミング + 入力管理 + メッセージルーティング
 * - SubCore1: ゲームパッドUART受信
 * - SubCore2: LCD描画 + 履歴管理
 * - SubCore3: コマンド解析（波動拳、SA判定）
 * 
 * v7: v5ベース（v6の問題を回避し、安定版から再スタート）
 */

#include <MP.h>
#include <Camera.h>

// 画像設定（QQVGA、30fps）
#define IMG_WIDTH   160
#define IMG_HEIGHT  120
#define IMG_SIZE    (IMG_WIDTH * IMG_HEIGHT * 2)  // RGB565 = 38,400 bytes

// メッセージID
#define MSG_GAMEPAD      1
#define MSG_DRAW         2
#define MSG_SET_BUFFER   3   // 共有メモリアドレス送信
#define MSG_CAMERA_READY 4   // カメラフレーム準備完了
#define MSG_PAD_STATUS   5   // 接続状態（SubCore1→MainCore→SubCore2）
#define MSG_HISTORY      7   // 履歴データ（SubCore3→MainCore→SubCore2）
                             // ※ 新規追加(bit31=0) と 色更新(bit31=1) の両方を含む
#define MSG_BUTTON_STATS 8   // ボタン押下時間統計（SubCore3→MainCore→SubCore2）
#define MSG_FPS_SC1      9   // SC1 FPS情報
#define MSG_FPS_SC3     10   // SC3 FPS情報
#define MSG_REACTION_START  11  // 反応速度テスト開始（MainCore→SubCore2,SubCore3）
#define MSG_REACTION_RESULT 12  // 反応速度テスト結果（SubCore3→MainCore→SubCore2）
#define MSG_REACTION_MARKER_DRAWN 14  // マーカー描画完了（SubCore2→MainCore）
#define MSG_FPS_MAIN        15  // MainCore Hz情報（MainCore→SubCore2）

// 差分検出設定
#define DETECT_SAMPLE_STEP  5  // サンプリング間隔（5ピクセルごと）
#define DETECT_SAMPLE_W     (IMG_WIDTH / DETECT_SAMPLE_STEP)   // 32
#define DETECT_SAMPLE_H     (IMG_HEIGHT / DETECT_SAMPLE_STEP)  // 24
#define DETECT_SAMPLE_COUNT (DETECT_SAMPLE_W * DETECT_SAMPLE_H)  // 768
#define PIXEL_DIFF_THRESHOLD 30  // 1ピクセルあたりの色差分閾値
#define MOTION_PIXEL_COUNT   15  // 変化したピクセル数の閾値

// タイミング
#define SEND_INTERVAL_MS  33  // 約30fps（MSG_DRAW用）
#define FRAME_INTERVAL_MS 17  // 1フレーム（約60fps、履歴送信用）

// 共有メモリ（カメラ画像バッファ）
uint16_t* sharedImageBuffer = NULL;

// カメラ状態
volatile bool imageReady = false;
bool cameraEnabled = true;
int captureFps = 0;
int captureCount = 0;

// 入力状態
uint8_t currentDir = 0;
uint16_t currentBtn = 0;
int currentFrame = 0;
bool hasInput = false;
bool inputChanged = false;

unsigned long inputStartTime = 0;
unsigned long lastSendTime = 0;
unsigned long lastFrameSendTime = 0;  // SubCore2へのフレーム単位送信タイマー

// フレーム単位送信用の状態保持
uint8_t frameDir = 0;      // フレーム内の最新方向
uint16_t frameBtn = 0;     // フレーム内の最新ボタン
bool frameHasInput = false; // フレーム内に入力があったか

// 統計
unsigned long statsStartTime = 0;
unsigned long loopCount = 0;
unsigned long sc1RecvCount = 0;
unsigned long sc2SendCount = 0;
unsigned long sc3RecvCount = 0;
unsigned long inputChangeCount = 0;  // 入力変化回数

// 反応速度テスト
unsigned long reactionNextTime = 0;        // 次のマーカー表示時刻
unsigned long reactionMarkerDrawnTime = 0; // マーカー描画完了時刻（millis）
bool reactionMarkerActive = false;         // マーカー表示中フラグ
bool reactionWaitingForDraw = false;       // 描画完了待ち
bool reactionActionDetected = false;       // Action検出済みフラグ
uint16_t reactionActionMs = 0;             // Action時間（ms）
uint16_t reactionReactMs = 0;              // React時間（ms）
const unsigned long REACTION_MARKER_DURATION = 2000;  // マーカー表示時間（2秒）

// 差分検出用バッファ
uint16_t prevSampleBuf[DETECT_SAMPLE_COUNT];  // 前フレームのサンプル
bool hasPrevFrame = false;                     // 前フレームがあるか
unsigned long motionDetectCount = 0;          // 動き検出回数

/**
 * RGB565色差分を計算
 * @param c1 色1（RGB565）
 * @param c2 色2（RGB565）
 * @return 色差分（0-255）
 */
inline int colorDiff(uint16_t c1, uint16_t c2) {
  int r1 = (c1 >> 11) & 0x1F;
  int g1 = (c1 >> 5) & 0x3F;
  int b1 = c1 & 0x1F;
  
  int r2 = (c2 >> 11) & 0x1F;
  int g2 = (c2 >> 5) & 0x3F;
  int b2 = c2 & 0x1F;
  
  int dr = abs(r1 - r2);
  int dg = abs(g1 - g2);
  int db = abs(b1 - b2);
  
  // 重み付き差分（G成分を2倍）
  return (dr * 8) + (dg * 4) + (db * 8);
}

/**
 * カメラ画像の動き検出（ピクセル単位）
 * @return 変化したピクセル数
 */
uint32_t detectMotion() {
  if (sharedImageBuffer == NULL) return 0;
  
  uint32_t changedPixels = 0;
  int sampleIdx = 0;
  
  // サンプリングして差分をチェック
  for (int y = 0; y < DETECT_SAMPLE_H; y++) {
    int srcY = y * DETECT_SAMPLE_STEP;
    for (int x = 0; x < DETECT_SAMPLE_W; x++) {
      int srcX = x * DETECT_SAMPLE_STEP;
      int srcIdx = srcY * IMG_WIDTH + srcX;
      
      uint16_t curr = sharedImageBuffer[srcIdx];
      
      if (hasPrevFrame) {
        int diff = colorDiff(curr, prevSampleBuf[sampleIdx]);
        
        // このピクセルの差分が閾値を超えていたらカウント
        if (diff > PIXEL_DIFF_THRESHOLD) {
          changedPixels++;
        }
      }
      
      prevSampleBuf[sampleIdx] = curr;
      sampleIdx++;
    }
  }
  
  hasPrevFrame = true;
  return changedPixels;
}

// カメラコールバック（フレーム準備完了時に呼ばれる）
void cameraCallback(CamImage img) {
  if (!img.isAvailable()) return;
  if (sharedImageBuffer == NULL) return;
  
  // YUV422 → RGB565 変換
  img.convertPixFormat(CAM_IMAGE_PIX_FMT_RGB565);
  
  // 共有メモリにコピー
  memcpy(sharedImageBuffer, img.getImgBuff(), IMG_SIZE);
  
  // 差分検出（反応速度テスト中のみ）
  if (reactionMarkerActive && !reactionActionDetected && !reactionWaitingForDraw) {
    uint32_t changedPixels = detectMotion();
    
    if (changedPixels > MOTION_PIXEL_COUNT) {
      motionDetectCount++;
      
      // Action時間を計算（マーカー描画完了から動き検出まで）
      unsigned long now = millis();
      reactionActionMs = (uint16_t)(now - reactionMarkerDrawnTime);
      reactionActionDetected = true;
      
      // SubCore2に送信（Action時間）
      uint32_t actionData = ((uint32_t)reactionActionMs << 16) | 0x01;  // bit0-1: 01=Action
      MP.Send(MSG_REACTION_RESULT, actionData, 2);
      
      Serial.print("[Main] Action detected: ");
      Serial.print(changedPixels);
      Serial.print(" pixels, ");
      Serial.print(reactionActionMs);
      Serial.println(" ms");
    }
  }
  
  // SubCore2にフレーム準備完了を通知
  MP.Send(MSG_CAMERA_READY, (uint32_t)0, 2);
  
  imageReady = true;
  captureCount++;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("================================");
  Serial.println("  ゲームパッド + カメラ v7");
  Serial.println("================================");
  
  pinMode(LED0, OUTPUT);
  pinMode(LED1, OUTPUT);
  digitalWrite(LED0, LOW);   // コントローラ未接続
  digitalWrite(LED1, LOW);   // マーカー非表示
  
  // USB電源管理ピン初期化（起動時はHIGH）
  pinMode(PIN_D02, OUTPUT);
  digitalWrite(PIN_D02, HIGH);
  Serial.println("USB電源: HIGH (起動中)");
  
  // 共有メモリ確保
  Serial.print("共有メモリ確保...");
  sharedImageBuffer = (uint16_t*)MP.AllocSharedMemory(IMG_SIZE);
  if (sharedImageBuffer == NULL) {
    Serial.println("NG");
  } else {
    Serial.print("OK (");
    Serial.print(IMG_SIZE);
    Serial.print(" bytes @ 0x");
    Serial.print((uint32_t)sharedImageBuffer, HEX);
    Serial.println(")");
  }
  
  // カメラ初期化
  Serial.print("カメラ初期化...");
  CamErr err = theCamera.begin(
    1,                              // バッファ数
    CAM_VIDEO_FPS_30,              // 30fps
    IMG_WIDTH,                      // 160
    IMG_HEIGHT,                     // 120
    CAM_IMAGE_PIX_FMT_YUV422        // YUV422（後でRGB565に変換）
  );
  if (err != CAM_ERR_SUCCESS) {
    Serial.println("NG - カメラなしで続行");
    cameraEnabled = false;
  } else {
    Serial.println("OK (QQVGA, 30fps)");
    theCamera.setAutoWhiteBalance(true);
    theCamera.setAutoExposure(true);
    theCamera.setHDR(CAM_HDR_MODE_OFF);  // HDR OFF
  }
  
  MP.RecvTimeout(MP_RECV_POLLING);
  
  // SubCore2起動
  Serial.print("SC2起動...");
  int ret2 = MP.begin(2);
  if (ret2 < 0) {
    Serial.print("NG(");
    Serial.print(ret2);
    Serial.println(")");
  } else {
    Serial.println("OK");
  }
  delay(500);
  
  // 共有メモリアドレスをSubCore2に送信
  if (sharedImageBuffer != NULL) {
    MP.Send(MSG_SET_BUFFER, sharedImageBuffer, 2);
    Serial.print("バッファアドレス送信: 0x");
    Serial.println((uint32_t)sharedImageBuffer, HEX);
  }
  
  // SubCore3起動（コマンド解析）
  Serial.print("SC3起動...");
  int ret3 = MP.begin(3);
  Serial.println(ret3 >= 0 ? "OK" : "NG");
  delay(500);
  
  // SubCore1起動
  Serial.print("SC1起動...");
  int ret1 = MP.begin(1);
  Serial.println(ret1 >= 0 ? "OK" : "NG");
  delay(500);
  
  // カメラストリーミング開始
  if (cameraEnabled && sharedImageBuffer != NULL) {
    Serial.print("ストリーミング開始...");
    err = theCamera.startStreaming(true, cameraCallback);
    Serial.println(err == CAM_ERR_SUCCESS ? "OK" : "NG");
  }
  
  inputStartTime = millis();
  lastSendTime = millis();
  lastFrameSendTime = millis();  // フレーム単位送信タイマー初期化
  statsStartTime = millis();
  reactionNextTime = millis() + 10000;  // 起動10秒後に最初のマーカー表示
  
  // すべてのコア起動完了 → USB電源をLOWに
  digitalWrite(PIN_D02, LOW);
  Serial.println("USB電源: LOW (起動完了)");
  Serial.println("開始");
}

void loop() {
  unsigned long now = millis();
  loopCount++;
  
  // SubCore1からゲームパッドデータ受信（複数回処理）
  int8_t msgid;
  uint32_t data;
  
  for (int i = 0; i < 8; i++) {  // 最大8メッセージ/ループ
    int ret = MP.Recv(&msgid, &data, 1);
    if (ret <= 0) break;
    
    if (msgid == MSG_GAMEPAD) {
      sc1RecvCount++;
      
      uint8_t newDir = data & 0xFF;
      uint16_t newBtn = (data >> 8) & 0xFFFF;
      
      // 入力変化検出
      if (newDir != currentDir || newBtn != currentBtn) {
        inputChanged = true;
        currentDir = newDir;
        currentBtn = newBtn;
        inputStartTime = now;
        inputChangeCount++;  // 入力変化カウント
        
        // 反応速度テスト中（マーカー描画完了後）ならReact時間を計算
        if (reactionMarkerActive && !reactionWaitingForDraw && newBtn != 0) {
          reactionReactMs = (uint16_t)(now - reactionMarkerDrawnTime);
          // SubCore2に送信（React時間）
          uint32_t reactData = ((uint32_t)reactionReactMs << 16) | 0x02;  // bit0-1: 02=React
          MP.Send(MSG_REACTION_RESULT, reactData, 2);
          reactionMarkerActive = false;  // テスト終了
        }
        
        // フレーム内の入力を更新（SubCore2へのフレーム単位送信用）
        frameDir = newDir;
        frameBtn = newBtn;
        frameHasInput = true;
        
        // SubCore3には即座に送信（コンボ解析用、ms精度維持）
        MP.Send(MSG_GAMEPAD, data, 3);
      }
      
      hasInput = true;
    }
    else if (msgid == MSG_PAD_STATUS) {
      // 接続状態をSubCore2に転送
      Serial.print("[Main] PAD_STATUS: 0x");
      Serial.println(data, HEX);
      MP.Send(MSG_PAD_STATUS, data, 2);
      
      // 切断時（0xFF）は入力送信を停止
      if ((data & 0xFF) == 0xFF) {
        hasInput = false;
        currentDir = 0;
        currentBtn = 0;
        currentFrame = 0;
        digitalWrite(LED0, LOW);  // コントローラ切断 → LED0 OFF
        Serial.println("Pad disconnected - input stopped");
      } else {
        digitalWrite(LED0, HIGH);  // コントローラ接続 → LED0 ON
      }
    }
    else if (msgid == MSG_FPS_SC1) {
      // SC1 FPS情報をSubCore2に転送
      MP.Send(MSG_FPS_SC1, data, 2);
    }
  }
  
  // SubCore2からメッセージ受信
  for (int i = 0; i < 4; i++) {
    int ret = MP.Recv(&msgid, &data, 2);
    if (ret <= 0) break;
    
    if (msgid == MSG_REACTION_MARKER_DRAWN) {
      // マーカー描画完了 → 時刻を記録 + 差分検出をリセット
      reactionMarkerDrawnTime = now;
      reactionWaitingForDraw = false;
      reactionMarkerActive = true;        // 描画完了後、マーカー有効化
      reactionActionDetected = false;     // Action検出フラグリセット
      reactionActionMs = 0;
      reactionReactMs = 0;
      hasPrevFrame = false;               // 差分検出リセット（マーカー表示後の画像が基準）
      digitalWrite(LED1, HIGH);           // マーカー表示 → LED1 ON
      Serial.print("[Main] Marker drawn at ");
      Serial.println(reactionMarkerDrawnTime);
    }
  }
  
  // SubCore3からメッセージ受信（複数回処理）
  // MSG_HISTORYのみ（新規追加＋色更新の両方を含む）
  for (int i = 0; i < 16; i++) {  // 最大16メッセージ/ループ
    int ret = MP.Recv(&msgid, &data, 3);
    if (ret <= 0) break;
    
    if (msgid == MSG_HISTORY) {
      sc3RecvCount++;
      // 履歴データをSubCore2に転送（エラー時もスキップ）
      MP.Send(MSG_HISTORY, data, 2);
    }
    else if (msgid == MSG_BUTTON_STATS) {
      // ボタン押下時間統計をSubCore2に転送
      MP.Send(MSG_BUTTON_STATS, data, 2);
    }
    else if (msgid == MSG_FPS_SC3) {
      // SC3 FPS情報をSubCore2に転送
      MP.Send(MSG_FPS_SC3, data, 2);
    }
    else if (msgid == MSG_REACTION_RESULT) {
      // 反応速度テスト結果をSubCore2に転送（SubCore3からは使用しない）
      // MainCoreで計算したReact時間のみを転送
      MP.Send(MSG_REACTION_RESULT, data, 2);
    }
  }
  
  // フレームカウント更新（0.1F単位、1フレーム=16.67ms）
  if (hasInput) {
    unsigned long elapsed = now - inputStartTime;
    int newFrame = (elapsed * 10 / 17) + 10;  // 0.1F単位
    if (newFrame > 999) newFrame = 999;  // 最大99.9F
    currentFrame = newFrame;
  }
  
  // 1フレーム（約16.67ms）ごとにSubCore2へ履歴送信
  if (now - lastFrameSendTime >= FRAME_INTERVAL_MS) {
    if (frameHasInput) {
      // フレーム内の最終入力をSubCore2に送信（履歴表示用）
      uint32_t frameData = (frameDir & 0xFF) | ((frameBtn & 0xFFFF) << 8);
      MP.Send(MSG_GAMEPAD, frameData, 2);
      sc2SendCount++;  // 送信カウント
      
      // フレーム状態リセット
      frameHasInput = false;
    }
    lastFrameSendTime = now;
  }
  
  // 30fpsでSubCore2へ送信（現在入力表示用）
  if (now - lastSendTime >= SEND_INTERVAL_MS) {
    if (hasInput) {
      // データパック: frame(10bit,0.1F単位) | btn(12) | dir(4) | reserved(6)
      uint32_t drawData = ((currentFrame & 0x3FF) << 22) |
                          ((currentBtn & 0xFFF) << 10) |
                          ((currentDir & 0xF) << 6);
      
      MP.Send(MSG_DRAW, drawData, 2);
      
      inputChanged = false;
    }
    lastSendTime = now;
  }
  
  // 5秒ごとに統計出力 + MainCore Hz送信
  if (now - statsStartTime >= 5000) {
    unsigned long loopHz = loopCount / 5;
    int drawFps = sc2SendCount / 5;
    int inputChanges = sc1RecvCount / 5;
    int camFps = captureCount / 5;
    int comboResults = sc3RecvCount / 5;
    Serial.print("Loop: ");
    Serial.print(loopHz);
    Serial.print(" Hz, Cam: ");
    Serial.print(camFps);
    Serial.print(" fps, Draw: ");
    Serial.print(drawFps);
    Serial.print(" fps, Input: ");
    Serial.print(inputChanges);
    Serial.print(" /sec, Combo: ");
    Serial.print(comboResults);
    Serial.println(" /sec");
    
    Serial.print("InputChanges: ");
    Serial.print(inputChangeCount);
    Serial.print(" total, MotionDetect: ");
    Serial.print(motionDetectCount);
    Serial.println(" /5sec");
    
    // MainCore Hz情報をSubCore2に送信（kHz単位）
    uint32_t loopKHz = loopHz / 1000;
    MP.Send(MSG_FPS_MAIN, loopKHz, 2);
    
    motionDetectCount = 0;
    
    loopCount = 0;
    sc2SendCount = 0;
    sc1RecvCount = 0;
    sc3RecvCount = 0;
    captureCount = 0;
    statsStartTime = now;
  }
  
  // 反応速度テスト制御（hasInput時のみ＝ゲームパッド接続時）
  if (hasInput) {
    // マーカー表示中で3秒経過したら消す
    if (reactionMarkerActive && (now - reactionMarkerDrawnTime >= REACTION_MARKER_DURATION)) {
      reactionMarkerActive = false;
      reactionWaitingForDraw = false;
      reactionActionDetected = false;
      digitalWrite(LED1, LOW);  // マーカー非表示 → LED1 OFF
      // タイムアウト通知（結果0 = タイムアウト）
      MP.Send(MSG_REACTION_RESULT, (uint32_t)0, 2);
    }
    
    // 次のマーカー表示タイミング
    if (!reactionMarkerActive && !reactionWaitingForDraw && now >= reactionNextTime) {
      // ランダム色を生成（RGB565）
      uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF};  // 赤緑青黄紫水
      uint16_t color = colors[random(6)];
      
      // マーカー表示開始を送信
      uint32_t startData = (uint32_t)color;  // 色情報を含める
      int sendRet = MP.Send(MSG_REACTION_START, startData, 2);  // SubCore2
      
      Serial.print("[Main] Reaction test started, color=0x");
      Serial.print(color, HEX);
      Serial.print(", sendRet=");
      Serial.print(sendRet);
      Serial.println(", waiting for draw...");
      
      reactionWaitingForDraw = true;   // 描画完了待ち
      reactionMarkerActive = false;    // 描画完了後にtrueにする（MSG_REACTION_MARKER_DRAWN受信時）
      reactionActionDetected = false;  // Action未検出
      
      // 次の表示時刻を設定（5〜10秒後、ランダム）
      reactionNextTime = now + REACTION_MARKER_DURATION + random(5000, 10001);
    }
  }
}
