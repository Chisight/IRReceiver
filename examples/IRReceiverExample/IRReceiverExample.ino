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
