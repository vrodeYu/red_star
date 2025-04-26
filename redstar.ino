// Визначення пінів для кнопок, світлодіодів та сенсорів
#define ButtonRed      15    // Пін червоної кнопки
#define ButtonWhite    16    // Пін білої кнопки
#define LedRed         31    // Пін червоного світлодіода
#define LedWhite       30    // Пін білого світлодіода
#define NTC            32    // Пін термістора NTC
#define BatteryCheck   11    // Пін для перевірки рівня батареї
#define bWAK_P1_5_LO   0x20  // Константа для налаштування режиму пробудження

// Змінні для відстеження часу натискання кнопок
uint32_t ButtonTimerR = 0;   // Таймер для червоної кнопки
uint32_t ButtonTimerW = 0;   // Таймер для білої кнопки

// Стан кнопок (натиснута/відпущена)
bool ButtonStateR = false;   // Поточний стан червоної кнопки
bool ButtonStateW = false;   // Поточний стан білої кнопки

// Прапорці для відстеження стану кнопок
bool ButtonFlagR = false;    // Прапорець для червоної кнопки
bool ButtonFlagW = false;    // Прапорець для білої кнопки
bool VoltFlag     = true;    // Прапорець для перевірки напруги

// Напрямок зміни яскравості (збільшення/зменшення)
bool pwm_dirR   = true;      // Напрямок зміни для червоного світлодіода (true = збільшення)
bool pwm_dirW   = true;      // Напрямок зміни для білого світлодіода (true = збільшення)
uint8_t pwm_dutyR = 0;       // Поточне значення ШІМ для червоного світлодіода
uint8_t pwm_dutyW = 0;       // Поточне значення ШІМ для білого світлодіода

// Загальні таймери
unsigned long btn_timer   = 0;   // Таймер для опитування стану кнопок
unsigned long sleep_timer = 0;   // Таймер для режиму сну
int f               = 0;         // Лічильник послідовних коротких натискань

// Змінні для неблокуючого мерехтіння при індикації рівня заряду
bool   voltBlinkActive    = false;               // Чи активне мерехтіння
int    voltBlinkBlinks    = 0;                   // Скільки разів треба блимнути
int    voltBlinkCount     = 0;                   // Скільки разів вже блимнуло
bool   voltBlinkLEDState  = false;               // Поточний стан світлодіода при блиманні
unsigned long voltBlinkLastMillis = 0;           // Час останньої зміни стану при блиманні
const unsigned long voltBlinkInterval = 200;     // Інтервал між змінами стану при блиманні (мс)

/**
 * Функція корекції яскравості для більш природного сприйняття людським оком
 * Застосовує квадратичну апроксимацію для компенсації нелінійного сприйняття яскравості
 */
byte CRT(byte val) {
  return ((long)val * val + 255) >> 8;
}

/**
 * Функція вимірювання напруги батареї
 * Робить 5 вимірювань та повертає середнє значення
 * @return Напруга батареї у вольтах
 */
float VoltageBattery() {
  float sum = 0;
  for (int i = 0; i < 5; i++) {
    int sv = analogRead(14);              // Отримуємо опорну напругу
    float v = 3.3 / sv;                   // Обчислюємо коефіцієнт
    v *= analogRead(BatteryCheck);        // Множимо на значення з піна перевірки батареї
    sum += v;                             // Додаємо до суми
    delay(30);                            // Затримка для стабілізації
  }
  return sum / 5;                         // Повертаємо середнє значення
}

/**
 * Ініціалізує індикацію рівня заряду батареї блиманням
 * Кількість блимань залежить від рівня заряду
 * @param number Значення напруги батареї
 */
void VoltBlinkStart(float number) {
  int segment = 0;
  // Визначаємо сегмент на основі виміряної напруги
  if      (number < 3.50)       segment = 0;  // Дуже низький заряд
  else if (number < 3.70)       segment = 1;  // Низький заряд
  else if (number < 3.87)       segment = 4;  // Середній заряд
  else                          segment = 5;  // Високий заряд

  // Встановлюємо кількість блимань відповідно до сегмента
  switch (segment) {
    case 0: voltBlinkBlinks = 1; break;  // ~25%
    case 1: voltBlinkBlinks = 2; break;  // ~50%
    case 4: voltBlinkBlinks = 3; break;  // ~75%
    case 5: voltBlinkBlinks = 4; break;  // ~100%
    default: voltBlinkBlinks = 1;
  }

  // Скидаємо змінні для початку нової індикації
  voltBlinkCount       = 0;
  voltBlinkLEDState    = false;
  voltBlinkLastMillis  = millis();
  voltBlinkActive      = true;

  // Вимикаємо обидва світлодіоди перед початком мерехтіння
  digitalWrite(LedWhite, LOW);
  digitalWrite(LedRed,   LOW);
}

/**
 * Оновлює стан мерехтіння світлодіода для індикації рівня батареї
 * Викликається в основному циклі для неблокуючої роботи
 */
void VoltBlinkUpdate() {
  if (!voltBlinkActive) return;                                // Якщо мерехтіння не активне, виходимо
  unsigned long now = millis();
  if (now - voltBlinkLastMillis < voltBlinkInterval) return;   // Якщо не минув потрібний час, виходимо
  voltBlinkLastMillis = now;                                   // Оновлюємо час останньої зміни

  voltBlinkLEDState = !voltBlinkLEDState;                      // Змінюємо стан світлодіода
  analogWrite(LedRed, voltBlinkLEDState ? 3 : 0);              // Встановлюємо яскравість 3 або 0
  delay(100);                                                  // Невелика затримка для стабільності

  // Кожен раз, коли LED згасає, рахуємо один блік
  if (!voltBlinkLEDState) {
    voltBlinkCount++;                                          // Збільшуємо лічильник блимань
    if (voltBlinkCount >= voltBlinkBlinks) {                   // Якщо досягли потрібної кількості
      voltBlinkActive = false;                                 // Завершуємо мерехтіння
      digitalWrite(LedRed, LOW);                               // Вимикаємо світлодіод
    }
  }
}

/**
 * Налаштування пристрою при запуску
 */
void setup() {
  // Налаштування пінів як входів
  pinMode(BatteryCheck, INPUT);
  pinMode(NTC, INPUT);
  pinMode(14, INPUT);
  
  // Налаштування пінів кнопок з внутрішнім підтягуючим резистором
  P1_DIR_PU |= (1 << ButtonRed) | (1 << ButtonWhite);
  
  // Налаштування пінів світлодіодів як виходів
  P3_DIR_PU &= ~((1 << LedRed) | (1 << LedWhite));
  
  // Налаштування ШІМ
  PWM_CK_SE = 15;
  
  // Налаштування режиму безпеки та пробудження
  SAFE_MOD = 0x55;
  SAFE_MOD = 0xaa;
  WAKE_CTRL = WAKE_CTRL | bWAK_P1_5_LO | bWAK_RST_HI;
  SAFE_MOD = 0x00;
}

/**
 * Основний цикл програми
 */
void loop() {
  // Оновлення стану кнопок кожні 64 мс
  if (millis() - btn_timer >= 64) {
    btn_timer    = millis();
    ButtonStateR = !digitalRead(ButtonRed);    // Інвертуємо, оскільки кнопки підключені до GND
    ButtonStateW = !digitalRead(ButtonWhite);
  }

  // Фіксація початку натискання червоної кнопки
  if (ButtonStateR && !ButtonFlagR) {
    ButtonFlagR    = true;
    ButtonTimerR   = millis();
  }
  
  // Фіксація початку натискання білої кнопки
  if (ButtonStateW && !ButtonFlagW) {
    ButtonFlagW    = true;
    ButtonTimerW   = millis();
  }

  // Утримання червоної кнопки - поступова зміна яскравості червоного світлодіода
  if (ButtonStateR && ButtonFlagR &&
      millis() - ButtonTimerR >= 200) {  // Якщо утримується більше 200 мс
    pwm_dutyR = constrain(pwm_dutyR + (pwm_dirR ? 1 : -1), 0, 255);  // Змінюємо яскравість
    delay(5);                            // Затримка для плавної зміни
    f        = 0;                        // Скидаємо лічильник коротких натискань
    pwm_dutyW= 0;                        // Вимикаємо білий світлодіод при регулюванні червоного
    pwm_dirW = true;                     // Скидаємо напрямок для білого
    VoltFlag = true;                     // Дозволяємо перевірку напруги
  }

  // Утримання білої кнопки - поступова зміна яскравості білого світлодіода
  if (ButtonStateW && ButtonFlagW &&
      millis() - ButtonTimerW >= 200) {  // Якщо утримується більше 200 мс
    pwm_dutyW = constrain(pwm_dutyW + (pwm_dirW ? 1 : -1), 0, 255);  // Змінюємо яскравість
    delay(5);                            // Затримка для плавної зміни
    f        = 0;                        // Скидаємо лічильник коротких натискань
    sleep_timer = millis();              // Оновлюємо таймер режиму сну
    pwm_dutyR= 0;                        // Вимикаємо червоний світлодіод при регулюванні білого
    pwm_dirR = true;                     // Скидаємо напрямок для червоного
    VoltFlag = true;                     // Дозволяємо перевірку напруги
  }

  // Відпускання червоної кнопки
  if (!ButtonStateR && ButtonFlagR) {
    ButtonFlagR = false;
    if (millis() - ButtonTimerR < 200) {  // Якщо це було коротке натискання (менше 200 мс)
      pwm_dutyR=0;                        // Вимикаємо обидва світлодіоди
      pwm_dutyW=0;
      f++;                                // Збільшуємо лічильник коротких натискань
      sleep_timer = millis();             // Оновлюємо таймер режиму сну
      delay(5);
    } else {                              // Якщо це було довге натискання
      pwm_dirR = !pwm_dirR;               // Змінюємо напрямок зміни яскравості
    }
  }

  // Відпускання білої кнопки
  if (!ButtonStateW && ButtonFlagW) {
    ButtonFlagW = false;
    if (millis() - ButtonTimerW < 200) {  // Якщо це було коротке натискання (менше 200 мс)
      pwm_dutyW=0;                        // Вимикаємо обидва світлодіоди
      pwm_dutyR=0;
      f++;                                // Збільшуємо лічильник коротких натискань
      sleep_timer = millis();             // Оновлюємо таймер режиму сну
      delay(5);
    } else {                              // Якщо це було довге натискання
      pwm_dirW = !pwm_dirW;               // Змінюємо напрямок зміни яскравості
    }
  }

  // Запуск індикації напруги після трьох коротких натискань
  if (f == 3 && VoltFlag) {
    float Vcc = VoltageBattery();         // Вимірюємо напругу батареї
    VoltBlinkStart(Vcc);                  // Запускаємо індикацію блиманням
    VoltFlag = false;                     // Блокуємо повторну індикацію
  }

  // Оновлюємо стан мерехтіння в неблокуючому режимі
  VoltBlinkUpdate();

  // Застосовуємо корекцію яскравості та встановлюємо значення ШІМ
  analogWrite(LedRed,   CRT(pwm_dutyR));
  analogWrite(LedWhite, CRT(pwm_dutyW));

  // Перехід у сплячий режим, якщо обидва світлодіоди вимкнені протягом 10 секунд
  if (pwm_dutyW == 0 && pwm_dutyR == 0 &&
      millis() - sleep_timer >= 10000) {
    sleep_timer = millis();               // Оновлюємо таймер сну
    PCON = PCON | PD;                     // Встановлюємо біт режиму зниженого енергоспоживання
  }
}
