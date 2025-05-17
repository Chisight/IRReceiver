## IRReceiver Library Documentation

This library allows your Arduino (atmega328), STM32, ESP8266, or ESP32 to receive, decode, and identify infrared (IR) remote control signals from common protocols like NEC, Sony SIRC12, and JVC using an interrupt driven receiver.

It's purpose is to be dramatically smaller than LIRC and IRremote based solutions.  The library avoids the bit reversed codes and included preambles that often makes LIRC data so difficult to read.

### Quick Start Example

```cpp
/**
 * @file IRReceiverExample.ino
 * @brief Example sketch for using the IRReceiver library.
 *
 * Captures, analyzes, and decodes IR signals from remotes like JVC, Sony, NEC.
 * Prints the decoded information to the Serial Monitor.
 */

#include <IRReceiver.h> // Include the library

// --- Set configuration of TL1838 pin ---
const int IR_RECEIVER_PIN = 4; // GPIO pin connected to your IR receiver module's data pin

// Create an instance of the IRReceiver library
IRReceiver irReceiver;

void setup() {
  Serial.begin(115200);
  // Wait for Serial Monitor to connect (optional)
  // while (!Serial && millis() < 3000);

  if (irReceiver.begin(IR_RECEIVER_PIN)) {
    Serial.println("IRReceiver Ready. Point a remote and press a button!");
  } else {
    Serial.println("Error: IR Receiver initialization failed!");
    Serial.println("Check that the specified pin supports interrupts.");
    while(1) delay(1000); // Halt on error
  }
}

void loop() {
  // Check if a complete IR code has been received and decoded
  if (irReceiver.isCode()) {
    DecodedIR result = irReceiver.getCode(); // Get the decoded data

    // Check if the decoding was successful (valid brand and command)
    if (result.brand != UNKNOWN && result.command != -1) {
        Serial.print("Brand: ");
        Serial.print(irReceiver.brandToString(result.brand));

        Serial.print(", Command: ");
        Serial.print(result.command);
        Serial.print(" (0x");
        Serial.print(result.command, HEX);
        Serial.print(")");

        Serial.print(", Address: ");
        Serial.print(result.address);
        Serial.print(" (0x");
        Serial.print(result.address, HEX);
        Serial.print(")");

        Serial.print(", Button: ");
        Serial.println(irReceiver.getButtonName(result.brand, result.command));
    } else {
        // This might happen if the signal was too noisy or didn't match a known protocol well enough.
        Serial.println("Received IR burst, but could not decode a valid known signal.");
    }
  }
  delay(10); // Small delay to allow other processes
}
```

### Public Functions

The `IRReceiver` class provides the following public methods:

---

#### `IRReceiver()`
*   **Description:** Constructor for the IRReceiver class. You typically create a global instance of this class.
*   **Usage:**
    ```cpp
    IRReceiver irReceiver; // Creates an instance
    ```

---

#### `bool begin(int pin)`
*   **Description:** Initializes the IR receiver library to listen for IR signals on the specified GPIO pin. This function configures the pin, sets up internal timers and buffers, and attaches the necessary\
interrupt to detect IR signal transitions. It automatically enables IR receiving.
*   **Parameters:**
    *   `pin`: The ESP32 GPIO pin number connected to the data output of your IR receiver module (e.g., TL1838, VS1838B). This pin must support interrupts.
*   **Returns:**
    *   `true`: If initialization was successful and the interrupt was attached.
    *   `false`: If initialization failed (e.g., the specified pin does not support interrupts).
*   **Usage:**
    ```cpp
    const int IR_PIN = 4;
    if (irReceiver.begin(IR_PIN)) {
      Serial.println("IR Receiver started successfully!");
    } else {
      Serial.println("Failed to start IR Receiver.");
    }
    ```

---

#### `bool isCode()`
*   **Description:** Checks if a complete IR signal has been received, processed, and successfully decoded into a known protocol and command. This function should be called repeatedly in your main `loop()`.\
When it returns `true`, it means a decoded IR code is available and can be retrieved using `getCode()`. This function handles the internal state, ensuring that each distinct IR transmission (including\
repeats if they are part of the same logical button press) results in `true` being returned appropriately.
*   **Returns:**
    *   `true`: A valid, decoded IR code is ready to be read with `getCode()`.
    *   `false`: No new decoded IR code is available.
*   **Usage:**
    ```cpp
    if (irReceiver.isCode()) {
      // A new IR code is available
      DecodedIR data = irReceiver.getCode();
      // Process 'data'
    }
    ```

---

#### `DecodedIR getCode()`
*   **Description:** Retrieves the most recently decoded IR signal data. This function should only be called after `isCode()` has returned `true`. Calling `getCode()` consumes the available code, meaning\
subsequent calls to `isCode()` will return `false` until a new IR signal is fully received and decoded.
*   **Returns:**
    *   `DecodedIR`: A struct containing the decoded information. The `DecodedIR` struct has the following members:
        *   `RemoteBrand brand`: An enum indicating the detected protocol (e.g., `SONY`, `NEC`, `JVC`, or `UNKNOWN`).
        *   `int command`: The decoded command code (e.g., button code). Value is `-1` if not successfully decoded.
        *   `int address`: The decoded address/device code. Value is `-1` if not applicable or not successfully decoded.
*   **`RemoteBrand` Enum:**
    *   `UNKNOWN`
    *   `JVC`
    *   `SONY` (also used for Sceptre remotes which use Sony protocol)
    *   `NEC`
*   **Usage:**
    ```cpp
    if (irReceiver.isCode()) {
      DecodedIR result = irReceiver.getCode();
      if (result.brand != UNKNOWN && result.command != -1) {
        // Use result.brand, result.command, result.address
      }
    }
    ```

---

#### `const char* brandToString(RemoteBrand brand) const`
*   **Description:** Converts a `RemoteBrand` enum value into a human-readable string (e.g., `SONY` enum becomes `"SONY"` string). Useful for printing or logging.
*   **Parameters:**
    *   `brand`: The `RemoteBrand` enum value to convert.
*   **Returns:**
    *   `const char*`: A pointer to a string literal representing the brand name. Returns `"UNKNOWN"` if the brand enum is not recognized.
*   **Usage:**
    ```cpp
    Serial.print("Brand: ");
    Serial.println(irReceiver.brandToString(result.brand));
    ```

---

#### `const char* getButtonName(RemoteBrand brand, int commandCode) const`
*   **Description:** Attempts to find a human-readable name for a given command code based on the detected remote brand. The library contains a predefined list of common button names for Sceptre/Sony, JVC,\
and NEC remotes.
*   **Parameters:**
    *   `brand`: The `RemoteBrand` of the remote.
    *   `commandCode`: The integer command code received from the remote.
*   **Returns:**
    *   `const char*`: A pointer to a string literal for the button name if a known match is found (e.g., `"sceptrePower"`, `"jvcVol+"`).
    *   If the button code is not recognized for the given brand, it returns a string in the format `"BRAND_CMD_DDD"` (e.g., `"SONY_CMD_123"`) or `"CMD_DDD"` if the brand is also unknown, where DDD is the\
decimal command code.
*   **Note:** The internal buffer for unknown command strings is static. This means the returned pointer is valid until the next call to `getButtonName` that results in an unknown code. This is generally\
fine for direct printing.
*   **Usage:**
    ```cpp
    Serial.print("Button: ");
    Serial.println(irReceiver.getButtonName(result.brand, result.command));
    ```

---

#### `void enable()`
*   **Description:** Enables IR signal receiving by attaching the hardware interrupt to the pin specified in `begin()`. It also resets internal state variables to ensure a clean start for the next capture\
session. `begin()` calls this automatically. You would typically use this to resume receiving after a call to `disable()`.
*   **Usage:**
    ```cpp
    irReceiver.enable(); // Start listening for IR signals again
    ```

---

#### `void disable()`
*   **Description:** Disables IR signal receiving by detaching the hardware interrupt. No new IR signals will be processed while disabled. This is useful if you need to perform other timing-sensitive\
operations or if you are transmitting IR signals from the same ESP32 and want to prevent self-reception. Any partially captured signal data is cleared.
*   **Usage:**
    ```cpp
    irReceiver.disable(); // Stop listening for IR signals
    // ... perform other tasks, or transmit IR ...
    // irReceiver.enable(); // To resume
    ```

---

