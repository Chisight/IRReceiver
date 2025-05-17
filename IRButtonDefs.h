#ifndef IR_BUTTON_DEFS_H
#define IR_BUTTON_DEFS_H

#include <stddef.h> // For size_t

struct IrButton {
    int commandCode;
    const char* name;
};

extern const IrButton SCEPTRE_BUTTONS[]; 
extern const size_t SCEPTRE_BUTTONS_COUNT;
extern const IrButton JVC_BUTTONS[];
extern const size_t JVC_BUTTONS_COUNT;
extern const IrButton NEC_BUTTONS[];
extern const size_t NEC_BUTTONS_COUNT;

#endif // IR_BUTTON_DEFS_H

