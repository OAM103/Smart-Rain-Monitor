#include <WiFi.h>          // Библиотека для работы с WiFi
#include <WiFiClientSecure.h> // Библиотека для защищенного соединения (HTTPS)
#include <NTPClient.h>       // Библиотека для получения времени с NTP сервера
#include <WiFiUdp.h>         // Библиотека для UDP (используется NTPClient)

// --- Настройки пользователя --- 
const char* ssid = "00000";         // Имя вашей WiFi сети (SSID)
const char* password = "0000000"; // Пароль вашей WiFi сети
const int rainSensorPin = 4;             // Пин, к которому подключен датчик дождя
const char* telegramBotToken = "0000000000:JJF9JHaNDx4z8Px1-Tk9FtrhubYDj2xgI8g"; // Токен вашего Telegram бота
const char* chatId = "00000000";       // ID чата, куда бот будет отправлять сообщения
const int rainThresholdTime = 5000;      // Время в миллисекундах, в течение которого датчик должен быть мокрым/сухим, чтобы подтвердить начало/окончание дождя
const int ntpUpdateInterval = 60000;     // Интервал обновления времени с NTP сервера (в миллисекундах)
const int timeOffsetSeconds = 10800;   // Смещение времени для Москвы (UTC+3), в секундах
const bool sendNightNotifications = false; // Отправлять уведомления ночью? (true - да, false - нет)

// --- Глобальные переменные --- 
enum RainState { RAIN_STOPPED, RAIN_STARTING, RAINING, RAIN_STOPPING }; // Перечисление для состояний дождя
RainState currentRainState = RAIN_STOPPED; // Текущее состояние дождя (изначально - дождь не идет)
unsigned long rainDetectedTime = 0;     // Время, когда впервые обнаружена влага на датчике
unsigned long rainStoppedTime = 0;      // Время, когда датчик впервые перестал обнаруживать влагу
unsigned long rainStartTime = 0;        // Время начала дождя (фактическое, после подтверждения)
WiFiUDP ntpUDP;                           // Объект для UDP (нужен для NTPClient)
NTPClient timeClient(ntpUDP);             // Объект для работы с NTP сервером
unsigned long lastNTPUpdateTime = 0;      // Время последнего обновления времени с NTP сервера

void setup() {
  Serial.begin(115200); // Инициализация Serial Monitor для отладки
  pinMode(rainSensorPin, INPUT_PULLUP); // Настройка пина датчика дождя: INPUT_PULLUP означает, что к пину подключен внутренний подтягивающий резистор.  
  connectWiFi();           // Подключение к WiFi
  setupNTP();              // Настройка NTP клиента
}


void loop() {
  updateNTP();        // Обновление времени с NTP сервера (если необходимо)
  handleRainSensor(); // Обработка данных с датчика дождя
  delay(100);         // Небольшая задержка для стабильности
}

// Функция подключения к WiFi
void connectWiFi() {
  WiFi.begin(ssid, password); // Подключение к WiFi сети с указанным SSID и паролем
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { // Ожидание подключения
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString()); // Вывод IP адреса
}

// Функция настройки NTP клиента
void setupNTP() {
  timeClient.begin();              // Инициализация NTP клиента
  timeClient.setTimeOffset(timeOffsetSeconds); // Установка смещения времени для вашего часового пояса
  updateNTP();                     // Получаем время сразу после подключения
}

// Функция обновления времени с NTP сервера
void updateNTP() {
  if (millis() - lastNTPUpdateTime > ntpUpdateInterval) { // Проверка, пора ли обновлять время
    if (WiFi.status() == WL_CONNECTED) { // Проверка, что WiFi подключен
      timeClient.update();               // Обновление времени
      lastNTPUpdateTime = millis();        // Запоминаем время последнего обновления
      Serial.println("NTP time updated");
    } else {
      Serial.println("WiFi disconnected, cannot update NTP"); // Вывод сообщения об ошибке, если WiFi не подключен
    }
  }
}

// Функция обработки данных с датчика дождя
void handleRainSensor() {
  int rainValue = digitalRead(rainSensorPin); // Чтение значения с датчика дождя (HIGH - сухо, LOW - мокро)
  unsigned long currentTime = millis();     // Текущее время в миллисекундах

  switch (currentRainState) { // Обработка в зависимости от текущего состояния дождя
    case RAIN_STOPPED: // Дождь не идет
      if (rainValue == LOW) { // Если датчик обнаружил влагу
        currentRainState = RAIN_STARTING; // Переход в состояние "начало дождя"
        rainDetectedTime = currentTime;    // Запоминаем время обнаружения влаги
        Serial.println("Potential rain detected...");
      }
      break;

    case RAIN_STARTING: // Начало дождя (ждем подтверждения)
      if (rainValue == LOW && currentTime - rainDetectedTime >= rainThresholdTime) { // Если датчик остается мокрым в течение заданного времени
        currentRainState = RAINING;       // Переход в состояние "дождь идет"
        rainStartTime = currentTime;       // Запоминаем время начала дождя
        String startTime = formatTime(timeClient.getEpochTime()); // Форматируем время начала дождя
        String message = "Внимание! Начался дождь в " + startTime; // Формируем сообщение для Telegram
        sendTelegramMessage(message);        // Отправляем сообщение в Telegram
        Serial.println("Rain started at " + startTime);
      } else if (rainValue == HIGH) { // Если датчик высох до истечения времени подтверждения
        currentRainState = RAIN_STOPPED; // Возврат в состояние "дождь не идет"
        Serial.println("False alarm - rain stopped.");
      }
      break;

    case RAINING: // Дождь идет
      if (rainValue == HIGH) { // Если датчик высох
        currentRainState = RAIN_STOPPING; // Переход в состояние "окончание дождя"
        rainStoppedTime = currentTime;     // Запоминаем время, когда датчик высох
        Serial.println("Rain stopping...");
      }
      break;

    case RAIN_STOPPING: // Окончание дождя (ждем подтверждения)
      if (rainValue == HIGH && currentTime - rainStoppedTime >= rainThresholdTime) { // Если датчик остается сухим в течение заданного времени
        currentRainState = RAIN_STOPPED;      // Переход в состояние "дождь не идет"
        Serial.println("Rain stopped.");
        if (sendNightNotifications || !isNight()) { // Проверка, нужно ли отправлять уведомление ночью
          String message = "Внимание! Дождь закончился."; // Формируем сообщение для Telegram
          sendTelegramMessage(message);         // Отправляем сообщение в Telegram
        } else {
          Serial.println("Rain stopped, but it's night. No notification sent.");
        }
      } else if (rainValue == LOW) { // Если датчик снова намок до истечения времени подтверждения
        currentRainState = RAINING;  // Возврат в состояние "дождь идет"
        Serial.println("Rain restarted.");
      }
      break;
  }
}

// Функция форматирования времени из epoch time (Unix timestamp)
String formatTime(time_t epochTime) {
  struct tm *ptm = gmtime((time_t *)&epochTime); // Преобразование epoch time в структуру tm (UTC)
  char buf[30];                                // Буфер для хранения отформатированной строки
  strftime(buf, sizeof(buf), "%H:%M:%S %d/%m/%Y", ptm); // Форматирование времени в строку
  return String(buf);                          // Возвращаем строку
}

// Функция определения, ночь сейчас или день
bool isNight() {
  int hour = timeClient.getHours(); // Получаем текущий час
  return (hour >= 22 || hour < 6); // Считаем, что ночь с 22:00 до 06:00
}

// Функция отправки сообщения в Telegram
void sendTelegramMessage(String message) {
  WiFiClientSecure client; // Создаем объект для защищенного соединения
  client.setInsecure();      // Отключает проверку сертификата (НЕБЕЗОПАСНО для production!)
  String url = "/bot" + String(telegramBotToken) + "/sendMessage?chat_id=" + String(chatId) + "&text=" + urlEncode(message); // Формируем URL для отправки сообщения
  Serial.println("Sending: " + url); // Выводим URL в Serial Monitor для отладки

  if (!client.connect("api.telegram.org", 443)) { // Подключаемся к Telegram API
    Serial.println("Telegram connection failed!"); // Выводим сообщение об ошибке, если не удалось подключиться
    return;                                     // Выходим из функции
  }

  String request = "GET " + url + " HTTP/1.1\r\nHost: api.telegram.org\r\nConnection: close\r\n\r\n"; // Формируем HTTP запрос
  client.print(request); // Отправляем запрос на сервер

  while (client.connected() && client.available() == 0) { // Ожидаем ответа от сервера
    delay(10);
  }
  while (client.available()) { // Читаем ответ от сервера
    Serial.println(client.readStringUntil('\n')); // Выводим ответ в Serial Monitor для отладки
  }
  client.stop();             // Закрываем соединение
  Serial.println("Telegram message sent!"); // Выводим сообщение об успешной отправке
}

// Функция URL-кодирования строки (для отправки специальных символов в Telegram)
String urlEncode(String str) {
  String encodedString = "";
  for (int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encodedString += c;
    } else if (c == ' ') {
      encodedString += "%20";
    } else {
      char buf[4];
      sprintf(buf, "%%%02X", c);
      encodedString += buf;
    }
  }
  return encodedString;
}