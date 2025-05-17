#include "IRReceiver.h"

// Initialize the static instance pointer
IRReceiver* IRReceiver::s_instance = nullptr;

IRReceiver::IRReceiver() :
    m_irPin(-1),
    m_rawTransitionIndex(0),
    m_lastTransitionMillis(0),
    m_lastPinState(HIGH),
    m_rawBurstCopied(false),
    m_pulseSpacePairCount(0),
    m_decodedSegmentCount(0),
    m_codeResultIsReady(false),
    m_isInterruptAttached(false) // Initialize as not attached
{
    for (int i = 0; i < NUM_BRANDS; ++i) {
        m_brandScores[i] = 0;
    }
}

// --- ISR and Raw Capture ---
void IRAM_ATTR IRReceiver::staticHandleIrInterrupt_priv() { // IRAM_ATTR on definition
    if (s_instance) {
        s_instance->handleIrInterrupt_priv();
    }
}

void IRAM_ATTR IRReceiver::handleIrInterrupt_priv() { // IRAM_ATTR on definition
    uint32_t currentTimeMicros = micros();
    int currentState = digitalRead(m_irPin);

    if (currentState != m_lastPinState && m_rawTransitionIndex < IR_LIB_MAX_TRANSITIONS) {
        uint32_t timeValue = currentTimeMicros & TIME_VALUE_MASK;
        if (currentState == LOW && m_lastPinState == HIGH) {
            timeValue |= DIRECTION_FLAG_H_TO_L;
        }
        m_rawTransitions[m_rawTransitionIndex] = timeValue;
        m_rawTransitionIndex = m_rawTransitionIndex + 1;
        m_lastPinState = currentState;
        m_lastTransitionMillis = millis();
    }
}

bool IRReceiver::begin(int pin) {
    s_instance = this; // Assign instance for ISR
    
    if (m_isInterruptAttached && m_irPin != -1) { // If already begun and attached, detach first
        disable();
    }
    
    m_irPin = pin;
    pinMode(m_irPin, INPUT_PULLUP);
    
    enable(); // Call enable to attach interrupt and set initial states

    // The check for NOT_AN_INTERRUPT is now effectively inside enable()
    // begin() relies on enable() to set m_isInterruptAttached correctly.
    if (!m_isInterruptAttached && m_irPin != -1) { // If enable failed to attach
         Debug(DEBUG_GENERAL, "IRReceiver: begin() failed to enable interrupts for pin ", m_irPin, ".\n");
        return false;
    }
    
    Debug(DEBUG_GENERAL, "IRReceiver initialized on pin: ", m_irPin, "\n");
    return true;
}

void IRReceiver::enable() {
    if (m_irPin == -1) {
        Debug(DEBUG_GENERAL, "IRReceiver: Cannot enable, pin not set. Call begin() first.\n");
        return;
    }
    if (m_isInterruptAttached) {
        Debug(DEBUG_GENERAL, "IRReceiver: Interrupt already enabled on pin ", m_irPin, ".\n");
        // Optionally, reset state anyway if desired, or just return
        // For robust behavior, reset state to ensure clean next capture:
    }

    // Reset state variables for a clean capture session
    m_lastPinState = digitalRead(m_irPin); // Important to get current state before attach
    m_lastTransitionMillis = millis();
    m_codeResultIsReady = false;
    m_rawBurstCopied = false;
    m_rawTransitionIndex = 0;
    // m_pulseSpacePairCount = 0; // Not strictly needed here, _processRawTransitionsToPairs resets it
    // m_decodedSegmentCount = 0; // Not strictly needed, _analyzeAndDecodeBurst resets it

    // Attach the interrupt
    if (digitalPinToInterrupt(m_irPin) != NOT_AN_INTERRUPT) {
        attachInterrupt(digitalPinToInterrupt(m_irPin), staticHandleIrInterrupt_priv, CHANGE);
        m_isInterruptAttached = true;
        Debug(DEBUG_GENERAL, "IRReceiver: Interrupts ENABLED on pin ", m_irPin, ".\n");
    } else {
        Debug(DEBUG_GENERAL, "IRReceiver: Error enabling interrupt. Pin ", m_irPin, " may not support interrupts.\n");
        m_isInterruptAttached = false;
    }
}

void IRReceiver::disable() {
    if (m_irPin == -1 || !m_isInterruptAttached) {
        Debug(DEBUG_GENERAL, "IRReceiver: Cannot disable, not initialized or interrupt not attached to pin ", m_irPin, ".\n");
        return;
    }

    if (digitalPinToInterrupt(m_irPin) != NOT_AN_INTERRUPT) {
        detachInterrupt(digitalPinToInterrupt(m_irPin));
        m_isInterruptAttached = false;
        Debug(DEBUG_GENERAL, "IRReceiver: Interrupts DISABLED on pin ", m_irPin, ".\n");
    } else {
         // This case should ideally not be reached if m_isInterruptAttached was true
         Debug(DEBUG_GENERAL, "IRReceiver: Error disabling interrupt. Pin ", m_irPin, " may not support interrupts (or state error).\n");
         m_isInterruptAttached = false; // Ensure flag is false
    }
    // When disabling, you might want to clear any partially captured data
    // or pending flags to prevent processing stale data when re-enabled.
    m_rawTransitionIndex = 0;
    m_rawBurstCopied = false;
    m_codeResultIsReady = false;
}

bool IRReceiver::isCode() {
    if (!m_isInterruptAttached) { // If interrupts are not attached, no new codes can come
        if(m_codeResultIsReady) return true; // but an old one might be pending from before disable
        return false;
    }

    if (m_codeResultIsReady) {
        return true; 
    }

    if (m_rawTransitionIndex > 0 && (millis() - m_lastTransitionMillis > IR_LIB_IDLE_TIMEOUT_MS) && !m_rawBurstCopied) {
        cli();
        int capturedCount = m_rawTransitionIndex;
        uint32_t localTransitions[IR_LIB_MAX_TRANSITIONS]; 
        if (capturedCount > IR_LIB_MAX_TRANSITIONS) capturedCount = IR_LIB_MAX_TRANSITIONS;

        for (int i = 0; i < capturedCount; i++) {
            localTransitions[i] = m_rawTransitions[i];
        }
        m_rawTransitionIndex = 0; 
        sei();

        m_rawBurstCopied = true; 
        _processRawTransitionsToPairs(localTransitions, capturedCount);

        if (m_pulseSpacePairCount > 0) {
            Debug(DEBUG_BURST, "\n--- IR Signal Burst Detected (Library Internal) ---\n"); 
            Debug(DEBUG_BURST, "Number of pulse/space pairs extracted: ", m_pulseSpacePairCount, "\n");

#ifdef DEBUG_BURST 
            if((DEBUG & DEBUG_BURST) == DEBUG_BURST) { 
                Debug(DEBUG_BURST, "Pulse/Space Pairs (us):\n");
                for (int i = 0; i < m_pulseSpacePairCount; ++i) {
                    Debug(DEBUG_BURST, "  Pair ", i, ": Pulse=", m_pulseSpacePairs[i].pulse, ", Space=", (m_pulseSpacePairs[i].space == -1 ? "MISSING" : String(m_pulseSpacePairs[i].space).c_str()), "\n");
                }
            }
#endif
            _analyzeAndDecodeBurst(); 
        } else {
            Debug(DEBUG_BURST, "No pulse/space pairs extracted from burst.\n");
            m_rawBurstCopied = false; 
        }
        return m_codeResultIsReady;
    } else if (m_rawTransitionIndex == 0 && m_rawBurstCopied) {
        m_rawBurstCopied = false;
    }
    return false;
}

DecodedIR IRReceiver::getCode() {
    if (m_codeResultIsReady) {
        m_codeResultIsReady = false;
        // m_rawBurstCopied is reset when a new burst starts processing in isCode()
        // or if isCode() determines no pairs were extracted.
        return m_finalResultCode;
    }
    return DecodedIR(); 
}

// _processRawTransitionsToPairs - REMAINS THE SAME
void IRReceiver::_processRawTransitionsToPairs(const uint32_t localTransitions[], int capturedCount) {
    m_pulseSpacePairCount = 0; 

    if (capturedCount < 2) {
        Debug(DEBUG_RAW_TIMING, "Not enough transitions (", capturedCount, ") to process burst.\n");
        return;
    }
    uint32_t previousTimeVal = localTransitions[0] & TIME_VALUE_MASK;

#ifdef DEBUG_RAW_TIMING
    if((DEBUG & DEBUG_RAW_TIMING) == DEBUG_RAW_TIMING) { 
        Debug(DEBUG_RAW_TIMING, "\nRaw Transitions and Deltas:\n");
    }
#endif
    int currentPulse = -1;

    for (int i = 1; i < capturedCount; i++) { 
        if (m_pulseSpacePairCount >= (IR_LIB_MAX_TRANSITIONS / 2)) {
             Debug(DEBUG_BURST, "Warning: Exceeded pulseSpacePairs buffer.\n"); 
             break;
        }

        uint32_t value = localTransitions[i];
        uint32_t currentTimeVal = value & TIME_VALUE_MASK;
        bool isHighToLow = (value & DIRECTION_FLAG_H_TO_L) != 0;

#ifdef DEBUG_RAW_TIMING
        if((DEBUG & DEBUG_RAW_TIMING) == DEBUG_RAW_TIMING) { 
            Debug(DEBUG_RAW_TIMING, i, ": ", currentTimeVal, " us | ", (isHighToLow ? "H->L" : "L->H"));
        }
#endif
        uint32_t deltaTime;
        if (currentTimeVal >= previousTimeVal) {
            deltaTime = currentTimeVal - previousTimeVal;
        } else {
            deltaTime = (TIME_VALUE_MASK - previousTimeVal) + currentTimeVal + 1;
        }
#ifdef DEBUG_RAW_TIMING
        if((DEBUG & DEBUG_RAW_TIMING) == DEBUG_RAW_TIMING) { 
            Debug(DEBUG_RAW_TIMING, " | Delta: ", deltaTime, " us");
            Debug(DEBUG_RAW_TIMING, "\n");
        }
#endif
        if (currentPulse == -1) {
            currentPulse = deltaTime;
        } else {
            int space = deltaTime;
            if (space > IR_LIB_IDLE_TIMEOUT_MS * 1000) { 
                m_pulseSpacePairs[m_pulseSpacePairCount].pulse = currentPulse;
                m_pulseSpacePairs[m_pulseSpacePairCount].space = -1;
                m_pulseSpacePairCount++;
                currentPulse = -1; 
            } else {
                m_pulseSpacePairs[m_pulseSpacePairCount].pulse = currentPulse;
                m_pulseSpacePairs[m_pulseSpacePairCount].space = space;
                m_pulseSpacePairCount++;
                currentPulse = -1;
            }
        }
        previousTimeVal = currentTimeVal;
    }

#ifdef DEBUG_RAW_TIMING
    if((DEBUG & DEBUG_RAW_TIMING) == DEBUG_RAW_TIMING) { 
        Debug(DEBUG_RAW_TIMING, "--- End Raw Transitions ---\n");
    }
#endif
    if (currentPulse != -1 && m_pulseSpacePairCount < (IR_LIB_MAX_TRANSITIONS / 2)) {
        m_pulseSpacePairs[m_pulseSpacePairCount].pulse = currentPulse;
        m_pulseSpacePairs[m_pulseSpacePairCount].space = -1;
        m_pulseSpacePairCount++;
    } else if (currentPulse != -1) { 
        Debug(DEBUG_BURST, "Warning: Exceeded pulseSpacePairs buffer for final pulse.\n");
    }
}


void IRReceiver::_analyzeAndDecodeBurst() {
    Debug(DEBUG_BURST, "\n--- Lib Internal: analyzeBurst ---\n"); 

    for(int i = 0; i < NUM_BRANDS; ++i) { 
        this->m_brandScores[i] = 0;
    }
    this->m_decodedSegmentCount = 0; 
    this->m_finalResultCode = DecodedIR(); 
    this->m_codeResultIsReady = false;   

    if (this->m_pulseSpacePairCount == 0) {
        Debug(DEBUG_BURST, "No pulse/space pairs provided for analysis.\n");
        return;
    }

    Debug(DEBUG_BRAND, "\n--- Lib Internal: Scoring Brands ---\n");
    this->m_brandScores[SONY] = this->scoreSonySIRC12(this->m_pulseSpacePairs, this->m_pulseSpacePairCount);
    this->m_brandScores[JVC] = this->scoreJVC(this->m_pulseSpacePairs, this->m_pulseSpacePairCount);
    this->m_brandScores[NEC] = this->scoreNEC(this->m_pulseSpacePairs, this->m_pulseSpacePairCount);

    Debug(DEBUG_BRAND, "\n--- Lib Internal: Remote Brand Scores (Final) ---\n");
    Debug(DEBUG_BRAND, "JVC Score: ", this->m_brandScores[JVC], "\n", "SONY Score: ", this->m_brandScores[SONY], "\n", "NEC Score: ", this->m_brandScores[NEC], "\n");
    Debug(DEBUG_BRAND, "-----------------------------------\n");

    RemoteBrand winningBrand = UNKNOWN;
    int maxScore = 0;
    for(int i = 1; i < NUM_BRANDS; ++i) {
        if (this->m_brandScores[i] > maxScore) {
            maxScore = this->m_brandScores[i];
            winningBrand = (RemoteBrand)i;
        }
    }
    if (maxScore == 0) winningBrand = UNKNOWN;


    Debug(DEBUG_DECODE_SUMMARY, "\nLib Internal Winning Brand: ", brandToString(winningBrand), " (Score: ", maxScore, ")\n");

    if (winningBrand == UNKNOWN) {
        Debug(DEBUG_DECODE_SUMMARY, "No definitive winning brand. Cannot decode.\n");
        return;
    }
    
    Debug(DEBUG_BURST, "\nLib Internal: Identifying segments for decoding...\n");
    int currentSegmentStartPairIndex = 0; 
    
    int initialPreambleOffset = 0; 
    if (this->m_pulseSpacePairCount > 0 && this->m_pulseSpacePairs[0].pulse != -1 && this->m_pulseSpacePairs[0].space != -1) {
        RemoteBrand preambleMatch = this->matchPreamble(this->m_pulseSpacePairs[0].pulse, this->m_pulseSpacePairs[0].space, false);
        if (preambleMatch != UNKNOWN && preambleMatch == winningBrand) {
            initialPreambleOffset = 1; 
        }
    }

    int dataStartForCurrentSegment = initialPreambleOffset; 
    bool segmentChecksums[IR_LIB_MAX_DECODED_SEGMENTS] = {false}; // For NEC checksums

    for (int i = 0; i < this->m_pulseSpacePairCount && this->m_decodedSegmentCount < IR_LIB_MAX_DECODED_SEGMENTS; ++i) {
        bool isSegmentEnd = false;
        int brand_repeat_delay = 0; 

        if (winningBrand == SONY) brand_repeat_delay = SONY_REPEAT_DELAY;
        else if (winningBrand == NEC) brand_repeat_delay = NEC_REPEAT_DELAY;
        else if (winningBrand == JVC) brand_repeat_delay = JVC_REPEAT_DELAY;

        if (brand_repeat_delay > 0 && this->m_pulseSpacePairs[i].space != -1 && this->isWithinTolerance(this->m_pulseSpacePairs[i].space, brand_repeat_delay, 5000)) {
            isSegmentEnd = true;
        } else if (i == this->m_pulseSpacePairCount - 1) {
            isSegmentEnd = true;
        }

        if (isSegmentEnd) {
            const PulseSpacePair* segmentDataPtr = this->m_pulseSpacePairs + currentSegmentStartPairIndex + dataStartForCurrentSegment;
            int numDataPairsInSegment = (i - (currentSegmentStartPairIndex + dataStartForCurrentSegment)) + 1;

            if (numDataPairsInSegment > 0) {
                if (winningBrand == NEC) {
                    DecodedNECInternal necResult = decodeNECData(segmentDataPtr, numDataPairsInSegment);
                    this->m_decodedSegments[this->m_decodedSegmentCount] = necResult.base;
                    segmentChecksums[this->m_decodedSegmentCount] = necResult.checksumValid;
                } else {
                    this->m_decodedSegments[this->m_decodedSegmentCount] = this->decodeWinningSegment(winningBrand, segmentDataPtr, numDataPairsInSegment);
                    // segmentChecksums remains false for non-NEC
                }

                if(this->m_decodedSegments[this->m_decodedSegmentCount].brand != UNKNOWN) { // Ensure decode function set the brand
                    this->m_decodedSegmentCount++;
                }
            }

            currentSegmentStartPairIndex = i + 1; 
            dataStartForCurrentSegment = 0; 

            if (currentSegmentStartPairIndex < this->m_pulseSpacePairCount &&
                this->m_pulseSpacePairs[currentSegmentStartPairIndex].pulse != -1 &&
                this->m_pulseSpacePairs[currentSegmentStartPairIndex].space != -1) {
                if (this->matchPreamble(this->m_pulseSpacePairs[currentSegmentStartPairIndex].pulse, this->m_pulseSpacePairs[currentSegmentStartPairIndex].space, true) == winningBrand) {
                    dataStartForCurrentSegment = 1; 
                }
            }
        }
    }

    if (this->m_decodedSegmentCount > 0) {
        this->determineWinner(this->m_decodedSegments, this->m_decodedSegmentCount, segmentChecksums); 
        if (this->m_finalResultCode.brand != UNKNOWN && this->m_finalResultCode.command != -1) {
            this->m_codeResultIsReady = true;
        }
    } else {
         Debug(DEBUG_DECODE_SUMMARY, "No segments decoded for the winning brand (after loop).\n");
    }
}

// --- Helper, Scoring, and Decoding Methods ---
// (isWithinTolerance, isWithinPercentageTolerance, matchPreamble, scoreSonySIRC12, scoreJVC, scoreNEC - are the same as before)
// ... (These functions would be here, unchanged from the previous correct version) ...
bool IRReceiver::isWithinTolerance(int captured, int expected, int tolerance) const {
  return abs(captured - expected) <= tolerance;
}

bool IRReceiver::isWithinPercentageTolerance(int captured, int expected, float tolerance_percent) const {
    if (expected == 0) return captured == 0;
    return abs(captured - expected) <= (expected * tolerance_percent);
}

RemoteBrand IRReceiver::matchPreamble(int pulse, int space, bool isRepeatPreamble) const {
  if (isRepeatPreamble) {
      if (this->isWithinTolerance(pulse, JVC_REPEAT_PREAMBLE_PULSE, TIMING_TOLERANCE) &&
          this->isWithinTolerance(space, JVC_REPEAT_PREAMBLE_SPACE, TIMING_TOLERANCE)) {
        return JVC;
      }
      if (this->isWithinTolerance(pulse, SONY_REPEAT_PREAMBLE_PULSE, TIMING_TOLERANCE) &&
          this->isWithinTolerance(space, SONY_REPEAT_PREAMBLE_SPACE, TIMING_TOLERANCE)) {
        return SONY;
      }
      if (this->isWithinTolerance(pulse, NEC_REPEAT_PREAMBLE_PULSE, TIMING_TOLERANCE) &&
          this->isWithinTolerance(space, NEC_REPEAT_PREAMBLE_SPACE, TIMING_TOLERANCE)) {
        return NEC;
      }
  } else {
      if (this->isWithinTolerance(pulse, JVC_PREAMBLE_PULSE, TIMING_TOLERANCE) &&
          this->isWithinTolerance(space, JVC_PREAMBLE_SPACE, TIMING_TOLERANCE)) {
        return JVC;
      }
      if (this->isWithinTolerance(pulse, SONY_PREAMBLE_PULSE, TIMING_TOLERANCE) &&
          this->isWithinTolerance(space, SONY_PREAMBLE_SPACE, TIMING_TOLERANCE)) {
        return SONY;
      }
      if (this->isWithinTolerance(pulse, NEC_PREAMBLE_PULSE, TIMING_TOLERANCE) &&
          this->isWithinTolerance(space, NEC_PREAMBLE_SPACE, TIMING_TOLERANCE)) {
        return NEC;
      }
  }
  return UNKNOWN;
}

int IRReceiver::scoreSonySIRC12(const PulseSpacePair pairs[], int count) const {
    int score = 0;
    Debug(DEBUG_BRAND, "\nScoring for SONY SIRC-12...\n");

    if (count == 0) return 0;

    int currentSegmentStartPairIndex = 0;
    int segmentCount = 0;

    for (int i = 0; i < count; ++i) {
        bool isSegmentEnd = false;
        if ((pairs[i].space != -1 && this->isWithinTolerance(pairs[i].space, SONY_REPEAT_DELAY, 5000)) || i == count - 1) {
             isSegmentEnd = true;
        }

        if (isSegmentEnd) {
            int segmentEndPairIndex = i;
            int segmentPairCount = segmentEndPairIndex - currentSegmentStartPairIndex + 1;

            if (segmentPairCount > 0) {
                segmentCount++;
                Debug(DEBUG_BRAND, "  Detected SONY Segment ", segmentCount, " (Pairs: ", segmentPairCount, ")\n");

                int segmentDataPairsStartIndex = currentSegmentStartPairIndex;
                bool isInitialFrame = (segmentCount == 1);

                if (segmentPairCount > 0 && pairs[currentSegmentStartPairIndex].space != -1) {
                    RemoteBrand preambleMatch = this->matchPreamble(pairs[currentSegmentStartPairIndex].pulse, pairs[currentSegmentStartPairIndex].space, !isInitialFrame);
                    if (isInitialFrame && preambleMatch == SONY) {
                         score++; Debug(DEBUG_BRAND, "    +1: Initial SONY Preamble Match in Segment 1.\n");
                         segmentDataPairsStartIndex = currentSegmentStartPairIndex + 1;
                    } else if (!isInitialFrame && preambleMatch == SONY) {
                         score++; Debug(DEBUG_BRAND, "    +1: Repeat SONY Preamble Match in Segment ", segmentCount, ".\n");
                         segmentDataPairsStartIndex = currentSegmentStartPairIndex + 1;
                    } else if (isInitialFrame) Debug(DEBUG_BRAND, "    +0: Initial SONY Preamble Mismatch in Segment 1.\n");
                    else Debug(DEBUG_BRAND, "    +0: Repeat SONY Preamble Mismatch in Segment ", segmentCount, ".\n");
                } else Debug(DEBUG_BRAND, "    +0: Segment ", segmentCount, " does not start with a valid pulse/space pair for preamble check.\n");

                int dataPairCountInSegment = segmentEndPairIndex - segmentDataPairsStartIndex + 1;
                if (dataPairCountInSegment > 0) {
                    if (isInitialFrame) {
                        if (this->isWithinTolerance(dataPairCountInSegment, SONY_INITIAL_BITS - 1, 2)) {
                            score++; Debug(DEBUG_BRAND, "    +1: Data Pair Count (", dataPairCountInSegment, ") close to expected SONY initial frame data length (", SONY_INITIAL_BITS - 1, ") in Segment 1.\n");
                        } else Debug(DEBUG_BRAND, "    +0: Data Pair Count (", dataPairCountInSegment, ") not close to expected SONY initial frame data length (", SONY_INITIAL_BITS - 1, ") in Segment 1.\n");
                    } else {
                         if (this->isWithinTolerance(dataPairCountInSegment, SONY_REPEAT_BITS - 1, 2)) {
                            score++; Debug(DEBUG_BRAND, "    +1: Data Pair Count (", dataPairCountInSegment, ") close to expected SONY repeat frame data length (", SONY_REPEAT_BITS - 1, ") in Segment ", segmentCount, ".\n");
                         } else Debug(DEBUG_BRAND, "    +0: Data Pair Count (", dataPairCountInSegment, ") not close to expected SONY repeat frame data length (", SONY_REPEAT_BITS - 1, ") in Segment ", segmentCount, ".\n");
                    }

                    if (dataPairCountInSegment > 1) {
                         bool marksAreVariable = false; bool spacesAreFixed = true;
                         int firstDataMark = pairs[segmentDataPairsStartIndex].pulse;
                         bool allMarksFixed = true;
                         for (int j = segmentDataPairsStartIndex + 1; j <= segmentEndPairIndex; ++j) {
                             if (!this->isWithinTolerance(pairs[j].pulse, firstDataMark, TIMING_TOLERANCE)) { allMarksFixed = false; break; }
                         }
                         if (!allMarksFixed) marksAreVariable = true;

                         int firstValidDataSpace = -1;
                         for(int j = segmentDataPairsStartIndex; j <= segmentEndPairIndex; ++j) {
                             if (pairs[j].space != -1 && !this->isWithinTolerance(pairs[j].space, SONY_REPEAT_DELAY, 5000)) { firstValidDataSpace = pairs[j].space; break; }
                         }
                         if (firstValidDataSpace != -1) {
                             for (int j = segmentDataPairsStartIndex; j <= segmentEndPairIndex; ++j) {
                                 if (pairs[j].space != -1 && !this->isWithinTolerance(pairs[j].space, SONY_REPEAT_DELAY, 5000)) {
                                     if (!this->isWithinTolerance(pairs[j].space, firstValidDataSpace, TIMING_TOLERANCE)) { spacesAreFixed = false; break; }
                                 } else if (pairs[j].space != -1 && this->isWithinTolerance(pairs[j].space, SONY_REPEAT_DELAY, 5000) && j != segmentEndPairIndex) {
                                     spacesAreFixed = false; break;
                                 }
                             }
                         } else spacesAreFixed = false;

                         if (marksAreVariable && spacesAreFixed) {
                             score++; Debug(DEBUG_BRAND, "    +1: Variable Mark / Fixed Space Structure Match in Segment ", segmentCount, ".\n");
                         } else Debug(DEBUG_BRAND, "    +0: Variable Mark / Fixed Space Structure Mismatch (Marks Fixed: ", !marksAreVariable, ", Spaces Fixed: ", spacesAreFixed, ") in Segment ", segmentCount, ".\n");
                    } else Debug(DEBUG_BRAND, "    +0: Not enough data pairs in Segment ", segmentCount, " to score structure.\n");
                } else Debug(DEBUG_BRAND, "    +0: No data pairs in Segment ", segmentCount, ".\n");
                currentSegmentStartPairIndex = i + 1;
            }
        }
    }
    Debug(DEBUG_BRAND, "SONY SIRC-12 Final Score: ", score, "\n");
    return score;
}

int IRReceiver::scoreJVC(const PulseSpacePair pairs[], int count) const {
    int score = 0;
    Debug(DEBUG_BRAND, "\nScoring for JVC...\n");
    if (count == 0) return 0;
    int currentSegmentStartPairIndex = 0;
    int segmentCount = 0;

    for (int i = 0; i < count; ++i) {
        bool isSegmentEnd = false;
        if ((pairs[i].space != -1 && this->isWithinTolerance(pairs[i].space, JVC_REPEAT_DELAY, 5000)) || i == count - 1) {
             isSegmentEnd = true;
        }
        if (isSegmentEnd) {
            int segmentEndPairIndex = i;
            int segmentPairCount = segmentEndPairIndex - currentSegmentStartPairIndex + 1;
            if (segmentPairCount > 0) {
                segmentCount++;
                Debug(DEBUG_BRAND, "  Detected JVC Segment ", segmentCount, " (Pairs: ", segmentPairCount, ")\n");
                int segmentDataPairsStartIndex = currentSegmentStartPairIndex;
                bool isInitialFrame = (segmentCount == 1);

                if (isInitialFrame && segmentPairCount > 0 && pairs[currentSegmentStartPairIndex].pulse != -1 && pairs[currentSegmentStartPairIndex].space != -1) {
                     RemoteBrand preambleMatch = this->matchPreamble(pairs[currentSegmentStartPairIndex].pulse, pairs[currentSegmentStartPairIndex].space, false);
                     if (preambleMatch == JVC) { score++; Debug(DEBUG_BRAND, "    +1: Initial JVC Preamble Match in Segment 1.\n"); segmentDataPairsStartIndex = currentSegmentStartPairIndex + 1; }
                     else Debug(DEBUG_BRAND, "    +0: Initial JVC Preamble Mismatch in Segment 1.\n");
                } else if (!isInitialFrame) {
                     int expectedRepeatPairCount = JVC_INITIAL_BITS; 
                     int actualRepeatPairCount = segmentEndPairIndex - currentSegmentStartPairIndex + 1;
                     if (this->isWithinTolerance(actualRepeatPairCount, expectedRepeatPairCount, 2)) { score++; Debug(DEBUG_BRAND, "    +1: JVC Repeat Frame Pair Count (", actualRepeatPairCount, ") close to expected (", expectedRepeatPairCount, ") in Segment ", segmentCount, ".\n"); }
                     else Debug(DEBUG_BRAND, "    +0: JVC Repeat Frame Pair Count (", actualRepeatPairCount, ") not close to expected (", expectedRepeatPairCount, ") in Segment ", segmentCount, ".\n");
                } else Debug(DEBUG_BRAND, "    +0: Segment 1 does not start with a valid pulse/space pair for preamble check.\n");

                if (isInitialFrame) {
                    int expectedDataPairCount = JVC_INITIAL_BITS - 1; 
                    int actualDataPairCountInSegment = segmentEndPairIndex - segmentDataPairsStartIndex + 1;
                    if (this->isWithinTolerance(actualDataPairCountInSegment, expectedDataPairCount, 2)) { score++; Debug(DEBUG_BRAND, "    +1: Data Pair Count (", actualDataPairCountInSegment, ") close to expected JVC initial frame data length (", expectedDataPairCount, ") in Segment 1.\n"); }
                    else Debug(DEBUG_BRAND, "    +0: Data Pair Count (", actualDataPairCountInSegment, ") not close to expected JVC initial frame data length (", expectedDataPairCount, ") in Segment 1.\n");
                }

                 if (segmentPairCount > 1) {
                     bool marksAreFixed = true; bool spacesAreVariable = false;
                     int bitStructureCheckStartIndex = isInitialFrame ? segmentDataPairsStartIndex : currentSegmentStartPairIndex;
                     int firstDataMark = pairs[bitStructureCheckStartIndex].pulse;
                     for (int j = bitStructureCheckStartIndex; j <= segmentEndPairIndex; ++j) {
                         if (pairs[j].pulse == -1) continue;
                         if (!this->isWithinTolerance(pairs[j].pulse, firstDataMark, TIMING_TOLERANCE)) { marksAreFixed = false; break; }
                     }
                     int firstValidDataSpace = -1; int spacesCheckStartIndex = isInitialFrame ? segmentDataPairsStartIndex : currentSegmentStartPairIndex;
                     for(int j = spacesCheckStartIndex; j <= segmentEndPairIndex; ++j) {
                         if (pairs[j].space != -1 && !this->isWithinTolerance(pairs[j].space, JVC_REPEAT_DELAY, 5000)) { firstValidDataSpace = pairs[j].space; break; }
                     }
                     bool allSpacesFixed = true;
                     if (firstValidDataSpace != -1) {
                         for (int j = spacesCheckStartIndex; j <= segmentEndPairIndex; ++j) {
                             if (pairs[j].space != -1 && !this->isWithinTolerance(pairs[j].space, JVC_REPEAT_DELAY, 5000)) {
                                 if (!this->isWithinTolerance(pairs[j].space, firstValidDataSpace, TIMING_TOLERANCE)) { allSpacesFixed = false; break; }
                             }
                         }
                     } else allSpacesFixed = false;
                     if (!allSpacesFixed) spacesAreVariable = true;

                     if (marksAreFixed && spacesAreVariable) { score++; Debug(DEBUG_BRAND, "    +1: Fixed Mark / Variable Space Structure Match in Segment ", segmentCount, ".\n"); }
                     else Debug(DEBUG_BRAND, "    +0: Fixed Mark / Variable Space Structure Mismatch (Marks Fixed: ", marksAreFixed, ", Spaces Fixed: ", !spacesAreVariable, ") in Segment ", segmentCount, ".\n");
                 } else if (segmentPairCount <= 1) Debug(DEBUG_BRAND, "    +0: Not enough data pairs in Segment ", segmentCount, " to score structure.\n");
                currentSegmentStartPairIndex = i + 1;
            }
        }
    }
    Debug(DEBUG_BRAND, "JVC Final Score: ", score, "\n");
    return score;
}

int IRReceiver::scoreNEC(const PulseSpacePair pairs[], int count) const {
    int score = 0;
    Debug(DEBUG_BRAND, "\nScoring for NEC...\n"); 
    if (count == 0) return 0;
    int currentSegmentStartPairIndex = 0;
    int segmentCount = 0;

    for (int i = 0; i < count; ++i) {
        bool isSegmentEnd = false;
        if ((pairs[i].space != -1 && this->isWithinTolerance(pairs[i].space, NEC_REPEAT_DELAY, 5000)) || i == count - 1) {
             isSegmentEnd = true;
        }
        if (isSegmentEnd) {
            int segmentEndPairIndex = i;
            int segmentPairCount = segmentEndPairIndex - currentSegmentStartPairIndex + 1;
            if (segmentPairCount > 0) {
                segmentCount++;
                Debug(DEBUG_BRAND, "  Detected NEC Segment ", segmentCount, " (Pairs: ", segmentPairCount, ")\n");
                int segmentDataPairsStartIndex = currentSegmentStartPairIndex;
                bool isInitialFrame = (segmentCount == 1);

                if (segmentPairCount > 0 && pairs[currentSegmentStartPairIndex].pulse != -1 && pairs[currentSegmentStartPairIndex].space != -1) {
                    RemoteBrand preambleMatch = this->matchPreamble(pairs[currentSegmentStartPairIndex].pulse, pairs[currentSegmentStartPairIndex].space, !isInitialFrame);
                    if (isInitialFrame && preambleMatch == NEC) { score++; Debug(DEBUG_BRAND, "    +1: Initial NEC Preamble Match in Segment 1.\n"); segmentDataPairsStartIndex = currentSegmentStartPairIndex + 1; }
                    else if (!isInitialFrame && preambleMatch == NEC) { score++; Debug(DEBUG_BRAND, "    +1: Repeat NEC Preamble Match in Segment ", segmentCount, ".\n"); segmentDataPairsStartIndex = currentSegmentStartPairIndex + 1; }
                    else if (isInitialFrame) Debug(DEBUG_BRAND, "    +0: Initial NEC Preamble Mismatch in Segment 1.\n");
                    else Debug(DEBUG_BRAND, "    +0: Repeat NEC Preamble Mismatch in Segment ", segmentCount, ".\n");
                } else Debug(DEBUG_BRAND, "    +0: Segment ", segmentCount, " does not start with a valid pulse/space pair for preamble check.\n");

                int dataPairCountInSegment = segmentEndPairIndex - segmentDataPairsStartIndex + 1;
                if (dataPairCountInSegment > 0 || (!isInitialFrame && dataPairCountInSegment == NEC_REPEAT_BITS)) { 
                    if (isInitialFrame) {
                        if (this->isWithinTolerance(dataPairCountInSegment, NEC_INITIAL_BITS -1, 2)) { 
                            score++; Debug(DEBUG_BRAND, "    +1: Data Pair Count (", dataPairCountInSegment, ") close to expected NEC initial frame data length (", NEC_INITIAL_BITS -1, ") in Segment 1.\n");
                        } else Debug(DEBUG_BRAND, "    +0: Data Pair Count (", dataPairCountInSegment, ") not close to expected NEC initial frame data length (", NEC_INITIAL_BITS -1, ") in Segment 1.\n");
                    } else {
                         if (this->isWithinTolerance(dataPairCountInSegment, NEC_REPEAT_BITS, 1)) {
                            score++; Debug(DEBUG_BRAND, "    +1: Data Pair Count (", dataPairCountInSegment, ") close to expected NEC repeat frame data length (", NEC_REPEAT_BITS, ") in Segment ", segmentCount, ".\n");
                         } else Debug(DEBUG_BRAND, "    +0: Data Pair Count (", dataPairCountInSegment, ") not close to expected NEC repeat frame data length (", NEC_REPEAT_BITS, ") in Segment ", segmentCount, ".\n");
                    }

                    if (dataPairCountInSegment > 1 && isInitialFrame) {
                         bool marksAreFixed = true; bool spacesAreVariable = false;
                         int firstDataMark = pairs[segmentDataPairsStartIndex].pulse;
                         Debug(DEBUG_BRAND, "    Checking bit structure for Segment ", segmentCount, " (Data Pairs: ", dataPairCountInSegment, ")\n");
                         Debug(DEBUG_BRAND, "      First Data Mark: ", firstDataMark, " us\n");
                         for (int j = segmentDataPairsStartIndex; j <= segmentEndPairIndex; ++j) {
                             if (pairs[j].pulse == -1) { Debug(DEBUG_BRAND, "      Skipping Pair ", j, ": Missing pulse\n"); continue; }
                             Debug(DEBUG_BRAND, "      Checking Pair ", j, ": Pulse=", pairs[j].pulse, " us, Space=", (pairs[j].space == -1 ? "MISSING" : String(pairs[j].space).c_str()), " us\n");
                             if (!this->isWithinTolerance(pairs[j].pulse, firstDataMark, TIMING_TOLERANCE)) { marksAreFixed = false; Debug(DEBUG_BRAND, "      Mark at Pair ", j, " (", pairs[j].pulse, " us) is not fixed.\n"); break; }
                             else Debug(DEBUG_BRAND, "      Mark at Pair ", j, " (", pairs[j].pulse, " us) is fixed.\n");
                         }
                         if (!marksAreFixed) Debug(DEBUG_BRAND, "      Marks are NOT fixed.\n"); else Debug(DEBUG_BRAND, "      Marks ARE fixed.\n");
                         if (marksAreFixed) spacesAreVariable = true; 

                         int firstValidDataSpace = -1;
                         Debug(DEBUG_BRAND, "      Finding first valid data space...\n");
                         for(int j = segmentDataPairsStartIndex; j <= segmentEndPairIndex; ++j) {
                             if (pairs[j].space != -1 && !this->isWithinTolerance(pairs[j].space, NEC_REPEAT_DELAY, 5000)) { firstValidDataSpace = pairs[j].space; Debug(DEBUG_BRAND, "      First valid data space found at Pair ", j, ": ", firstValidDataSpace, " us\n"); break; }
                             Debug(DEBUG_BRAND, "      Pair ", j, " space (", (pairs[j].space == -1 ? "MISSING" : String(pairs[j].space).c_str()), " us) is repeat delay or missing.\n");
                         }
                         bool allSpacesFixed = true;
                         if (firstValidDataSpace != -1) {
                             Debug(DEBUG_BRAND, "      Checking if all spaces are fixed...\n");
                             for (int j = segmentDataPairsStartIndex; j <= segmentEndPairIndex; ++j) {
                                 if (pairs[j].space != -1 && !this->isWithinTolerance(pairs[j].space, NEC_REPEAT_DELAY, 5000)) {
                                     Debug(DEBUG_BRAND, "      Checking Pair ", j, " space: ", pairs[j].space, " us\n");
                                     if (!this->isWithinTolerance(pairs[j].space, firstValidDataSpace, TIMING_TOLERANCE)) { allSpacesFixed = false; Debug(DEBUG_BRAND, "      Space at Pair ", j, " (", pairs[j].space, " us) is not fixed.\n"); break; }
                                     else Debug(DEBUG_BRAND, "      Space at Pair ", j, " (", pairs[j].space, " us) is fixed.\n");
                                 } else if (pairs[j].space != -1 && this->isWithinTolerance(pairs[j].space, NEC_REPEAT_DELAY, 5000) && j != segmentEndPairIndex) {
                                     allSpacesFixed = false; Debug(DEBUG_BRAND, "      Found repeat gap within data segment at Pair ", j, ".\n"); break;
                                 } else Debug(DEBUG_BRAND, "      Pair ", j, " space (", (pairs[j].space == -1 ? "MISSING" : String(pairs[j].space).c_str()), " us) is repeat delay or missing.\n");
                             }
                         } else { allSpacesFixed = false; Debug(DEBUG_BRAND, "      No valid data spaces found to check for fixed timing.\n"); }
                         if (!allSpacesFixed) spacesAreVariable = true; 

                         Debug(DEBUG_BRAND, "      Marks Fixed: ", marksAreFixed, ", Spaces Variable: ", spacesAreVariable, "\n");
                         if (marksAreFixed && spacesAreVariable) { score++; Debug(DEBUG_BRAND, "    +1: Fixed Mark / Variable Space Structure Match in Segment ", segmentCount, ".\n"); }
                         else Debug(DEBUG_BRAND, "    +0: Fixed Mark / Variable Space Structure Mismatch (Marks Fixed: ", marksAreFixed, ", Spaces Fixed: ", !spacesAreVariable, ") in Segment ", segmentCount, ".\n");
                     } else if (dataPairCountInSegment <= 1 && isInitialFrame) Debug(DEBUG_BRAND, "    +0: Not enough data pairs in Segment ", segmentCount, " to score structure.\n");
                }
                currentSegmentStartPairIndex = i + 1;
            }
        }
    }
    return score; 
}

// This is the original decodeWinningSegment. It doesn't handle NEC checksums.
// If NEC checksums are important and were part of the original .ino's DecodedIR struct,
// this will need adjustment or a separate path for NEC.
DecodedIR IRReceiver::decodeWinningSegment(RemoteBrand brand, const PulseSpacePair dataPairs[], int dataPairCount) const {
    DecodedIR decodedData; // This struct does NOT have checksumValid as per your latest .h
    if (brand == UNKNOWN || dataPairCount == 0) {
        Debug(DEBUG_DECODE_SUMMARY, "  Cannot decode segment: Unknown brand or no data pairs.\n");
        return decodedData;
    }
    decodedData.brand = brand;
    uint32_t rawBits = 0;
    int bitCount = 0;
    int maxBitsToDecode = 32;

    Debug(DEBUG_BITS, "  Attempting to decode data segment for brand: ", brandToString(brand), ". Segment has ", dataPairCount, " pulse/space pairs.\n");

    if (brand == SONY) {
        maxBitsToDecode = SONY_INITIAL_BITS - 1;
        for (int i = 0; i < dataPairCount && bitCount < maxBitsToDecode; ++i) {
            int pulse = dataPairs[i].pulse; int space = dataPairs[i].space;
            Debug(DEBUG_BITS, "    Pair ", i, " (Bit ", bitCount, "): Pulse: ", pulse, " us, Space: ", (space == -1 ? "MISSING" : String(space).c_str()), " us -> ");
            if (pulse != -1) {
                if (this->isWithinTolerance(pulse, SONY_ZERO_PULSE, TIMING_TOLERANCE)) { rawBits |= (0UL << bitCount); Debug(DEBUG_BITS, "0\n"); bitCount++; }
                else if (this->isWithinTolerance(pulse, SONY_ONE_PULSE, TIMING_TOLERANCE)) { rawBits |= (1UL << bitCount); Debug(DEBUG_BITS, "1\n"); bitCount++; }
                else { Debug(DEBUG_BITS, "UNKNOWN PULSE Timing\n"); break; }
            } else { Debug(DEBUG_BITS, "MISSING PULSE\n"); break; }
            if (bitCount == maxBitsToDecode && space != -1 && this->isWithinTolerance(space, SONY_REPEAT_DELAY, 5000)) { Debug(DEBUG_BITS, "    Detected SONY repeat delay after decoding last bit.\n"); break; }
            if (bitCount == maxBitsToDecode && space != -1 && this->isWithinTolerance(space, SONY_BIT_SPACE, TIMING_TOLERANCE)) { Debug(DEBUG_BITS, "    Decoded all expected SONY data bits.\n"); break; }
        }
        if (bitCount >= 7) decodedData.command = (rawBits >> 0) & 0x7F;
        if (bitCount >= 12) decodedData.address = (rawBits >> 7) & 0x1F;
        Debug(DEBUG_DECODE_SUMMARY, "  Decoded SONY (", bitCount, " bits) - Command: ", decodedData.command, ", Address: ", decodedData.address, "\n");

    } else if (brand == JVC) { // Separated JVC from NEC for clarity as NEC was more complex
        const int expectedPulse = JVC_BIT_PULSE;
        const int zeroSpace = JVC_ZERO_SPACE;
        const int oneSpace = JVC_ONE_SPACE;
        maxBitsToDecode = 16; // 8 Addr + 8 Cmd for JVC

        for (int i = 0; i < dataPairCount && bitCount < maxBitsToDecode; ++i) {
            int pulse = dataPairs[i].pulse; int space = dataPairs[i].space;
            if (i == dataPairCount - 1 && space == -1) { space = zeroSpace; Debug(DEBUG_BITS, "    Inferred last JVC space as ZERO.\n");}
            Debug(DEBUG_BITS, "    Pair ", i, " (Bit ", bitCount, "): Pulse: ", pulse, " us, Space: ", (space == -1 ? "MISSING" : String(space).c_str()), " us -> ");
            if (this->isWithinTolerance(pulse, expectedPulse, TIMING_TOLERANCE)) {
                 if (space != -1) {
                      if (this->isWithinTolerance(space, zeroSpace, TIMING_TOLERANCE)) { rawBits |= (0UL << bitCount); Debug(DEBUG_BITS, "0\n"); bitCount++; }
                      else if (this->isWithinTolerance(space, oneSpace, TIMING_TOLERANCE)) { rawBits |= (1UL << bitCount); Debug(DEBUG_BITS, "1\n"); bitCount++; }
                      else { Debug(DEBUG_BITS, "UNKNOWN SPACE Timing\n"); break; }
                 } else { Debug(DEBUG_BITS, "MISSING SPACE (Not Last Bit)\n"); break; }
            } else { Debug(DEBUG_BITS, "UNKNOWN PULSE\n"); break; }
        }
        if (bitCount >= 8) decodedData.address = (rawBits >> 0) & 0xFF;
        if (bitCount >= 16) decodedData.command = (rawBits >> 8) & 0xFF;
        Debug(DEBUG_DECODE_SUMMARY, "  Decoded JVC (", bitCount, " bits) - Address: ", decodedData.address, ", Command: ", decodedData.command, "\n");
    }
    // NEC decoding is handled by decodeNECData now
    return decodedData;
}

// Specific decoder for NEC to handle checksum, returning it via DecodedNECInternal
IRReceiver::DecodedNECInternal IRReceiver::decodeNECData(const PulseSpacePair dataPairs[], int dataPairCount) const {
    DecodedNECInternal result;
    result.base.brand = NEC; // Set brand for the base part
    uint32_t rawBits = 0;
    int bitCount = 0;
    int maxBitsToDecode = NEC_INITIAL_BITS - 1; // 32 data bits for NEC

    Debug(DEBUG_BITS, "  Attempting to decode NEC data. Segment has ", dataPairCount, " pulse/space pairs.\n");

    const int expectedPulse = NEC_BIT_PULSE;
    const int zeroSpace = NEC_ZERO_SPACE;
    const int oneSpace = NEC_ONE_SPACE;

    for (int i = 0; i < dataPairCount && bitCount < maxBitsToDecode; ++i) {
        int pulse = dataPairs[i].pulse; int space = dataPairs[i].space;
        if (i == dataPairCount - 1 && space == -1) { space = zeroSpace; Debug(DEBUG_BITS, "    Inferred last NEC space as ZERO.\n");}
        Debug(DEBUG_BITS, "    Pair ", i, " (Bit ", bitCount, "): Pulse: ", pulse, " us, Space: ", (space == -1 ? "MISSING" : String(space).c_str()), " us -> ");
        if (this->isWithinTolerance(pulse, expectedPulse, TIMING_TOLERANCE)) {
             if (space != -1) {
                  if (this->isWithinTolerance(space, zeroSpace, TIMING_TOLERANCE)) { rawBits |= (0UL << bitCount); Debug(DEBUG_BITS, "0\n"); bitCount++; }
                  else if (this->isWithinTolerance(space, oneSpace, TIMING_TOLERANCE)) { rawBits |= (1UL << bitCount); Debug(DEBUG_BITS, "1\n"); bitCount++; }
                  else { Debug(DEBUG_BITS, "UNKNOWN SPACE Timing\n"); break; }
             } else { Debug(DEBUG_BITS, "MISSING SPACE (Not Last Bit)\n"); break; }
        } else { Debug(DEBUG_BITS, "UNKNOWN PULSE\n"); break; }
    }

    int addressByte1 = (bitCount >= 8) ? (rawBits >> 0) & 0xFF : -1;
    int addressByte2 = (bitCount >= 16) ? (rawBits >> 8) & 0xFF : -1;
    int commandByte1 = (bitCount >= 24) ? (rawBits >> 16) & 0xFF : -1;
    int commandByte2 = (bitCount >= 32) ? (rawBits >> 24) & 0xFF : -1;

    if (addressByte1 != -1 && addressByte2 != -1 && (uint8_t)(addressByte1 + addressByte2) == 0xFF) { 
        result.base.address = addressByte1; 
        Debug(DEBUG_DECODE_SUMMARY, "  NEC Address: 8-bit (", result.base.address, ")\n"); 
    } else if (addressByte1 != -1 && addressByte2 != -1) { // Extended NEC or non-standard
        result.base.address = (addressByte2 << 8) | addressByte1; 
        Debug(DEBUG_DECODE_SUMMARY, "  NEC Address: 16-bit (", result.base.address, ")\n"); 
    } else if (addressByte1 != -1) { // Only one address byte decoded
        result.base.address = addressByte1; 
        Debug(DEBUG_DECODE_SUMMARY, "  NEC Address: 8-bit (partial decode: ", result.base.address, ")\n"); 
    } else {
        Debug(DEBUG_DECODE_SUMMARY, "  NEC Address: UNKNOWN (not enough bits)\n");
    }

    if (commandByte1 != -1) {
         result.base.command = commandByte1; 
         Debug(DEBUG_DECODE_SUMMARY, "  NEC Command: ", result.base.command, "\n");
         if (commandByte2 != -1) {
              if ((uint8_t)(commandByte1 + commandByte2) == 0xFF) { 
                  result.checksumValid = true; 
                  Debug(DEBUG_DECODE_SUMMARY, "  NEC Command Checksum Valid.\n"); 
              } else {
                   Debug(DEBUG_DECODE_SUMMARY, "  NEC Command Checksum Invalid (", commandByte1, " + ", commandByte2, " != 255).\n");
              }
         } else {
             Debug(DEBUG_DECODE_SUMMARY, "  NEC Command Checksum: Not enough bits for validation.\n");
         }
    } else {
         Debug(DEBUG_DECODE_SUMMARY, "  NEC Command: UNKNOWN (not enough bits)\n");
    }
    Debug(DEBUG_DECODE_SUMMARY, "  Decoded NEC (", bitCount, " bits) - Address: ", result.base.address, ", Command: ", result.base.command, ", Checksum Valid: ", result.checksumValid ? "Yes" : "No", "\n");
    return result;
}


void IRReceiver::determineWinner(const DecodedIR segments[], int count, const bool segmentChecksums[]) {
    this->m_finalResultCode = DecodedIR(); 

    if (count == 0) {
        Debug(DEBUG_DECODE_SUMMARY, "\n--- No Decoded Segments for Winner Determination ---\n");
        return;
    }
    Debug(DEBUG_DECODE_SUMMARY, "\nDecoded Segments:\n");
    for(int i = 0; i < count; ++i) {
        Debug(DEBUG_DECODE_SUMMARY, "  Segment ", i + 1, ": Brand=", brandToString(segments[i].brand), ", Command=", segments[i].command, ", Address=", segments[i].address);
        if (segments[i].brand == NEC) Debug(DEBUG_DECODE_SUMMARY, ", Checksum Valid=", segmentChecksums[i] ? "Yes" : "No");
        Debug(DEBUG_DECODE_SUMMARY, "\n");
    }

    struct DecodedCount { DecodedIR data; int count = 0; bool checksumValidForNEC = false; }; // Add checksum for NEC uniqueness
    DecodedCount counts[IR_LIB_MAX_DECODED_SEGMENTS]; 
    int uniqueCount = 0;

    for(int i = 0; i < count; ++i) {
        bool found = false;
        if (segments[i].brand != UNKNOWN && segments[i].command != -1) { // Only process valid decodes
            for(int j = 0; j < uniqueCount; ++j) {
                bool match = (segments[i].brand == counts[j].data.brand &&
                              segments[i].command == counts[j].data.command &&
                              segments[i].address == counts[j].data.address );
                if (segments[i].brand == NEC) {
                    match &= (segmentChecksums[i] == counts[j].checksumValidForNEC);
                }
                // Other brands don't use checksum for this comparison
                
                if (match) {
                    counts[j].count++;
                    found = true;
                    break;
                }
            }
            if (!found && uniqueCount < IR_LIB_MAX_DECODED_SEGMENTS) {
                counts[uniqueCount].data = segments[i];
                counts[uniqueCount].count = 1;
                if(segments[i].brand == NEC) {
                    counts[uniqueCount].checksumValidForNEC = segmentChecksums[i];
                }
                uniqueCount++;
            }
        }
    }

    int maxCount = 0; int winnerIndex = -1;
    for(int i = 0; i < uniqueCount; ++i) {
        if (counts[i].count > maxCount) {
            maxCount = counts[i].count;
            winnerIndex = i;
        }
    }

    if (winnerIndex != -1) {
        this->m_finalResultCode = counts[winnerIndex].data;
        // Note: m_finalResultCode (DecodedIR) does not store checksum.
        // The example sketch would need to be aware if it wants to print this.
        // For now, the library determines the winner based on it, but doesn't store it in the final public struct.
        Debug(DEBUG_DECODE_SUMMARY, "\n--- Winning Decoded IR Signal ---\n");
        Debug(DEBUG_DECODE_SUMMARY, "Brand: ", brandToString(this->m_finalResultCode.brand), ", Command: ", this->m_finalResultCode.command, ", Address: ", this->m_finalResultCode.address);
        if (this->m_finalResultCode.brand == NEC) Debug(DEBUG_DECODE_SUMMARY, ", (NEC Checksum for winning segment: ", counts[winnerIndex].checksumValidForNEC ? "Valid" : "Invalid", ")");
        Debug(DEBUG_DECODE_SUMMARY, " (Occurrences: ", counts[winnerIndex].count, ")\n");
        Debug(DEBUG_DECODE_SUMMARY, "-----------------------------------\n");
    } else {
        Debug(DEBUG_DECODE_SUMMARY, "\n--- No Winning Decoded Signal Found ---\n");
    }
}

const char* IRReceiver::brandToString(RemoteBrand brand) const {
    switch (brand) {
        case JVC: return "JVC";
        case SONY: return "SONY";
        case NEC: return "NEC";
        case UNKNOWN: default: return "UNKNOWN";
    }
}

const char* IRReceiver::getButtonName(RemoteBrand brand, int commandCode) const {
    const IrButton* buttonArray = nullptr;
    size_t buttonCount = 0;

    // Select the correct button array based on the brand
    // Your notes indicate Sceptre uses Sony protocol, so we map SONY brand to SCEPTRE_BUTTONS
    switch (brand) {
        case SONY: // Sceptre codes are used for SONY brand
            buttonArray = SCEPTRE_BUTTONS;
            buttonCount = SCEPTRE_BUTTONS_COUNT;
            break;
        case JVC:
            buttonArray = JVC_BUTTONS;
            buttonCount = JVC_BUTTONS_COUNT;
            break;
        case NEC:
            buttonArray = NEC_BUTTONS;
            buttonCount = NEC_BUTTONS_COUNT;
            break;
        case UNKNOWN:
        default:
            // For an unknown brand, we can't look up in a specific array.
            // Fall through to the generic unknown command formatting.
            break;
    }

    // Iterate through the selected array to find the command code
    if (buttonArray != nullptr && buttonCount > 0) {
        for (size_t i = 0; i < buttonCount; ++i) {
            if (buttonArray[i].commandCode == commandCode) {
                return buttonArray[i].name; // Found the button name
            }
        }
    }

    // If code not found, format a string like "BRAND_CMD_DDD" or "CMD_DDD"
    // using itoa and string functions.
    static char unknownCmdBuf[32]; // Ensure buffer is large enough
    char numBuf[15]; // Buffer for the number string (max 11 chars for int + null)

    itoa(commandCode, numBuf, 6); // Convert integer to string (base 10)

    switch (brand) {
        case SONY:
            strcpy(unknownCmdBuf, "SONY_CMD_");
            break;
        case JVC:
            strcpy(unknownCmdBuf, "JVC_CMD_");
            break;
        case NEC:
            strcpy(unknownCmdBuf, "NEC_CMD_");
            break;
        case UNKNOWN:
        default:
            strcpy(unknownCmdBuf, "CMD_");
            break;
    }
    strcat(unknownCmdBuf, numBuf); // Append the number string
    return unknownCmdBuf;
}


