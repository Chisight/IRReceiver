#ifndef IR_RECEIVER_H
#define IR_RECEIVER_H

#include <Arduino.h>
#include "IRButtonDefs.h"
#include "IRReceiverDebug.h" 

// --- Configuration Constants
#define IR_LIB_MAX_TRANSITIONS 300
#define IR_LIB_IDLE_TIMEOUT_MS 100
#define IR_LIB_MAX_DECODED_SEGMENTS 10

// Struct to hold pulse and space pairs
struct PulseSpacePair {
    int pulse;
    int space;
};

// Enum for remote brands
#ifndef REMOTEBRAND_ENUM
#define REMOTEBRAND_ENUM
enum RemoteBrand {
  UNKNOWN = 0, 
  JVC,
  SONY,
  NEC,
  NUM_BRANDS
};
#endif

// Struct to hold decoded command and address
struct DecodedIR {
  RemoteBrand brand = UNKNOWN;
  int command = -1;
  int address = -1;
};

class IRReceiver {
public:
    IRReceiver();

    bool begin(int pin);
    bool isCode();
    DecodedIR getCode();
    const char* brandToString(RemoteBrand brand) const;
    const char* getButtonName(RemoteBrand brand, int commandCode) const;
    void enable();
    void disable();

private:
    // Protocol Definitions
    static constexpr int JVC_PREAMBLE_PULSE = 8400; /* ... all other protocol constants ... */
    static constexpr int JVC_PREAMBLE_SPACE = 4200;
    static constexpr int JVC_BIT_PULSE = 526;
    static constexpr int JVC_ZERO_SPACE = 526;
    static constexpr int JVC_ONE_SPACE = 1574;
    static constexpr int JVC_REPEAT_DELAY = 22000;
    static constexpr int JVC_REPEAT_PREAMBLE_PULSE = 0;
    static constexpr int JVC_REPEAT_PREAMBLE_SPACE = 0;
    static constexpr int JVC_INITIAL_BITS = 17;
    static constexpr int JVC_REPEAT_BITS = 16;

    static constexpr int SONY_PREAMBLE_PULSE = 2400;
    static constexpr int SONY_PREAMBLE_SPACE = 600;
    static constexpr int SONY_ZERO_PULSE = 600;
    static constexpr int SONY_ONE_PULSE = 1200;
    static constexpr int SONY_BIT_SPACE = 600;
    static constexpr int SONY_REPEAT_DELAY = 25000;
    static constexpr int SONY_REPEAT_PREAMBLE_PULSE = 2400;
    static constexpr int SONY_REPEAT_PREAMBLE_SPACE = 600;
    static constexpr int SONY_INITIAL_BITS = 13;
    static constexpr int SONY_REPEAT_BITS = 13;

    static constexpr int NEC_PREAMBLE_PULSE = 9000;
    static constexpr int NEC_PREAMBLE_SPACE = 4500;
    static constexpr int NEC_BIT_PULSE = 563;
    static constexpr int NEC_ZERO_SPACE = 563;
    static constexpr int NEC_ONE_SPACE = 563 * 3;
    static constexpr int NEC_REPEAT_DELAY = 42000;
    static constexpr int NEC_REPEAT_PREAMBLE_PULSE = 8900;
    static constexpr int NEC_REPEAT_PREAMBLE_SPACE = 2200;
    static constexpr int NEC_INITIAL_BITS = 33; 
    static constexpr int NEC_REPEAT_BITS = 1;   

    // Analysis Configuration
    static constexpr int TIMING_TOLERANCE = 200;
    static constexpr float PERCENTAGE_TOLERANCE = 0.10f; 
    static constexpr int MIN_REPEAT_GAP = 10000;

    // Raw Capture
    static IRReceiver* s_instance; 
    int m_irPin;
    volatile uint32_t m_rawTransitions[IR_LIB_MAX_TRANSITIONS];
    volatile int m_rawTransitionIndex;
    volatile unsigned long m_lastTransitionMillis;
    volatile int m_lastPinState;
    bool m_rawBurstCopied; 

    // Analysis & Decoding Data
    PulseSpacePair m_pulseSpacePairs[IR_LIB_MAX_TRANSITIONS / 2];
    int m_pulseSpacePairCount;
    int m_brandScores[NUM_BRANDS];
    DecodedIR m_decodedSegments[IR_LIB_MAX_DECODED_SEGMENTS]; 
    int m_decodedSegmentCount;
    DecodedIR m_finalResultCode; 
    bool m_codeResultIsReady;    
    bool m_isInterruptAttached; 

    // ISR Methods (NO IRAM_ATTR in declarations)
    static void staticHandleIrInterrupt_priv(); 
    void handleIrInterrupt_priv(); 

    // Internal Processing Methods
    void _processRawTransitionsToPairs(const uint32_t localTransitions[], int capturedCount);
    void _analyzeAndDecodeBurst();

    // Helper Methods
    bool isWithinTolerance(int captured, int expected, int tolerance) const;
    bool isWithinPercentageTolerance(int captured, int expected, float tolerance_percent) const;
    RemoteBrand matchPreamble(int pulse, int space, bool isRepeatPreamble) const;

    // Scoring Functions
    int scoreSonySIRC12(const PulseSpacePair pairs[], int count) const;
    int scoreJVC(const PulseSpacePair pairs[], int count) const;
    int scoreNEC(const PulseSpacePair pairs[], int count) const;

    // Decoding Function
    DecodedIR decodeWinningSegment(RemoteBrand brand, const PulseSpacePair pairs[], int count) const;
    struct DecodedNECInternal { DecodedIR base; bool checksumValid = false; };
    DecodedNECInternal decodeNECData(const PulseSpacePair dataPairs[], int dataPairCount) const; 

    // Winner Determination
    void determineWinner(const DecodedIR segments[], int count, const bool segmentChecksums[]);

    // Constants for Time/Direction Packing
    static constexpr uint32_t TIME_VALUE_MASK = 0x7FFFFFFF;
    static constexpr uint32_t DIRECTION_FLAG_H_TO_L = 0x80000000;
};

#endif // IR_RECEIVER_H
