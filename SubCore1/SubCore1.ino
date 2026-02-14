/**
 * ========================================
 *   gamepad_display_v7 - SubCore1
 * ========================================
 * 
 * ゲームパッド入力受信
 * 
 * - ハードウェアUART受信（Serial2: D0, 115200bps）
 * - 方向・ボタン変化時のみMainCoreへ送信
 * - 接続状態の検出と通知
 * 
 * v7: v5ベース
 */

#if (SUBCORE != 1)
#error "This sketch should be compiled for SubCore1"
#endif

#include <MP.h>

// メッセージID
#define MSG_GAMEPAD     1
#define MSG_PAD_STATUS  5  // 接続状態通知
#define MSG_FPS_SC1     9  // SC1 FPS情報

// パッド種別
#define PAD_DISCONNECTED  0xFF  // Pff: 非接続
#define PAD_HID           0x01  // P01: HID
#define PAD_XINPUT        0x02  // P02: Xinput

// デバッグ用: 生データログ出力
#define DEBUG_RAW_LOG 0

// パケットバッファ
char packetBuf[64];
int packetLen = 0;

// 前回の送信データ
uint8_t lastDir = 0xFF;
uint16_t lastBtn = 0xFFFF;

// 現在の接続状態
uint8_t currentPadType = PAD_DISCONNECTED;

// ループ周波数計測（Hz）
static unsigned long lastFpsSendTime = 0;
static unsigned long loopCountPerSec = 0;

void setup() {
  pinMode(LED2, OUTPUT);
  
  // ハードウェアUART初期化（D0=RX, D1=TX）
  Serial2.begin(115200);
  
  MP.begin();
  
#if DEBUG_RAW_LOG
  MPLog("SubCore1: Raw data logging enabled\n");
#endif
  
  // 起動通知
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED2, HIGH);
    delay(100);
    digitalWrite(LED2, LOW);
    delay(100);
  }
  
  // 初期状態として非接続を通知
  sendPadStatus(PAD_DISCONNECTED);
}

void loop() {
  unsigned long now = millis();
  loopCountPerSec++;
  
  // ハードウェアUARTでデータ受信
  while (Serial2.available()) {
    char c = Serial2.read();
    
    // 受信した全バイトをパケットバッファに追加
    if (packetLen < 62) {
      packetBuf[packetLen++] = c;
    }
    
    // パケット終端（;または改行）
    if (c == ';' || c == '\n' || c == '\r') {
      if (packetLen > 1) {
        packetBuf[packetLen] = '\0';
        processPacket();
      }
      packetLen = 0;
    }
  }
  
  // 1秒ごとにループ周波数を送信（kHz単位で送信）
  if (now - lastFpsSendTime >= 1000) {
    uint32_t loopHz = loopCountPerSec / 1000;  // kHz
    MP.Send(MSG_FPS_SC1, loopHz);
    loopCountPerSec = 0;
    lastFpsSendTime = now;
  }
}

// 接続状態を送信
void sendPadStatus(uint8_t padType) {
  uint32_t data = padType;
  MP.Send(MSG_PAD_STATUS, data);
  MPLog("PAD_STATUS: 0x%02X\n", padType);
}

void processPacket() {
  // 終端文字を除いた長さで判定
  int dataLen = packetLen;
  if (dataLen > 0 && (packetBuf[dataLen-1] == ';' || packetBuf[dataLen-1] == '\n' || packetBuf[dataLen-1] == '\r')) {
    dataLen--;
  }
  
#if DEBUG_RAW_LOG
  char saved = packetBuf[dataLen];
  packetBuf[dataLen] = '\0';
  MPLog("RAW[%d]: %s\n", dataLen, packetBuf);
  packetBuf[dataLen] = saved;
#endif
  
  if (dataLen < 5) return;
  if (packetBuf[0] < '0' || packetBuf[0] > '9') return;
  
  // 3文字目で判別: 'P'=接続状態, 'G'=入力データ
  char packetType = packetBuf[2];
  
  // 接続状態パケット (10Pff, 10P01, 10P02)
  if (packetType == 'P' && dataLen >= 5) {
    uint8_t newPadType = PAD_DISCONNECTED;
    
    // 種別コード解析 (P + 2桁hex)
    int h1 = hexCharToInt(packetBuf[3]);
    int h2 = hexCharToInt(packetBuf[4]);
    
    if (h1 >= 0 && h2 >= 0) {
      uint8_t typeCode = (h1 << 4) | h2;
      
      if (typeCode == 0xFF) {
        newPadType = PAD_DISCONNECTED;
      } else if (typeCode == 0x01) {
        newPadType = PAD_HID;
      } else if (typeCode == 0x02) {
        newPadType = PAD_XINPUT;
      }
    }
    
    // 状態が変わった時のみ通知
    if (newPadType != currentPadType) {
      currentPadType = newPadType;
      sendPadStatus(currentPadType);
      
      // 切断時は入力状態をリセット
      if (currentPadType == PAD_DISCONNECTED) {
        lastDir = 0xFF;
        lastBtn = 0xFFFF;
      }
    }
    return;
  }
  
  // 入力データパケット (10G0N000)
  if (packetType == 'G') {
    // 入力データを受信 = パッドが接続されている
    // currentPadTypeが非接続なら、接続状態に更新（切断検出のため）
    if (currentPadType == PAD_DISCONNECTED) {
      currentPadType = PAD_XINPUT;  // デフォルトでXinputと仮定
      MPLog("Input received -> connected (assumed Xinput)\n");
    }
    
    // パケット解析
    uint8_t direction = 0xFF;
    uint16_t buttons = 0;
    
    for (int i = 2; i < packetLen; i++) {
      char c = packetBuf[i];
      
      // 方向 (G + 1桁hex)
      if (c == 'G' && i + 1 < packetLen) {
        int gVal = hexCharToInt(packetBuf[++i]);
        if (gVal >= 0) {
          direction = gVal & 0xF;
        }
      }
      // ボタン (N + 3-4桁hex)
      else if (c == 'N') {
        int nDataLen = 0;
        int j = i + 1;
        while (j < packetLen && hexCharToInt(packetBuf[j]) >= 0) {
          nDataLen++;
          j++;
        }
        
        if (nDataLen == 4 && i + 4 < packetLen) {
          int b0 = hexCharToInt(packetBuf[++i]);
          int b1 = hexCharToInt(packetBuf[++i]);
          int b2 = hexCharToInt(packetBuf[++i]);
          int b3 = hexCharToInt(packetBuf[++i]);
          if (b0 >= 0 && b1 >= 0 && b2 >= 0 && b3 >= 0) {
            buttons = (b0 << 12) | (b1 << 8) | (b2 << 4) | b3;
          }
        }
        else if (nDataLen == 3 && i + 3 < packetLen) {
          int b1 = hexCharToInt(packetBuf[++i]);
          int b2 = hexCharToInt(packetBuf[++i]);
          int b3 = hexCharToInt(packetBuf[++i]);
          if (b1 >= 0 && b2 >= 0 && b3 >= 0) {
            buttons = (b1 << 8) | (b2 << 4) | b3;
          }
        }
      }
    }
    
    // 方向データがなければ無視
    if (direction == 0xFF) return;
    
    // 変化時のみ送信
    if (direction != lastDir || buttons != lastBtn) {
      digitalWrite(LED2, HIGH);
      
      uint32_t data = ((uint32_t)buttons << 8) | direction;
      
      if (MP.Send(MSG_GAMEPAD, data) >= 0) {
        lastDir = direction;
        lastBtn = buttons;
      }
      
      digitalWrite(LED2, LOW);
    }
  }
}

int hexCharToInt(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
