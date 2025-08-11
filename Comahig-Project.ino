  #include <Keypad.h>
  #include <Wire.h>
  #include <Arduino.h>
  #include <SPI.h>
  #include <MFRC522.h>
  #include <Wire.h>
  #include <LiquidCrystal_I2C.h>
  #include <ESP32Servo.h>

  LiquidCrystal_I2C lcd(0x27, 16, 2);

  // RFID module connections
  #define SS_PIN 27
  #define RST_PIN 26
  MFRC522 rfid(SS_PIN, RST_PIN);
  MFRC522::MIFARE_Key key;

  // RFID block number
  const byte blockNumber = 4;

  // Keypad setup
  const byte ROWS = 4;
  const byte COLS = 4;
  char keys[ROWS][COLS] = {
    { '1', '2', '3', 'A' },
    { '4', '5', '6', 'B' },
    { '7', '8', '9', 'C' },
    { '*', '0', '#', 'D' }
  };
  byte rowPins[ROWS] = { 2, 0, 4, 16 };
  byte colPins[COLS] = { 17, 5, 18, 19 };
  Keypad kpad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

  // Password input and visibility
  String x = "";
  bool hide = true;

  #define BUZZER 32
  #define SERVO 23
  const int ledIndicator[] = { 1, 3 }, proxSensor[] = { 25, 33 };

  String actualData = "";

  Servo motor;

  void setup() {
    Serial.begin(9600);

    for (int indicate : ledIndicator) {
      pinMode(indicate, OUTPUT);
    }
    for (int prox : proxSensor) {
      pinMode(prox, INPUT);
    }
    pinMode(BUZZER, OUTPUT);

    motor.attach(SERVO);

    digitalWrite(ledIndicator[0], HIGH);
    digitalWrite(ledIndicator[1], LOW);

    // Initialize LCD
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Idle Mode...");

    // Initialize SPI and RFID
    SPI.begin(14, 12, 13, SS_PIN);  // SCK, MISO, MOSI, SS
    rfid.PCD_Init();
    for (byte i = 0; i < 6; i++) {
      key.keyByte[i] = 0xFF;  // Default key
    }
  }

  bool ret = false;

  void loop() {
    // Wait until the proximity sensor is triggered
    while (digitalRead(proxSensor[1]) == 1) {
      delay(15);
      return;
    }

    // LCD displays welcome message
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Welcome! Please");
    lcd.setCursor(0, 1);
    lcd.print("tap your Card");

    // Authenticate card
    cardAuthChecker();

    // Confirm card reading
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Reading Card");
    lcd.setCursor(0, 1);
    lcd.print("Successful");

    resetReader();

    // Execute card agenda
    cardAgenda();

    // Change indicator LED states
    digitalWrite(ledIndicator[0], LOW);
    digitalWrite(ledIndicator[1], HIGH);

    motor.write(90);  // Servo opens at 90 degrees
    digitalWrite(BUZZER, HIGH);
    delay(1000);
    digitalWrite(BUZZER, LOW);

    // Wait until the other proximity sensor is triggered
    while (digitalRead(proxSensor[0]) != 0) {
      delay(15);
    }

    // Close access and reset indicators
    delay(3000);     // Allow time for servo to fully close
    motor.write(0);  // Servo closes at 0 degrees

    digitalWrite(ledIndicator[0], HIGH);
    digitalWrite(ledIndicator[1], LOW);

    resetReader();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Idle Mode...");
  }


  void cardAuthChecker() {
    while (true) {

      // Attempt to read card serial
      if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
        delay(15);
        continue;
      }

      Serial.println(F("Card detected, authenticating..."));

      // Authenticate with the specified block
      if (rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockNumber, &key, &(rfid.uid)) != MFRC522::STATUS_OK) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Auth Failed!");
        Serial.println(F("Authentication failed."));
        delay(1000);
        continue;  // Retry if authentication fails
      }

      Serial.println(F("Authentication successful!"));
      break;  // Exit loop if authentication succeeds
    }
  }

  void cardAgenda() {

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("(C) Load Card.");
    lcd.setCursor(0, 1);
    lcd.print("(D) Pay Pass.");

    while (true) {
      char k = kpad.getKey();
      if (k) {
        if (k == 'C') {
          loadCard();
          break;
        } else if (k == 'D') {
          paypassCard();
          break;
        } else {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Invalid Input!");
          lcd.setCursor(0, 1);
          lcd.print("Try again...");
          delay(750);
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("(C) Load Card.");
          lcd.setCursor(0, 1);
          lcd.print("(D) Pay Pass.");
          continue;
        }
      }
    }
  }

  void loadCard() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Load Value:");
    lcd.setCursor(0, 1);

    String inputValue = "";  // Stores the user input

    while (true) {
      char key = kpad.getKey();  // Get keypress from the keypad
      if (key) {
        switch (key) {
          case '*':  // Clear the input
            inputValue = "";
            lcd.setCursor(0, 1);
            lcd.print("                ");  // Clear the second row
            lcd.setCursor(0, 1);
            break;

          case '#':  // Confirm and write the value
            if (inputValue.length() > 0) {
              int valueToLoad = inputValue.toInt();  // Convert string to integer
              if (valueToLoad > 0) {
                byte data[16] = { 0 };  // Prepare a buffer to write
                String valueStr = String(valueToLoad);
                valueStr.getBytes(data, 16);

                resetReader();      // Reset the reader
                delay(100);         // Stabilize the reader
                cardAuthChecker();  // Authenticate the card

                // Read the current balance from the card
                byte buffer[18] = { 0 };
                byte size = sizeof(buffer);
                String balanceStr = "";
                int cardVal = 0;

                if (rfid.MIFARE_Read(blockNumber, buffer, &size) == MFRC522::STATUS_OK) {
                  Serial.print(F("Current Balance: "));
                  for (byte i = 0; i < 16; i++) {
                    if (buffer[i] >= 32 && buffer[i] <= 126) {
                      Serial.print((char)buffer[i]);
                      balanceStr += (char)buffer[i];
                    }
                  }
                  balanceStr.trim();
                  cardVal = balanceStr.toInt();  // Convert to integer
                  Serial.println();
                  Serial.print(F("Parsed Balance: "));
                  Serial.println(cardVal);

                  // Add the user input to the current balance
                  long updatedValue = (long)cardVal + (long)valueToLoad;  // Use long to prevent overflow

                  // Convert the updated value back to bytes
                  String updatedValueStr = String(updatedValue);
                  memset(data, 0, 16);  // Clear the buffer
                  updatedValueStr.getBytes(data, 16);

                  // Write the updated value to the card
                  if (writeBlock(blockNumber, data)) {
                    lcd.clear();
                    lcd.setCursor(0, 0);
                    lcd.print("Load Success!");
                    lcd.setCursor(0, 1);
                    lcd.print("New Value: " + String(updatedValue));
                    Serial.println(F("Value written successfully!"));
                    delay(1000);

                    delay(100);     // Stabilize the reader
                    resetReader();  // Reset the reader
                    cardAgenda();

                    return;  // Exit the function
                  } else {
                    lcd.clear();
                    lcd.setCursor(0, 0);
                    lcd.print("Write Failed!");
                    Serial.println(F("Failed to write data."));
                    digitalWrite(BUZZER, HIGH);
                    delay(500);
                    digitalWrite(BUZZER, LOW);
                    delay(1000);
                  }
                } else {
                  lcd.clear();
                  lcd.setCursor(0, 0);
                  lcd.print("Read Failed!");
                  digitalWrite(BUZZER, HIGH);
                  delay(500);
                  digitalWrite(BUZZER, LOW);
                  delay(1000);
                  continue;  // Retry if reading fails
                }
              } else {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Invalid Value!");
                digitalWrite(BUZZER, HIGH);
                delay(500);
                digitalWrite(BUZZER, LOW);
                delay(1000);
                inputValue = "";  // Reset input
              }
            } else {
              lcd.clear();
              lcd.setCursor(0, 0);
              lcd.print("Empty Input!");
              digitalWrite(BUZZER, HIGH);
              delay(500);
              digitalWrite(BUZZER, LOW);
              delay(2000);
            }
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Load Value:");
            lcd.setCursor(0, 1);
            break;

          default:  // Append numbers to the input
            if (key >= '0' && key <= '9') {
              if (inputValue.length() < 16) {  // Ensure input fits in the block
                inputValue += key;
                lcd.print(key);  // Display the input
              } else {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Max Limit 16!");
                delay(2000);
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Load Value:");
                lcd.setCursor(0, 1);
                lcd.print(inputValue);  // Redisplay current input
              }
            }
            break;
        }
      }
    }
  }

  void paypassCard() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Tap your Card...");
    delay(1000);

    const int price = 50;  // Deduction amount
    int cardVal = 0, newVal = 0;

    while (true) {
      resetReader();
      delay(100);  // Allow reader to stabilize

      cardAuthChecker();  // Authenticate card

      // Read the block to get the current balance
      byte buffer[18];
      byte size = sizeof(buffer);
      String balanceStr = "";
      cardVal = 0;

      if (rfid.MIFARE_Read(blockNumber, buffer, &size) == MFRC522::STATUS_OK) {
        Serial.print(F("Current Balance: "));
        for (byte i = 0; i < 16; i++) {
          if (buffer[i] >= 32 && buffer[i] <= 126) {
            Serial.print((char)buffer[i]);
            balanceStr += (char)buffer[i];
          }
        }
        balanceStr.trim();
        cardVal = balanceStr.toInt();  // Convert to integer
        Serial.println();
        Serial.print(F("Parsed Balance: "));
        Serial.println(cardVal);
      } else {
        Serial.println(F("Failed to read block."));
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Read Failed!");
        delay(1000);
        continue;  // Retry if reading fails
      }

      // Check if balance is sufficient
      if (cardVal < price) {
        lcd.clear();
        digitalWrite(BUZZER, HIGH);
        delay(500);
        digitalWrite(BUZZER, LOW);
        lcd.setCursor(0, 0);
        lcd.print("Insufficient");
        lcd.setCursor(0, 1);
        lcd.print("Balance!");
        Serial.println(F("Insufficient balance."));
        delay(2000);
        loadCard();
        break;  // Exit if insufficient balance
      }

      // Deduct the price
      newVal = cardVal - price;

      // Prepare data to write back (16 bytes, padded with zeros)
      byte n_data[16] = { 0 };
      String newBalanceStr = String(newVal);
      newBalanceStr.getBytes(n_data, 16);

      // Write updated balance back to the card
      if (writeBlock(blockNumber, n_data)) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Payment Success!");
        Serial.println(F("Data written successfully!"));
        delay(1000);
        Serial.print(F("New Balance: "));
        Serial.println(newVal);
        break;  // Exit loop after successful write
      } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Write Failed!");
        Serial.println(F("Failed to write data. Retry..."));
        delay(1000);
      }
    }

    // Display final balance and transaction details
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Old Bal: " + String(cardVal));
    lcd.setCursor(0, 1);
    lcd.print("Charged: " + String(price));
    delay(2000);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("New Balance:");
    lcd.setCursor(0, 1);
    lcd.print(newVal);

    resetReader();  // Final reset
  }

  bool writeBlock(byte blockNumber, byte *data) {
    MFRC522::StatusCode status = rfid.MIFARE_Write(blockNumber, data, 16);  // Write 16 bytes
    if (status == MFRC522::STATUS_OK) {
      return true;
    } else {
      Serial.print(F("Write error: "));
      Serial.println(rfid.GetStatusCodeName(status));
      return false;
    }
  }

  void updateDisplay() {
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    if (hide) {
      for (int i = 0; i < x.length(); i++) lcd.print("*");
    } else {
      lcd.print(x);
    }
  }

  void resetReader() {
    rfid.PICC_HaltA();       // Halt communication with the current card
    rfid.PCD_StopCrypto1();  // Stop encryption on the current card
    rfid.PCD_Init();         // Reinitialize the RFID module to prepare it for a new card
  }

  void handleKeyInput(char key) {

    // Handle key inputs for password
    if (key == '*') {
      x = "";
      lcd.setCursor(0, 1);
      lcd.print("                ");
    } else if (key == 'A') {
      hide = false;
      updateDisplay();
    } else if (key == 'B') {
      hide = true;
      updateDisplay();
    } else if (key == '#') {
      lcd.clear();
      lcd.print(x.equals(actualData) ? "ACCESS GRANTED" : "ACCESS DENIED");
      x = "";
    } else {
      x += key;
      updateDisplay();
    }
  }

  void resetSystem() {

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Place your card");
    lcd.setCursor(0, 1);
    lcd.print("on the scanner");
  }

  int getData() {
    while (true) {
      Serial.println(F("Enter an integer (16 characters max):"));
      while (!Serial.available())
        ;  // Wait for user input

      String input = Serial.readStringUntil('\n');
      input.trim();  // Remove leading/trailing whitespace

      // Check validity: numeric and <= 16 characters
      if (input.length() <= 16 && input.toInt() != 0 || input == "0") {
        int x = input.toInt();  // Convert to integer

        Serial.print(F("Data accepted: "));
        Serial.println(x);
        return x;
      }

      Serial.println(input.length() > 16 ? F("Error: Input exceeds 16 characters. Try again.") : F("Error: Input must be an integer. Try again."));
    }
  }

  int readBlock(byte blockNumber) {
    byte buffer[18];
    byte size = sizeof(buffer);

    // Read the block data
    if (rfid.MIFARE_Read(blockNumber, buffer, &size) != MFRC522::STATUS_OK) {
      Serial.print(F("Read error: "));
      Serial.println(rfid.GetStatusCodeName(rfid.MIFARE_Read(blockNumber, buffer, &size)));
      return -1;  // Indicate failure
    }

    // Convert to string
    String dataStr = "";
    for (byte i = 0; i < 16; i++) {
      if (buffer[i] != 0) {  // Ignore null bytes
        dataStr += char(buffer[i]);
      }
    }

    // Convert string to integer
    int value = dataStr.toInt();
    Serial.print(F("Block Data: "));
    Serial.println(value);
    return value;
  }
