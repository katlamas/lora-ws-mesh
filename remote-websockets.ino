#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <SPI.h>
#include <LoRa.h>
#include <pgmspace.h>

const char *ssid = "LoRa_Chat_AP_2";  
const char *password = "12345678";  

WebServer server(80);  
WebSocketsServer webSocket(81);  // WebSockets on port 81

#define SS 18
#define RST 14
#define DIO0 26

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    if (type == WStype_TEXT) {
        String message = String((char *)payload);
        Serial.println("Received from WebSocket: " + message);

        LoRa.beginPacket();
        LoRa.print(message);
        LoRa.endPacket();

        webSocket.broadcastTXT("You: " + message);
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

        webSocket.broadcastTXT("From Other Group: " + receivedMsg);
    }
}

const char htmlPage[] PROGMEM = R"rawliteral(
<html>
<head>
    <title>LoRa Chat</title>
</head>
<body>
    <h1>LoRa Chat</h1>
    <input id='msg' placeholder='Type a message'>
    <button onclick='sendMessage()'>Send</button>
    <ul id='chat'></ul>
    <script>
        var ws = new WebSocket('ws://' + location.host + ':81');
        ws.onmessage = event => {
            var li = document.createElement('li');
            li.innerHTML = event.data;
            document.getElementById('chat').appendChild(li);
        };
        function sendMessage() {
            ws.send(document.getElementById('msg').value);
        }
    </script>
</body>
</html>
)rawliteral";

void handleRoot() {
    server.send(200, "text/html", htmlPage);
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

    SPI.begin(5, 19, 27, 18);  // (SCK, MISO, MOSI, SS)
    LoRa.setPins(18, 14, 26);  // (SS, RST, DIO0)

    if (!LoRa.begin(868E6)) {
        Serial.println("LoRa Init Failed! Running without LoRa...");
    } else {
        Serial.println("LoRa Started!");
    }

    server.on("/", handleRoot);
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
