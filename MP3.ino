#include <Arduino.h>
#include "SPI.h"
#include "SD.h"
#include "AudioFileSourceSD.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include <Adafruit_NeoPixel.h>
#include <TFT_eSPI.h>
#include <vector>
#include "BluetoothA2DPSink.h"

// ---- 引脚定义 ----
#define BTN_MODE     0
#define SD_CS        5
#define SD_MOSI     23
#define SD_MISO     19
#define SD_SCK      18

// I2S 引脚
#define I2S_LRC     27
#define I2S_BCLK    26
#define I2S_DOUT    33

#define LED_PIN     22
#define TFT_BL      21

#define BTN_VOL_UP   36
#define BTN_VOL_DOWN 39
#define BTN_PREV     34
#define BTN_PAUSE    35
#define BTN_NEXT     32

// ---- 内存配置 (16MB Flash, 无 PSRAM) ----
const int BUFFER_SIZE = 16384; 

// ---- 全局对象 ----
TFT_eSPI tft = TFT_eSPI();
Adafruit_NeoPixel strip(12, LED_PIN, NEO_GRB + NEO_KHZ800);

// SD 播放对象
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSD *file = nullptr;
AudioFileSourceBuffer *buff = nullptr;
AudioOutputI2S *out = nullptr;

// 蓝牙对象
BluetoothA2DPSink *a2dp_sink = nullptr;

enum Mode { MODE_SD, MODE_BT };
Mode currentMode = MODE_SD;
std::vector<String> playlists;
int currentIndex = 0;
float currentVolume = 0.3;
bool isManualStopped = false; 
unsigned long lastBtnTime = 0;
uint16_t currentThemeColor = TFT_GREEN;

// ---- 辅助函数 ----
String getFileName(String path) {
    int slashIndex = path.lastIndexOf('/');
    return (slashIndex != -1) ? path.substring(slashIndex + 1) : path;
}

// ---- UI 函数 ----
void drawRainbowHeader(int colorOffset) {
    int r = sin(0.1 * colorOffset + 0) * 127 + 128;
    int g = sin(0.1 * colorOffset + 2) * 127 + 128;
    int b = sin(0.1 * colorOffset + 4) * 127 + 128;
    currentThemeColor = tft.color565(r, g, b);
    tft.setTextColor(currentThemeColor, TFT_BLACK);
    tft.drawCentreString("MERRY", 120, 5, 4); 
    tft.drawCentreString("CHRISTMAS", 120, 35, 4); 
}

void drawWaveform(float vol) {
    static int prevHeights[10] = {0};
    int xStart = 45; int yBottom = 215; int barWidth = 10; int spacing = 5;
    int maxH = 10 + (vol * 40); 
    if(maxH > 45) maxH = 45;

    for (int i = 0; i < 10; i++) {
        int h = random(3, maxH);
        if (abs(h - prevHeights[i]) > 4) { 
            if (prevHeights[i] > h) {
                tft.fillRect(xStart + i * (barWidth + spacing), yBottom - prevHeights[i], barWidth, prevHeights[i] - h, TFT_BLACK);
            }
            tft.fillRect(xStart + i * (barWidth + spacing), yBottom - h, barWidth, h, currentThemeColor);
            prevHeights[i] = h;
        }
    }
}

// ---- 资源管理 ----
void stopSD() {
    if (mp3) { mp3->stop(); delete mp3; mp3 = nullptr; }
    if (buff) { buff->close(); delete buff; buff = nullptr; }
    if (file) { file->close(); delete file; file = nullptr; }
    if (out) { out->stop(); delete out; out = nullptr; }
}

void stopBT() {
    if (a2dp_sink) {
        a2dp_sink->stop();
        delete a2dp_sink; 
        a2dp_sink = nullptr;
    }
}

void playCurrentSong() {
    stopSD();
    if (playlists.empty()) return;

    if (!out) {
        out = new AudioOutputI2S();
        out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
        out->SetGain(currentVolume);
    }

    String fullPath = playlists[currentIndex];
    String shortName = getFileName(fullPath);

    tft.fillRect(0, 80, 240, 160, TFT_BLACK); 
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("SD PLAYING", 120, 95, 4);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawCentreString(shortName.c_str(), 120, 140, 2);

    file = new AudioFileSourceSD(fullPath.c_str());
    buff = new AudioFileSourceBuffer(file, BUFFER_SIZE); 
    mp3 = new AudioGeneratorMP3();
    mp3->begin(buff, out);
    isManualStopped = false;
    
    Serial.printf("Playing SD. Free Heap: %d\n", ESP.getFreeHeap());
}

// ---- 模式切换 ----
void switchMode() {
    static unsigned long lastSwitchTime = 0;
    if (millis() - lastSwitchTime < 1000) return;
    lastSwitchTime = millis();

    if (currentMode == MODE_SD) {
        // SD -> BT
        stopSD(); 
        currentMode = MODE_BT;

        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawCentreString("BT MODE", 120, 100, 4);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawCentreString("Starting...", 120, 140, 2);

        // 创建蓝牙对象
        a2dp_sink = new BluetoothA2DPSink();
        
        // ---- 修正点 1：使用结构体配置引脚 (兼容旧版库) ----
        i2s_pin_config_t my_pin_config = {
            .bck_io_num = I2S_BCLK,
            .ws_io_num = I2S_LRC,
            .data_out_num = I2S_DOUT,
            .data_in_num = I2S_PIN_NO_CHANGE
        };
        a2dp_sink->set_pin_config(my_pin_config);
        // ------------------------------------------------
        
        a2dp_sink->start("DSTIKE");
        a2dp_sink->set_volume(currentVolume * 100); 

        tft.fillRect(0, 130, 240, 30, TFT_BLACK);
        tft.drawCentreString("NAME: DSTIKE", 120, 140, 2);
        
    } else {
        // BT -> SD
        stopBT();
        delay(200); 
        currentMode = MODE_SD;
        tft.fillScreen(TFT_BLACK);
        playCurrentSong();
    }
}

// ---- 按键处理 ----
void checkModeButton() {
    static int clickCount = 0;
    static unsigned long lastClickTime = 0;
    static bool lastState = HIGH;
    bool currentState = digitalRead(BTN_MODE);
    if (lastState == HIGH && currentState == LOW) {
        unsigned long now = millis();
        if (now - lastClickTime > 50) { clickCount++; lastClickTime = now; }
    }
    lastState = currentState;
    if (clickCount > 0 && (millis() - lastClickTime > 400)) {
        if (clickCount >= 2) switchMode(); 
        clickCount = 0;
    }
}

void handleButtons() {
    if (millis() - lastBtnTime < 150) return;

    if (digitalRead(BTN_PAUSE) == LOW) {
        if (currentMode == MODE_SD) {
            if (mp3 && mp3->isRunning()) { 
                isManualStopped = !isManualStopped;
                if(isManualStopped) {
                    tft.fillRect(0, 175, 240, 55, TFT_BLACK); 
                    tft.setTextColor(TFT_RED, TFT_BLACK); 
                    tft.drawCentreString("PAUSED", 120, 190, 4);
                } else {
                    tft.fillRect(0, 175, 240, 55, TFT_BLACK); 
                }
            } else if (!mp3) {
                 playCurrentSong();
            }
        } else if (currentMode == MODE_BT && a2dp_sink) {
             // ---- 修正点 2：使用 ESP 底层状态常量 (兼容旧版库) ----
             if(a2dp_sink->get_audio_state() == ESP_A2D_AUDIO_STATE_STARTED) {
                 a2dp_sink->pause();
             } else {
                 a2dp_sink->play();
             }
             // ------------------------------------------------
        }
        lastBtnTime = millis();
    }
    
    if (digitalRead(BTN_NEXT) == LOW) {
        if (currentMode == MODE_SD) { currentIndex = (currentIndex + 1) % playlists.size(); playCurrentSong(); }
        else if (currentMode == MODE_BT && a2dp_sink) { a2dp_sink->next(); }
        lastBtnTime = millis();
    }
    
    if (digitalRead(BTN_PREV) == LOW) {
        if (currentMode == MODE_SD) { currentIndex = (currentIndex - 1 + playlists.size()) % playlists.size(); playCurrentSong(); }
        else if (currentMode == MODE_BT && a2dp_sink) { a2dp_sink->previous(); }
        lastBtnTime = millis();
    }

    if (digitalRead(BTN_VOL_UP) == LOW) {
        currentVolume = min(currentVolume + 0.05f, 1.0f);
        if (currentMode == MODE_SD && out) out->SetGain(currentVolume);
        else if (currentMode == MODE_BT && a2dp_sink) a2dp_sink->set_volume(currentVolume * 100);
        lastBtnTime = millis();
    }

    if (digitalRead(BTN_VOL_DOWN) == LOW) {
        currentVolume = max(currentVolume - 0.05f, 0.0f);
        if (currentMode == MODE_SD && out) out->SetGain(currentVolume);
        else if (currentMode == MODE_BT && a2dp_sink) a2dp_sink->set_volume(currentVolume * 100);
        lastBtnTime = millis();
    }
}

void setup() {
    setCpuFrequencyMhz(240);
    Serial.begin(115200);
    
    pinMode(BTN_MODE, INPUT_PULLUP);
    pinMode(BTN_VOL_UP, INPUT_PULLUP); pinMode(BTN_VOL_DOWN, INPUT_PULLUP);
    pinMode(BTN_PREV, INPUT_PULLUP); pinMode(BTN_PAUSE, INPUT_PULLUP); 
    pinMode(BTN_NEXT, INPUT_PULLUP); 
    pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);

    tft.init(); tft.setRotation(0); tft.fillScreen(TFT_BLACK);
    strip.begin(); strip.setBrightness(20); strip.show();

    // 2.0.17 稳定 SD 配置
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI, 20000000)) { 
        if (!SD.begin(SD_CS)) {
            tft.setTextColor(TFT_RED);
            tft.drawCentreString("SD Fail!", 120, 120, 4);
            while(1);
        }
    }

    File root = SD.open("/");
    while (File entry = root.openNextFile()) {
        String name = entry.name();
        if (name.endsWith(".mp3") || name.endsWith(".MP3")) playlists.push_back("/" + name);
        entry.close();
    }
    root.close();

    playCurrentSong();
}

void loop() {
    unsigned long burstStart = millis();
    if (currentMode == MODE_SD && mp3 && mp3->isRunning() && !isManualStopped) {
        while (millis() - burstStart < 15) { 
            if (!mp3->loop()) { 
                mp3->stop();
                currentIndex = (currentIndex + 1) % playlists.size();
                playCurrentSong();
                break;
            }
        }
    }

    checkModeButton();
    handleButtons();

    static unsigned long lastUpdate = 0;
    static int hueOffset = 0; 
    
    if (millis() - lastUpdate > 100) { 
        drawRainbowHeader(hueOffset / 100); 
        for (int i = 0; i < 12; i++) {
            strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hueOffset + (i * 5461))));
        }
        strip.show(); 

        if (currentMode == MODE_SD && mp3 && mp3->isRunning() && !isManualStopped) drawWaveform(currentVolume);
        else if (currentMode == MODE_BT) drawWaveform(0.4); 
        
        hueOffset += 512;
        lastUpdate = millis();
    }
}