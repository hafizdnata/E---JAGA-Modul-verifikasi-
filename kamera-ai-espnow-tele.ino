#include <esp_now.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <Seeed_Arduino_SSCMA.h>

// Telegram Bot Token
String botToken = "8415472731:AAEm00GLhqmp5eF1UiFya1ODMQwzb6iVfTc";
String telegramURL = "https://api.telegram.org/bot" + botToken;

String chatIDs[50];
int chatCount = 0;
int lastUpdateID = 0;

#define EEPROM_SIZE 512
#define CHAT_COUNT_ADDR 0
#define CHAT_IDS_ADDR 4

typedef struct struct_message {
    char text[32];
} struct_message;

struct_message incomingData;
struct_message replyData;

uint8_t txAddress[] = {0x24, 0xEC, 0x4A, 0x01, 0x47, 0x70};

SSCMA AI;
bool aiActive = false;
unsigned long aiStartTime = 0;
const unsigned long aiTimeout = 15000; // 15 detik

void loadChatIDs() {
  EEPROM.get(CHAT_COUNT_ADDR, chatCount);
  if (chatCount < 0 || chatCount > 50) {
    chatCount = 0;
    return;
  }
  int addr = CHAT_IDS_ADDR;
  for (int i = 0; i < chatCount; i++) {
    int len;
    EEPROM.get(addr, len);
    addr += sizeof(int);
    if (len > 0 && len < 20) {
      char buffer[20];
      for (int j = 0; j < len; j++) {
        buffer[j] = EEPROM.read(addr + j);
      }
      buffer[len] = '\0';
      chatIDs[i] = String(buffer);
      addr += len;
    }
  }
}

void saveChatIDs() {
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.put(CHAT_COUNT_ADDR, chatCount);
  int addr = CHAT_IDS_ADDR;
  for (int i = 0; i < chatCount; i++) {
    int len = chatIDs[i].length();
    EEPROM.put(addr, len);
    addr += sizeof(int);
    for (int j = 0; j < len; j++) {
      EEPROM.write(addr + j, chatIDs[i][j]);
    }
    addr += len;
  }
  EEPROM.commit();
  Serial.println("Chat IDs saved to EEPROM");
}

void addChatID(String chatID) {
  for (int i = 0; i < chatCount; i++) {
    if (chatIDs[i] == chatID) {
      Serial.println("Chat ID " + chatID + " already exists");
      return;
    }
  }
  if (chatCount < 50) {
    chatIDs[chatCount] = chatID;
    chatCount++;
    saveChatIDs();
    Serial.println("Added chat ID: " + chatID + " (Total: " + String(chatCount) + ")");
  } else {
    Serial.println("Maximum chat IDs reached!");
  }
}

void removeChatID(String chatID) {
  for (int i = 0; i < chatCount; i++) {
    if (chatIDs[i] == chatID) {
      for (int j = i; j < chatCount - 1; j++) {
        chatIDs[j] = chatIDs[j + 1];
      }
      chatCount--;
      saveChatIDs();
      Serial.println("Removed chat ID: " + chatID + " (Total: " + String(chatCount) + ")");
      return;
    }
  }
  Serial.println("Chat ID " + chatID + " not found");
}

void sendMessage(String chatID, String message) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = telegramURL + "/sendMessage";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  DynamicJsonDocument doc(1024);
  doc["chat_id"] = chatID;
  doc["text"] = message;
  doc["parse_mode"] = "HTML";
  String jsonString;
  serializeJson(doc, jsonString);
  int httpCode = http.POST(jsonString);
  if (httpCode == 200) {
    Serial.println("Message sent to " + chatID + ": " + message);
  } else {
    Serial.println("Failed to send message to " + chatID + ". HTTP Code: " + String(httpCode));
  }
  http.end();
}

void broadcastMessage(String message) {
  if (chatCount == 0) {
    Serial.println("No subscribers to send message to");
    return;
  }
  Serial.println("Broadcasting message to " + String(chatCount) + " subscribers...");
  for (int i = 0; i < chatCount; i++) {
    sendMessage(chatIDs[i], message);
    delay(100);
  }
}

void checkTelegramMessages() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = telegramURL + "/getUpdates?offset=" + String(lastUpdateID + 1);
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode == 200) {
    String response = http.getString();
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, response);
    JsonArray updates = doc["result"];
    for (JsonObject update : updates) {
      int updateID = update["update_id"];
      lastUpdateID = updateID;
      if (update.containsKey("message")) {
        JsonObject message = update["message"];
        String chatID = String((long long)message["chat"]["id"]);
        String text = message["text"];
        String firstName = message["from"]["first_name"];
        Serial.println("Received message from " + firstName + " (ID: " + chatID + "): " + text);
        if (text == "/start") {
          addChatID(chatID);
          sendMessage(chatID, "✅ Anda telah berlangganan notifikasi ESP32!\n\nKirim /stop untuk berhenti berlangganan.");
        }
        else if (text == "/stop") {
          removeChatID(chatID);
          sendMessage(chatID, "❌ Anda telah berhenti berlangganan notifikasi ESP32.");
        }
        else if (text == "/status") {
          String statusMsg = "🤖 ESP32 Bot Status:\n";
          statusMsg += "📡 WiFi: Connected\n";
          statusMsg += "👥 Total subscribers: " + String(chatCount) + "\n";
          statusMsg += "⏰ Uptime: " + String(millis()/1000) + " seconds";
          sendMessage(chatID, statusMsg);
        }
        else if (text == "/help") {
          String helpMsg = "📋 Available commands:\n\n";
          helpMsg += "/start - Subscribe to notifications\n";
          helpMsg += "/stop - Unsubscribe from notifications\n";
          helpMsg += "/status - Show bot status\n";
          helpMsg += "/help - Show this help message";
          sendMessage(chatID, helpMsg);
        }
        else {
          sendMessage(chatID, "🤔 Unknown command. Send /help for available commands.");
        }
      }
    }
  }
  http.end();
}

// Callback ESP-NOW (hanya deteksi "LANSIA_JATUH")
void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *incomingDataBytes, int len) {
  if (len == sizeof(struct_message)) {
    memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));
    Serial.print("Dari TX: "); Serial.println(incomingData.text);

    if (strcmp(incomingData.text, "LANSIA_JATUH") == 0) {
      aiActive = true;
      aiStartTime = millis();
      Serial.println("AI diaktifkan karena deteksi LANSIA_JATUH dari ESP-NOW");
    }
  }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  loadChatIDs();

  WiFiManager wm;
  bool res = wm.autoConnect("ESP32-AP", "password123");
  if (!res) {
    Serial.println("[WiFi] Failed to connect");
    ESP.restart();
  } else {
    Serial.println("[WiFi] Connected!");
  }

  Serial.println("Loaded Chat IDs:");
  for (int i = 0; i < chatCount; i++) {
    Serial.println("- " + chatIDs[i]);
  }

  AI.begin();
  Serial.println("AI initialized");

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  WiFi.mode(WIFI_STA);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, txAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  Serial.print("WiFi status: ");
  Serial.println(WiFi.status());
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  static unsigned long lastTelegramCheck = 0;
  if (millis() - lastTelegramCheck > 5000) {
    checkTelegramMessages();
    lastTelegramCheck = millis();
  }

  if (aiActive && (millis() - aiStartTime < aiTimeout)) {
    if (!AI.invoke()) {
      Serial.println("invoke success");

      for (int i = 0; i < AI.classes().size(); i++) {
        if (AI.classes()[i].target == 1 && AI.classes()[i].score >= 90) {
          Serial.println("LANSIA JATUH TERDETEKSI OLEH AI!");
          broadcastMessage("⚠️ <b>Deteksi Jatuh</b> oleh AI!\nStatus: LANSIA JATUH\nSkor: " + String(AI.classes()[i].score) + "%");
          aiActive = false;
          break;
        }
      }
    } else {
      Serial.println("AI invoke failed");
    }
  } else if (aiActive && millis() - aiStartTime >= aiTimeout) {
    Serial.println("AI timeout, dinonaktifkan");
    aiActive = false;
  }

  delay(500);
}
