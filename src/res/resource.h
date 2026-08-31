#ifndef EUREKA_RESOURCE_H
#define EUREKA_RESOURCE_H

#define IDR_MAIN_MENU        100
// Two accelerator tables: the first always applies, the second only while the
// keyboard belongs to the host.  See the comments in eureka.rc.
#define IDR_ACCELERATORS     101
#define IDR_ACCELERATORS_HOST 102

#define IDD_SETTINGS         200
#define IDD_ABOUT            201
#define IDD_SLOTS            202
#define IDD_NEWDISK          203

// Menu and accelerator commands.
#define ID_FILE_EXPORT       40001
#define ID_FILE_EXIT         40002
#define ID_DISK_INSERT       40003
#define ID_DISK_EJECT        40004
#define ID_DISK_NEW          40005
#define ID_DISK_PROTECT      40006
#define ID_MACHINE_RESET     40010
#define ID_MACHINE_POWEROFF  40011
#define ID_KEYBOARD_BRAILLE  40020
#define ID_KEYBOARD_PC       40021
#define ID_KEYBOARD_TOGGLE   40022
#define ID_KEYBOARD_RELEASE  40023
#define ID_KEYBOARD_PASSONCE 40024
// F11 and F12 are the guest's keys too -- the ROM's own table for the PC
// keyboard maps scan codes 57h and 58h to CAh and CBh (1DF05) -- so the two
// the host took have to be reachable some other way.  A menu item costs no
// key at all, and a screen reader reads it out.
#define ID_KEYBOARD_SEND_F11 40025
#define ID_KEYBOARD_SEND_AF11 40026
#define ID_KEYBOARD_SEND_F12 40027
#define ID_KEYBOARD_SEND_AF12 40028
#define ID_TOOLS_SETTINGS    40030
#define ID_TOOLS_DIAGDUMP    40031
#define ID_HELP_KEYS         40040
#define ID_HELP_ABOUT        40041
// Not a menu item: the accelerator that opens the menu bar, because Alt and
// F10 both belong to the guest.
#define ID_ACTIVATE_MENU     40050

// Quick-choice slots.  Nine consecutive ids so the handler can work out which
// slot it is by subtracting; the menu and Ctrl+digit both name them 1..9.
// Spelled out one by one because the .rc cannot compute them, and a menu
// carrying bare numbers would drift away from this header without a word.
#define ID_DISK_SLOT1        40061
#define ID_DISK_SLOT2        40062
#define ID_DISK_SLOT3        40063
#define ID_DISK_SLOT4        40064
#define ID_DISK_SLOT5        40065
#define ID_DISK_SLOT6        40066
#define ID_DISK_SLOT7        40067
#define ID_DISK_SLOT8        40068
#define ID_DISK_SLOT9        40069
#define ID_DISK_SLOT_FIRST   ID_DISK_SLOT1
#define ID_DISK_SLOT_LAST    ID_DISK_SLOT9
#define ID_DISK_SLOTS        40070

// Settings dialog controls.
#define IDC_MODE_BRAILLE     1001
#define IDC_MODE_PC          1002
#define IDC_DIAGNOSTICS      1003

// About dialog controls.
#define IDC_ABOUT_TEXT       1010

// New diskette dialog controls.  1030 was IDC_NEW_FOLDER, the "from an
// existing folder" radio, which duplicated ID_DISK_INSERT; the number is left
// unused rather than reassigned, so a stale .res cannot land on it.
#define IDC_NEW_EMPTYFOLDER  1031
#define IDC_NEW_UNSAVED      1032
#define IDC_NEW_UNFORMATTED  1033
#define IDC_NEW_PATH         1034
#define IDC_NEW_BROWSE       1035
#define IDC_NEW_SLOT         1036

// Slots dialog controls.
#define IDC_SLOT_LIST        1020
#define IDC_SLOT_ASSIGN      1021
#define IDC_SLOT_CURRENT     1022
#define IDC_SLOT_CLEAR       1023
#define IDC_SLOT_LOCK        1024
#define IDC_SLOT_NEWUNSAVED  1025

// Labels and group boxes need no id of their own.
#ifndef IDC_STATIC
#define IDC_STATIC           -1
#endif

#endif
