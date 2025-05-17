#include "IRButtonDefs.h"

// --- Sceptre/Sony Button Definitions ---
//Most of these were captured trial and error, but examples exist at http://www.hifi-remote.com/sony/Sony_tv.htm and http://www.johncon.com/john/archive/rawSend.Sceptre.ino
//Sony codes commonly found online include the device address (0b00010000) and are obfuscated by being bit reversed (MSB first)
const IrButton SCEPTRE_BUTTONS[] = {
    {0, "sceptreOne"},
    {1, "sceptreTwo"},
    {2, "sceptreThree"},
    {3, "sceptreFour"},
    {4, "sceptreFive"},
    {5, "sceptreSix"},
    {6, "sceptreSeven"},
    {7, "sceptreEight"},
    {8, "sceptreNine"},
    {9, "sceptreZero"},
    {11, "sceptreEnter"},
    {16, "sceptreCh+"},
    {17, "sceptreCh-"},
    {18, "sceptreVol+"},
    {19, "sceptreVol-"},
    {20, "sceptreMute"},          // toggle
    {21, "sceptrePower"},         // toggle
    {22, "sceptreExit"},          // Exit menus
    {24, "sceptreFav"},           // Fav button
    {35, "sceptreCc"},
    {36, "sceptreFavAdd"},        // Fav add button (add tv station favorite??)
    {37, "sceptreSource"},        //TV,AV,YPbPr,HDMI1,HDMI2,HDMI3,HDMI4,Media, current is selected, arrows change, enter sets.
                                  //times out with no change unless enter pressed. To go from unknown input to HDMIn, send 
                                  //YPBPR followed by HDMI x the HDMI number desierd.  Sending YPBPR, HDMI, HDMI will set HDMI2
    {41, "sceptreStandard"},      // toggles sound modes and displays in lower left corner
    {46, "sceptrePwrOn"},
    {47, "sceptrePwrOff"},
    {51, "sceptreRight"},
    {52, "sceptreLeft"},
    {54, "sceptreSleep"},         //1st brings up current setting, additional adds 10 minutes, 120+10=off
    {57, "sceptreHdmi"},          // rotate to next HDMI input
    {60, "sceptreInfo"},          // toggle, times out
    {64, "sceptreAir"},           // select over the Air (aka TV)
    {65, "sceptreAv"},
    {67, "sceptreHdmi3"},
    {68, "sceptreHdmi4"},
    {69, "sceptreYpbpr"},
    {72, "sceptreHdmi1"},
    {90, "sceptreUser"},          // toggles video presets displays in lower left corner, times out
    {96, "sceptreMenu"},
    {98, "sceptreFull"},          // toggles screen zoom modes and displays in lower left corner, times out
    {99, "sceptreUsb"},
    {100, "sceptreFreeze"},       //toggle hold of image on screen
    {101, "sceptreGuide"},        // toggle tv guide
    {110, "sceptreAir2"},         // duplicate of 64???
    {111, "sceptreAir3"},         // duplicate of 64???
    {116, "sceptreUp"},
    {117, "sceptreDown"},
    {123, "sceptreVoice"}        // toggle menu voice assist
};
const size_t SCEPTRE_BUTTONS_COUNT = sizeof(SCEPTRE_BUTTONS) / sizeof(IrButton);


// --- JVC Button Definitions ---
//Most of these were captured trial and error, format information is at https://www.sbprojects.net/knowledge/ir/jvc.php
//JVC codes commonly found online include the device address and are MSB first, only the data is included here and it is in LSB first order.
const IrButton JVC_BUTTONS[] = {
    {0, "jvcPwr"}, 
    {1, "jvcVol+"},
    {2, "jvcVol-"},
    {13, "jvcAux"}     // select AUX input
    // Add more JVC buttons here as needed
};
const size_t JVC_BUTTONS_COUNT = sizeof(JVC_BUTTONS) / sizeof(IrButton);

// --- NEC Button Definitions ---
const IrButton NEC_BUTTONS[] = {
    {0, "necPwr"}, 
    {16, "necPlay"},
    {19, "necStop"},
    {64, "nvcTray"} //open/close DVD tray
    // Add more JVC buttons here as needed
};
const size_t NEC_BUTTONS_COUNT = sizeof(NEC_BUTTONS) / sizeof(IrButton);

