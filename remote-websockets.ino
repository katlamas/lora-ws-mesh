#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <SPI.h>
#include <LoRa.h>
#include <SPIFFS.h>

const char *ssid = "LoRa_Chat_AP_2";  
const char *password = "12345678";  

WebServer server(80);  
WebSocketsServer webSocket(81);  // WebSockets on port 81

#define SS 18
#define RST 14
#define DIO0 26

String nodeID;

void saveMessage(const String &message) {
    File file = SPIFFS.open("/chatlog.txt", "a");
    if (file) {
        file.println(message);
        file.close();
    }
}

String loadMessages() {
    File file = SPIFFS.open("/chatlog.txt", "r");
    String history = "";
    if (file) {
        while (file.available()) {
            history += file.readStringUntil('\n') + "<br>";
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
            let message = nickname + " (" + nodeID + "): " + msgInput.value;
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

    WiFi.softAP(ssid, password);
    Serial.println("WiFi AP Started: ");
    Serial.println(ssid);
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Initialization Failed!");
    }

    nodeID = String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.println("Node ID: " + nodeID);

    SPI.begin(5, 19, 27, 18);
    LoRa.setPins(18, 14, 26);

    if (!LoRa.begin(868E6)) {
        Serial.println("LoRa Init Failed! Running without LoRa...");
    } else {
        Serial.println("LoRa Started!");
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