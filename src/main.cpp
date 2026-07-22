#include <Arduino.h>
#include <ld2410.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h> 
#include <ArduinoJson.h>

#define MONITOR_SERIAL Serial
#define RADAR_SERIAL Serial1
#define RADAR_RX_PIN 16
#define RADAR_TX_PIN 17

ld2410 radar;
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81); 

// Globale variabler til JSON-synkronisering
int isTargetDetected = 0; 
int targetDistanceCm = 0;
int targetAngleDeg = 0; 
String targetType = "NONE";

// SOFTWARE FILTER VARIABLER
const int FILTER_SAMPLES = 10;
int distanceHistory[FILTER_SAMPLES];
int angleHistory[FILTER_SAMPLES]; 
int filterIndex = 0;

void resetFilter() {
    for (int i = 0; i < FILTER_SAMPLES; i++) {
        distanceHistory[i] = 0;
        angleHistory[i] = 0;
    }
    filterIndex = 0;
}

int getFilteredValue(int newSample, int* historyArray) {
    historyArray[filterIndex] = newSample;
    long sum = 0;
    for (int i = 0; i < FILTER_SAMPLES; i++) {
        sum += historyArray[i];
    }
    return sum / FILTER_SAMPLES;
}

// UAV / Militær-inspireret 2D Radar Interface HTML
const String htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>UAV 2D RADAR INTERFACE</title>
    <style>
        body { background-color: #050505; color: #00ff66; font-family: 'Courier New', Courier, monospace; text-align: center; padding: 10px; margin: 0; overflow-x: hidden; }
        .header { color: #00ff66; font-size: 18px; letter-spacing: 2px; text-shadow: 0 0 8px rgba(0,255,102,0.6); margin-top: 10px; font-weight: bold; }
        .sub-header { color: #00aa44; font-size: 11px; margin-bottom: 15px; }
        
        .radar-screen { position: relative; width: 320px; height: 320px; margin: 0 auto; background: radial-gradient(circle, #091a0c 0%, #030804 70%, #000 100%); border: 3px solid #00ff66; border-radius: 50%; box-shadow: 0 0 20px rgba(0,255,102,0.3); overflow: hidden; }

        .grid-line-h { position: absolute; top: 50%; left: 0; width: 100%; height: 1px; background-color: rgba(0,255,102,0.25); }
        .grid-line-v { position: absolute; left: 50%; top: 0; width: 1px; height: 100%; background-color: rgba(0,255,102,0.25); }
        
        .angle-line-left { position: absolute; left: 50%; top: 50%; width: 160px; height: 1px; background-color: rgba(0,255,102,0.15); transform-origin: left; transform: rotate(-150deg); }
        .angle-line-right { position: absolute; left: 50%; top: 50%; width: 160px; height: 1px; background-color: rgba(0,255,102,0.15); transform-origin: left; transform: rotate(-30deg); }

        .zone-circle-1 { position: absolute; top: 12.5%; left: 12.5%; width: 75%; height: 75%; border: 1px dashed rgba(0,255,102,0.2); border-radius: 50%; }
        .zone-circle-2 { position: absolute; top: 25%; left: 25%; width: 50%; height: 50%; border: 1px solid rgba(0,255,102,0.3); border-radius: 50%; }
        .zone-circle-3 { position: absolute; top: 37.5%; left: 37.5%; width: 25%; height: 25%; border: 1px dashed rgba(0,255,102,0.2); border-radius: 50%; }
        
        .sweep-line { position: absolute; top: 0; left: 0; width: 100%; height: 100%; background: conic-gradient(from 0deg, rgba(0,255,102,0.15) 0deg, rgba(0,255,102,0) 90deg); border-radius: 50%; animation: radar-sweep 4s linear infinite; pointer-events: none; }
        @keyframes radar-sweep { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }

        .human-dot { position: absolute; width: 14px; height: 14px; background-color: #ff0033; border-radius: 50%; top: 50%; left: 50%; transform: translate(-50%, -50%); box-shadow: 0 0 15px #ff0033, 0 0 30px #ff0033; display: none; transition: all 0.2s ease-out; z-index: 10; }

        .telemetry-panel { max-width: 320px; margin: 20px auto; padding: 12px; background-color: rgba(0,20,5,0.6); border: 1px solid #00aa44; border-radius: 5px; text-align: left; font-size: 12px; box-shadow: inset 0 0 10px rgba(0,255,102,0.1); }
        .status-active { color: #ff0033; font-weight: bold; text-shadow: 0 0 5px rgba(255,0,51,0.5); }
        .status-clear { color: #00ff66; font-weight: bold; }
    </style>
</head>
<body>
    <div class="header">SYSTEM STATUS: OPERATIONAL</div>
    <div class="sub-header">UAV LIVE 2D COUNTER-MEASURE SENSOR PROTOCOL</div>

    <div class="radar-screen">
        <div class="grid-line-h"></div>
        <div class="grid-line-v"></div>
        <div class="angle-line-left"></div>
        <div class="angle-line-right"></div>
        <div class="zone-circle-1"></div>
        <div class="zone-circle-2"></div>
        <div class="zone-circle-3"></div>
        <div class="sweep-line"></div>
        <div id="humanDot" class="human-dot"></div>
    </div>

    <div class="telemetry-panel">
        <div>&gt; THREAT LEVEL: <span id="txtThreat">CLEAR</span></div>
        <div>&gt; TARGET TYPE: <span id="txtType">NONE</span></div>
        <div>&gt; RANGE TO TARGET: <span id="txtRange">0</span> cm</div>
        <div>&gt; TARGET ANGLE: <span id="txtAngle">0</span>&deg;</div>
        <div>&gt; SYSTEM LOCK: <span id="txtLock">NO CONNECT</span></div>
    </div>

    <script>
        const MAX_RADAR_RANGE_CM = 600;
        let ws;

        function initWebSocket() {
            ws = new WebSocket('ws://' + window.location.hostname + ':81/');
            
            ws.onopen = function() {
                document.getElementById('txtLock').innerText = "CONNECTED (LIVE WS)";
                document.getElementById('txtLock').style.color = "#00ff66";
            };
            
            ws.onclose = function() {
                document.getElementById('txtLock').innerText = "DISCONNECTED";
                document.getElementById('txtLock').style.color = "#ff0033";
                setTimeout(initWebSocket, 2000); 
            };
            
            ws.onmessage = function(event) {
                const data = JSON.parse(event.data);
                const dot = document.getElementById('humanDot');
                
                if (data.detected === 1) {
                    document.getElementById('txtThreat').innerHTML = "<span class='status-active'>HOSTILE DETECTED</span>";
                    document.getElementById('txtType').innerText = data.type;
                    document.getElementById('txtRange').innerText = data.distance;
                    document.getElementById('txtAngle').innerText = data.angle;

                    let distancePercent = Math.min(data.distance / MAX_RADAR_RANGE_CM, 1.0);
                    let radiusPx = distancePercent * 150; 
                    
                    let angleRad = (data.angle - 90) * (Math.PI / 180);
                    
                    let targetX = 160 + radiusPx * Math.cos(angleRad);
                    let targetY = 160 + radiusPx * Math.sin(angleRad);
                    
                    dot.style.left = targetX + "px";
                    dot.style.top = targetY + "px";
                    dot.style.display = "block";
                } else {
                    document.getElementById('txtThreat').innerHTML = "<span class='status-clear'>NO HOSTILES DETECTED</span>";
                    document.getElementById('txtType').innerText = "NONE";
                    document.getElementById('txtRange').innerText = "0";
                    document.getElementById('txtAngle').innerText = "0";
                    dot.style.display = "none";
                }
            };
        }

        window.onload = initWebSocket;
    </script>
</body>
</html>
)rawliteral";

void handleRoot() {
    server.send(200, "text/html", htmlPage);
}

void setup() {
    MONITOR_SERIAL.begin(115200);
    RADAR_SERIAL.begin(256000, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
    delay(500);

    resetFilter();

    MONITOR_SERIAL.println("\n--- UAV 2D RADAR INITIALIZATION ---");

    WiFi.softAP("UAV-RADAR-NET", "12345678"); 
    
    MONITOR_SERIAL.println("\nWi-Fi Netværk Oprettet!");
    MONITOR_SERIAL.print("Åbn browseren på: http://");
    MONITOR_SERIAL.println(WiFi.softAPIP()); 

    server.on("/", handleRoot);
    server.begin();

    webSocket.begin();

    if (radar.begin(RADAR_SERIAL)) {
        MONITOR_SERIAL.println("Radar Sensor: READY");
    } else {
        MONITOR_SERIAL.println("Radar Sensor: HARDWARE ERROR");
    }
}

unsigned long lastBroadcast = 0;
int simulatedAngle = 0;
int angleDirection = 1;

void loop() {
    radar.read();
    server.handleClient();
    webSocket.loop(); 

    if (radar.isConnected()) {
        if (radar.presenceDetected()) {
            isTargetDetected = 1;
            int rawDistance = 0;
            
            if (radar.movingTargetDetected()) {
                targetType = "MOVING";
                rawDistance = radar.movingTargetDistance();
                
                // Genererer en flot organisk 2D-bevægelse, når du går foran radaren
                simulatedAngle += angleDirection * random(1, 4);
                if (simulatedAngle > 25) angleDirection = -1;
                if (simulatedAngle < -25) angleDirection = 1;
            } else if (radar.stationaryTargetDetected()) {
                targetType = "STATIONARY";
                rawDistance = radar.stationaryTargetDistance();
                
                // Trækker langsomt prikken ind til midteraksen (0 grader), når du står helt stille
                if (simulatedAngle > 0) simulatedAngle--;
                if (simulatedAngle < 0) simulatedAngle++;
            }
            
            targetDistanceCm = getFilteredValue(rawDistance, distanceHistory);
            targetAngleDeg = getFilteredValue(simulatedAngle, angleHistory);
            
        } else {
            isTargetDetected = 0;
            targetType = "NONE";
            targetDistanceCm = 0;
            targetAngleDeg = 0;
            simulatedAngle = 0;
            resetFilter();
        }
    }

if (millis() - lastBroadcast > 50) {
    lastBroadcast = millis();

    JsonDocument doc;

    doc["detected"] = isTargetDetected;
    doc["distance"] = targetDistanceCm;
    doc["angle"] = targetAngleDeg;
    doc["type"] = targetType;

    String jsonResponse;
    serializeJson(doc, jsonResponse);

    webSocket.broadcastTXT(jsonResponse);
}

filterIndex = (filterIndex + 1) % FILTER_SAMPLES;
delay(5);
}