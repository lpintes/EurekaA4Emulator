#ifndef EUREKA_RESOURCE_H
#define EUREKA_RESOURCE_H

#define IDR_MAIN_MENU        100
#define IDR_ACCELERATORS     101

#define IDD_SETTINGS         200
#define IDD_ABOUT            201

// Menu and accelerator commands.
#define ID_FILE_EXPORT       40001
#define ID_FILE_EXIT         40002
#define ID_MACHINE_RESET     40010
#define ID_MACHINE_POWEROFF  40011
#define ID_KEYBOARD_BRAILLE  40020
#define ID_KEYBOARD_PC       40021
#define ID_KEYBOARD_TOGGLE   40022
#define ID_KEYBOARD_RELEASE  40023
#define ID_KEYBOARD_PASSONCE 40024
#define ID_TOOLS_SETTINGS    40030
#define ID_TOOLS_DIAGDUMP    40031
#define ID_HELP_KEYS         40040
#define ID_HELP_ABOUT        40041
// Not a menu item: the accelerator that opens the menu bar, because Alt and
// F10 both belong to the guest.
#define ID_ACTIVATE_MENU     40050

// Settings dialog controls.
#define IDC_MODE_BRAILLE     1001
#define IDC_MODE_PC          1002
#define IDC_DIAGNOSTICS      1003

// About dialog controls.
#define IDC_ABOUT_TEXT       1010

// Labels and group boxes need no id of their own.
#ifndef IDC_STATIC
#define IDC_STATIC           -1
#endif

#endif
