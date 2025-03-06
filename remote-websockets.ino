#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <SPI.h>
#include <LoRa.h>
#include <SPIFFS.h>
#include <Crypto.h>
#include <AES.h>
#include <Base64.h>

WebServer server(80);  
WebSocketsServer webSocket(81);  // WebSockets on port 81

#define SS 18
#define RST 14
#define DIO0 26

String nodeID;

AES128 aes;

// 16-byte (128-bit) secret key - must be the same on all devices
uint8_t aesKey[16] = { 0x54, 0x68, 0x69, 0x73, 0x49, 0x73, 0x41, 0x53, 0x65, 0x63, 0x72, 0x65, 0x74, 0x4B, 0x65, 0x79 };

void encryptMessage(String plainText, uint8_t *encryptedData) {
    uint8_t plainBytes[16] = {0};
    plainText.getBytes(plainBytes, 16);
    aes.setKey(aesKey, 16);
    aes.encryptBlock(encryptedData, plainBytes);
}

String decryptMessage(uint8_t *encryptedData) {
    uint8_t decryptedBytes[16] = {0};
    aes.setKey(aesKey, 16);
    aes.decryptBlock(decryptedBytes, encryptedData);
    return String((char *)decryptedBytes);
}

void saveMessage(const String &message) {
    uint8_t encryptedData[16] = {0};
    encryptMessage(message, encryptedData);
    File file = SPIFFS.open("/chatlog.txt", "a");
    if (file) {
        file.write(encryptedData, 16);
        file.close();
    }
}

String loadMessages() {
    File file = SPIFFS.open("/chatlog.txt", "r");
    String history = "";
    if (file) {
        uint8_t encryptedData[16];
        while (file.read(encryptedData, 16) == 16) {
            history += decryptMessage(encryptedData) + "<br>";
        }
        file.close();
    }
    return history;
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    if (type == WStype_TEXT) {
        String message = String((char *)payload);
        Serial.println("Received from WebSocket: " + message);

        LoRa.beginPacket();
        LoRa.print(message);
        LoRa.endPacket();

        saveMessage(message);
        webSocket.broadcastTXT(message);
    }
}

void receiveLoRaMessages() {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        String receivedMsg = "";
        while (LoRa.available()) {
            receivedMsg += (char)LoRa.read();
        }
        Serial.println("LoRa Received: " + receivedMsg);

        saveMessage(receivedMsg);
        webSocket.broadcastTXT(receivedMsg);
    }
}

const char htmlPage[] PROGMEM = R"rawliteral(
<html>
<head>
    <title>LoRa Chat</title>
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <style>
      li {
        list-style: none;
      }
    </style>
</head>
<body>
    <h1>LoRa Chat</h1>
    <h2>Node %NODE_ID%</h2>
    <input id='nickname' placeholder='Enter nickname'>
    <input id='msg' placeholder='Type a message'>
    <button onclick='sendMessage()'>Send</button>
    <ul id='chat'></ul>
    <script>
        const ws = new WebSocket('ws://' + location.host + ':81');
        const nodeID = "%NODE_ID%";  // Injected from ESP32
        const chat = document.getElementById('chat');
        const nameInput = document.getElementById('nickname');
        const msgInput = document.getElementById('msg');

        ws.onopen = () => {
            fetch('/history').then(res => res.text()).then(data => {
                chat.innerHTML = data;
            });
        };

        ws.onmessage = event => {
            console.log("Received WebSocket message:", event.data);
            var li = document.createElement('li');
            li.innerHTML = event.data;
            chat.appendChild(li);
        };

        function sendMessage() {
            let nickname = nameInput.value || "Guest";
            let message = nickname + ": " + msgInput.value;
            ws.send(message);
            msgInput.value = ""
            msgInput.focus()
        }
    </script>
</body>
</html>
)rawliteral";

void handleRoot() {
    String page = htmlPage;
    page.replace("%NODE_ID%", nodeID);  // Replace with actual Node ID
    server.send(200, "text/html", page);
}

void handleHistory() {
    server.send(200, "text/html", loadMessages());
}

void clearHistory() {
    if (SPIFFS.remove("/chatlog.txt")) {
        Serial.println("Chat log deleted successfully.");
        server.send(200, "text/plain", "Chat log deleted successfully.");
    } else {
        Serial.println("Failed to delete chat log.");
        server.send(500, "text/plain", "Failed to delete chat log.");
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println("Hello, World! LilyGO LoRa is connected.");

    uint64_t chipID = ESP.getEfuseMac();  // Get unique 64-bit MAC address
    char nodeIDBuffer[13];  // Buffer for 12 hex characters + null terminator
    sprintf(nodeIDBuffer, "%012llX", chipID);
    nodeID = String(nodeIDBuffer);
    Serial.println("Node ID: " + nodeID);

    String ssid = "LoRa_Chat_AP_" + nodeID;
    const char *password = "12345678";  

    WiFi.softAP(ssid, password);
    Serial.println("WiFi AP Started: ");
    Serial.println(ssid);
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Initialization Failed!");
    }

    SPI.begin(5, 19, 27, 18);
    LoRa.setPins(18, 14, 26);

    if (!LoRa.begin(868E6)) {
        Serial.println("LoRa Init Failed! Running without LoRa...");
    } else {
        Serial.println("LoRa Started!");
        
        // Set LoRa Spreading Factor (SF)
        // Lower SF (e.g., 6) → Faster speed, shorter range
        // Higher SF (e.g., 12) → Longer range, slower transmission
        LoRa.setSpreadingFactor(10);

        // Set LoRa Transmission Power (TxPower)
        // Lower power (e.g., 2 dBm) → Saves battery, shorter range
        // Higher power (e.g., 20 dBm) → Better range, higher battery usage
        LoRa.setTxPower(17);
    }

    server.on("/", handleRoot);
    server.on("/history", handleHistory);
    server.on("/clearHistory", clearHistory);
    server.begin();
    Serial.println("Web Server Started!");

    webSocket.begin();
    webSocket.onEvent(onWebSocketEvent);
    Serial.println("WebSockets Started!");
}

void loop() {
    server.handleClient();
    webSocket.loop();
    receiveLoRaMessages();
    delay(100);
}