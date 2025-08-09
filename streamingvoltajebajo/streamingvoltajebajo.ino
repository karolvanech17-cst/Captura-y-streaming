#include "esp_camera.h"
#include "FS.h"
#include "SD_MMC.h"
#include <WiFi.h>
#include <time.h>
#include <WebServer.h>

// ================================
//  CONFIGURACIÓN ESP32-S3-CAM-N16R8-160
// ================================
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     21
#define SIOC_GPIO_NUM     14

#define Y2_GPIO_NUM       5
#define Y3_GPIO_NUM       3
#define Y4_GPIO_NUM       2
#define Y5_GPIO_NUM       4
#define Y6_GPIO_NUM       6
#define Y7_GPIO_NUM       8
#define Y8_GPIO_NUM       9
#define Y9_GPIO_NUM       11

#define VSYNC_GPIO_NUM    13
#define HREF_GPIO_NUM     12
#define PCLK_GPIO_NUM     7

// ================================
//  CONFIGURACIÓN WIFI Y TIEMPO
// ================================
const char* ssid = "KarolHotspot";
const char* password = "23456789";
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -5 * 3600;  // Colombia
const int daylightOffset_sec = 0;

// ================================
//  CONFIGURACIÓN CAPTURA AUTOMÁTICA
// ================================
unsigned long lastCapture = 0;
const unsigned long captureInterval = 10000; // 10 segundos (CONFIGURABLE)
bool streamingActive = false; // Flag para controlar streaming
unsigned long lastStreamFrame = 0;
bool sdCardAvailable = false; // Flag para controlar disponibilidad de SD

WebServer server(80);

// ================================
//  DIAGNÓSTICO AVANZADO DE SD
// ================================
void diagnosticSD() {
  Serial.println("\n === DIAGNÓSTICO COMPLETO DE TARJETA SD ===");
  
  // 1. Verificar detección física
  uint8_t cardType = SD_MMC.cardType();
  Serial.printf("--Tipo de tarjeta detectada: ");
  switch(cardType) {
    case CARD_NONE: 
      Serial.println("❌ NINGUNA"); 
      sdCardAvailable = false;
      return;
    case CARD_MMC: Serial.println("✅ MMC"); break;
    case CARD_SD: Serial.println("✅ SDSC"); break;
    case CARD_SDHC: Serial.println("✅ SDHC"); break;
    default: Serial.println("--Desconocida"); break;
  }
  
  // 2. Información de capacidad
  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  uint64_t totalBytes = SD_MMC.totalBytes() / (1024 * 1024);
  uint64_t usedBytes = SD_MMC.usedBytes() / (1024 * 1024);
  uint64_t freeBytes = (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024 * 1024);
  
  Serial.printf("--Capacidad total: %lluMB\n", cardSize);
  Serial.printf("--Espacio usado: %lluMB\n", usedBytes);
  Serial.printf("--Espacio libre: %lluMB\n", freeBytes);
  
  if (freeBytes < 5) {
    Serial.println("⚠️ ADVERTENCIA: Menos de 5MB libres!");
  }
  
  // 3. Prueba de escritura básica
  Serial.println("--Probando escritura básica...");
  File testFile = SD_MMC.open("/test_diagnóstico.txt", FILE_WRITE);
  if (testFile) {
    testFile.println("Test de diagnóstico");
    testFile.close();
    Serial.println("✅ Escritura básica: OK");
    
    // Verificar lectura
    testFile = SD_MMC.open("/test_diagnóstico.txt", FILE_READ);
    if (testFile) {
      String content = testFile.readString();
      testFile.close();
      SD_MMC.remove("/test_diagnóstico.txt");
      Serial.println("✅ Lectura básica: OK");
      sdCardAvailable = true;
    } else {
      Serial.println("❌ Error en lectura");
      sdCardAvailable = false;
    }
  } else {
    Serial.println("❌ Error en escritura básica");
    Serial.println("Posibles causas:");
    Serial.println("   - Tarjeta protegida contra escritura");
    Serial.println("   - Sistema de archivos corrupto");
    Serial.println("   - Tarjeta defectuosa");
    sdCardAvailable = false;
  }
  
  // 4. Listar archivos existentes
  Serial.println(" Archivos en raíz:");
  File root = SD_MMC.open("/");
  File file = root.openNextFile();
  int fileCount = 0;
  while (file && fileCount < 10) { // Mostrar máximo 10 archivos
    Serial.printf("    %s (%d bytes)\n", file.name(), file.size());
    file = root.openNextFile();
    fileCount++;
  }
  if (fileCount == 0) {
    Serial.println("    Directorio vacío");
  }
  
  Serial.println("==========================================");
}

// ================================
//  INICIALIZACIÓN SD CARD MEJORADA
// ================================
void initMicroSDCard() {
  Serial.println(" Inicializando tarjeta SD...");
  sdCardAvailable = false;
  
  // Reiniciar completamente el bus SD
  SD_MMC.end();
  delay(2000); // Aumentado el delay
  
  // Configuración de pines específica para ESP32-S3
  Serial.println(" Configurando pines SD para ESP32-S3...");
  
  // Intentar diferentes configuraciones
  bool mounted = false;
  
  // Configuración 1: Modo 1-bit con pines estándar
  Serial.println(" Intento 1: Modo 1-bit estándar...");
  if (SD_MMC.begin("/sdcard", true)) {
    mounted = true;
    Serial.println("✅ Montado en modo 1-bit estándar");
  }
  
  // Configuración 2: Modo 1-bit sin path específico
  if (!mounted) {
    Serial.println(" Intento 2: Modo 1-bit sin path...");
    SD_MMC.end();
    delay(1000);
    if (SD_MMC.begin()) {
      mounted = true;
      Serial.println("✅ Montado sin path específico");
    }
  }
  
  // Configuración 3: Con pines manuales
  if (!mounted) {
    Serial.println(" Intento 3: Configuración manual de pines...");
    SD_MMC.end();
    delay(1000);
    SD_MMC.setPins(42, 39, 41); // CLK, CMD, D0 para ESP32-S3
    if (SD_MMC.begin("/sdcard", true)) {
      mounted = true;
      Serial.println("✅ Montado con pines manuales");
    }
  }
  
  // Configuración 4: Forzar reinicio completo
  if (!mounted) {
    Serial.println(" Intento 4: Reinicio completo del sistema SD...");
    SD_MMC.end();
    delay(3000);
    
    // Reinicio más agresivo
    esp_restart(); // En casos extremos, reiniciar completamente
  }
  
  if (!mounted) {
    Serial.println("❌ FALLO COMPLETO EN TODOS LOS INTENTOS");
    Serial.println(" Verificaciones recomendadas:");
    Serial.println("   1. Tarjeta SD bien insertada");
    Serial.println("   2. Tarjeta formateada en FAT32");
    Serial.println("   3. Tarjeta clase 10 o superior");
    Serial.println("   4. Contactos limpios");
    Serial.println("   5. Probar con otra tarjeta");
    Serial.println("  Sistema continuará sin SD");
    return;
  }
  
  // Diagnóstico completo
  diagnosticSD();
}

// ================================
//  CAPTURA Y GUARDADO MEJORADO
// ================================
void takeSavePhoto() {
  // Verificación múltiple de SD
  if (!sdCardAvailable) {
    Serial.println("❌ SD no disponible - reintentando inicialización...");
    initMicroSDCard();
    if (!sdCardAvailable) {
      Serial.println("❌ SD sigue sin funcionar - saltando captura");
      return;
    }
  }
  
  // Verificación adicional en tiempo real
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("❌ Tarjeta SD desconectada durante operación");
    sdCardAvailable = false;
    return;
  }
  
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Fallo al capturar imagen");
    return;
  }

  struct tm timeinfo;
  char filename[64];
  char timeStr[32];
  
  if (!getLocalTime(&timeinfo)) {
    Serial.println("❌ Sin sincronización NTP - usando contador");
    static int photoCounter = 1;
    sprintf(filename, "/pic_%04d.jpg", photoCounter++);
    strcpy(timeStr, "Sin timestamp");
  } else {
    // Formato mejorado para evitar caracteres problemáticos
    strftime(filename, sizeof(filename), "/pic_%Y%m%d_%H%M%S.jpg", &timeinfo);
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  }
  
  Serial.printf("-- Captura automática - %s\n", timeStr);
  Serial.printf("-- Guardando: %s\n", filename);

  // Verificar espacio libre antes de escribir
  uint64_t freeBytes = SD_MMC.totalBytes() - SD_MMC.usedBytes();
  if (freeBytes < (fb->len * 2)) { // Margen de seguridad
    Serial.println("❌ Espacio insuficiente en SD");
    esp_camera_fb_return(fb);
    return;
  }

  fs::FS &fs = SD_MMC;
  File file = fs.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println("❌ No se pudo crear archivo");
    Serial.println(" Ejecutando diagnóstico...");
    diagnosticSD();
    
    // Intentar crear directorio si no existe
    if (!SD_MMC.exists("/")) {
      Serial.println(" Creando directorio raíz...");
      SD_MMC.mkdir("/");
    }
    
    // Segundo intento
    file = fs.open(filename, FILE_WRITE);
    if (!file) {
      Serial.println("❌ Segundo intento fallido - SD posiblemente corrupta");
      sdCardAvailable = false;
      esp_camera_fb_return(fb);
      return;
    }
  }
  
  size_t written = file.write(fb->buf, fb->len);
  file.flush(); // Forzar escritura inmediata
  file.close();
  
  if (written == fb->len) {
    Serial.printf("✅ Imagen guardada correctamente (%d bytes)\n", fb->len);
    
    // Verificación post-escritura
    File checkFile = SD_MMC.open(filename, FILE_READ);
    if (checkFile && checkFile.size() == fb->len) {
      checkFile.close();
      Serial.println("✅ Verificación post-escritura: OK");
    } else {
      Serial.println("⚠️ Advertencia: Verificación post-escritura falló");
      if (checkFile) checkFile.close();
    }
  } else {
    Serial.printf("❌ Escritura incompleta: %d de %d bytes\n", written, fb->len);
    sdCardAvailable = false; // Marcar SD como problemática
  }

  esp_camera_fb_return(fb);
}

// ================================
//  CONFIGURACIÓN DE CÁMARA
// ================================
void configCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    Serial.println("✅ PSRAM detectada - Configuración optimizada");
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    Serial.println("❌ NO se detectó PSRAM");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Error al iniciar la cámara: 0x%x\n", err);
    while (true) delay(1000);
  }

  Serial.println("✅ Cámara inicializada correctamente");
}

// ================================
//  CONEXIÓN WIFI
// ================================
void connectWiFi() {
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  Serial.print("--Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Conectado a WiFi");
  Serial.print("--Dirección IP: ");
  Serial.println(WiFi.localIP());
}

// ================================
//  SINCRONIZACIÓN DE TIEMPO
// ================================
void syncTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  Serial.println("--Sincronizando hora con NTP...");
  
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 10) {
    delay(1000);
    attempts++;
    Serial.print(".");
  }
  
  if (attempts >= 10) {
    Serial.println("\n Error obteniendo la hora NTP - continuando sin sincronización");
    return;
  }
  Serial.println("\n Hora sincronizada correctamente");
}

// ================================
//  MANEJO DEL STREAMING
// ================================
void handle_jpg_stream() {
  WiFiClient client = server.client();
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);

  Serial.println("--Cliente conectado al streaming");
  streamingActive = true;

  while (client.connected()) {
    unsigned long now = millis();
    
    if (now - lastCapture > captureInterval) {
      Serial.println("--Momento de captura automática - pausando streaming");
      takeSavePhoto();
      lastCapture = now;
      Serial.println("--Reanudando streaming...");
    }
    
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("❌ Fallo al capturar imagen para streaming");
      delay(100);
      continue;
    }

    response = "--frame\r\n";
    response += "Content-Type: image/jpeg\r\n\r\n";
    server.sendContent(response);
    server.sendContent((const char*)fb->buf, fb->len);
    server.sendContent("\r\n");

    esp_camera_fb_return(fb);
    lastStreamFrame = now;
    
    delay(100);
  }
  
  streamingActive = false;
  Serial.println("---Cliente desconectado del streaming");
}

// ================================
//  SERVIDOR WEB MEJORADO
// ================================
void startWebServer() {
  server.on("/", []() {
    String html = "<html><head><title>ESP32-S3 Deteccion de Semillas</title>";
    html += "<meta http-equiv='refresh' content='30'></head>"; // Auto-refresh cada 30s
    html += "<body style='font-family: Arial; text-align: center; background: #f0f0f0;'>";
    html += "<h1>===Sistema de Deteccion de Semillas===</h1>";
    html += "<h2> ESP32-S3-CAM Streaming + Auto Captura</h2>";
    html += "<div style='margin: 20px;'>";
    html += "<img src='/stream' style='max-width: 90%; border: 3px solid #333; border-radius: 10px;'>";
    html += "</div>";
    html += "<div style='background: white; margin: 20px; padding: 15px; border-radius: 10px;'>";
    html += "<h3> Estado del Sistema</h3>";
    html += "<p><strong>Intervalo de captura:</strong> " + String(captureInterval/1000) + " segundos</p>";
    html += "<p><strong>Tarjeta SD:</strong> " + String(sdCardAvailable ? "✅ Disponible" : "❌ Error") + "</p>";
    html += "<p><strong>Streaming:</strong> " + String(streamingActive ? "✅ Activo" : "❌ Inactivo") + "</p>";
    html += "<p><strong>Memoria PSRAM:</strong> " + String(psramFound() ? "✅ Detectada" : "❌ No detectada") + "</p>";
    html += "</div>";
    html += "<div style='margin: 20px;'>";
    html += "<button onclick='location.reload()' style='padding: 10px 20px; font-size: 16px;'> Actualizar Estado</button>";
    html += "</div>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });
  
  server.on("/stream", HTTP_GET, handle_jpg_stream);
  
  // Endpoint de diagnóstico
  server.on("/diagnostico", []() {
    String html = "<html><head><title>Diagnóstico SD</title></head>";
    html += "<body style='font-family: Arial; background: #f0f0f0; padding: 20px;'>";
    html += "<h1> Diagnóstico de Tarjeta SD</h1>";
    html += "<pre style='background: black; color: green; padding: 20px; border-radius: 10px;'>";
    
    uint8_t cardType = SD_MMC.cardType();
    html += "Estado: " + String(cardType == CARD_NONE ? "❌ No detectada" : "✅ Detectada") + "\n";
    
    if (cardType != CARD_NONE) {
      html += "Tipo: ";
      switch(cardType) {
        case CARD_MMC: html += "MMC"; break;
        case CARD_SD: html += "SDSC"; break;
        case CARD_SDHC: html += "SDHC"; break;
        default: html += "Desconocido"; break;
      }
      html += "\n";
      
      uint64_t totalMB = SD_MMC.totalBytes() / (1024 * 1024);
      uint64_t usedMB = SD_MMC.usedBytes() / (1024 * 1024);
      uint64_t freeMB = totalMB - usedMB;
      
      html += "Capacidad total: " + String((unsigned long)totalMB) + " MB\n";
      html += "Espacio usado: " + String((unsigned long)usedMB) + " MB\n";
      html += "Espacio libre: " + String((unsigned long)freeMB) + " MB\n";
    }
    
    html += "</pre>";
    html += "<button onclick='history.back()' style='padding: 10px 20px; font-size: 16px;'>← Volver</button>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });
  
  server.begin();
  Serial.println("✅ Servidor web iniciado en puerto 80");
  Serial.println(" Accede desde tu navegador a la IP mostrada arriba");
  Serial.println(" Diagnóstico disponible en: http://IP/diagnostico");
}

// ================================
//  CONFIGURACIÓN INICIAL
// ================================
void setup() {
  Serial.begin(115200);
  delay(2000); // Tiempo adicional para estabilización
  
  Serial.println(" === SISTEMA DE DETECCIÓN DE SEMILLAS ===");
  Serial.println(" ESP32-S3-CAM con Streaming + Auto Captura");
  Serial.println("Versión mejorada con diagnóstico avanzado");
  Serial.println("============================================");
  
  configCamera();
  connectWiFi();
  syncTime();
  initMicroSDCard(); // Inicialización mejorada con diagnóstico
  startWebServer();
  
  Serial.println("============================================");
  Serial.printf("✅ Sistema iniciado %s\n", sdCardAvailable ? "completamente" : "sin SD");
  Serial.printf("--Captura automática cada %d segundos\n", captureInterval/1000);
  Serial.println("Streaming disponible las 24 horas");
  if (sdCardAvailable) {
    Serial.println("--Guardado automático: ACTIVO");
  } else {
    Serial.println("Guardado automático: DESHABILITADO (problema con SD)");
  }
  Serial.println("============================================");
}

// ================================
// BUCLE PRINCIPAL MEJORADO
// ================================
void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (now - lastCapture > captureInterval) {
    // Solo intentar captura si SD está disponible o cada 10 intentos
    static int failedAttempts = 0;
    if (sdCardAvailable || failedAttempts >= 10) {
      takeSavePhoto();
      if (!sdCardAvailable) {
        failedAttempts++;
        if (failedAttempts >= 10) {
          Serial.println("Reintentando inicialización de SD cada 10 fallos...");
          failedAttempts = 0;
        }
      } else {
        failedAttempts = 0;
      }
    }
    lastCapture = now;
  }
  
  delay(10);
}