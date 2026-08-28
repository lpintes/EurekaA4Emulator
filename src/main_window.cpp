#include "main_window.h"

#include "dialogs.h"
#include "host_console.h"
#include "res/resource.h"
#include "win/dialog.h"

namespace {

constexpr wchar_t kClassName[] = L"EurekaA4EmulatorWindow";

// Read from another process by the NVDA add-on in nvda-addon/, which sleeps the
// screen reader over this window while Eureka owns the keyboard and wakes it
// again when Shift+F11 hands the keyboard back.  Two things about the choice:
//
// A window property rather than a named event or a mutex, because the state
// belongs to *this* window -- two emulators running at once each answer for
// themselves, which a process-wide name cannot do.
//
// Absent is a valid answer and means "not released": an emulator built before
// this existed then reads as Eureka owning the keyboard, which is the state it
// is almost always in.
constexpr wchar_t kKeyboardReleasedProp[] = L"EurekaA4.KeyboardReleased";

// A mode nobody can hear is the trap this machine keeps setting: a keyboard
// that goes nowhere and a keyboard that is broken are both silence.  So every
// change of where the keys go says so out loud, and says it in the host's own
// voice -- Beep() goes to the default device, not through the emulated DAC, so
// it cannot be mistaken for Eureka speaking.
//
// Beep() is synchronous.  That is affordable now and would not have been
// before: the machine is on its own thread, so a tenth of a second spent here
// costs the guest nothing.
void Tone(unsigned hz, unsigned ms) { Beep(hz, ms); }

// Falling: the keys are leaving Eureka.  Rising: they are coming back.
void ToneLeaving() { Tone(880, 70); Tone(440, 90); }
void ToneReturning() { Tone(440, 70); Tone(880, 90); }
void ToneArmed() { Tone(1200, 50); }
void ToneDisarmed() { Tone(600, 50); }


// Kept in one place so the menu, the help text and this list cannot drift
// apart.  A shortcut a screen reader never reads is a shortcut nobody has.
constexpr wchar_t kShortcutHelp[] =
    L"Skratky emulátora (patria oknu, do Eureky sa neposielajú):\r\n"
    L"\r\n"
    L"F12 — otvorí ponuku. Alt ani F10 to nerobia, tie patria Eureke.\r\n"
    L"      Pozor, F11 aj F12 sú na klávesnici PC platné klávesy Eureky\r\n"
    L"      (CAh a CBh) a okno jej ich berie. Vráti ich ponuka Klávesnica →\r\n"
    L"      Poslať Eureke kláves; F11 je ROM operačného systému.\r\n"
    L"F11 — nasledujúci kláves nepôjde do Eureky, ale do Windows.\r\n"
    L"      Ozve sa vysoký tón, keď je nachystaný, a nižší, keď sa minie.\r\n"
    L"      Hodí sa napríklad na Alt+medzerník, ponuku okna.\r\n"
    L"Shift+F11 — uvoľní klávesnicu úplne: do Eureky nejde nič, okno sa\r\n"
    L"      správa ako hociktoré iné okno Windows. Klesajúca dvojica tónov\r\n"
    L"      znamená, že klávesy Eureku opúšťajú, stúpajúca že sa vracajú.\r\n"
    L"      Kým to platí, je to napísané aj v titulku okna.\r\n"
    L"Ctrl+K — prepne klávesnicu medzi braillovskou a externou PC.\r\n"
    L"Ctrl+Shift+R — reset.\r\n"
    L"Ctrl+Shift+V — vypne Eureku tak, ako to robí ona sama.\r\n"
    L"Ctrl+Shift+U — uloží disketu do priečinka.\r\n"
    L"Ctrl+Shift+D — výpis diagnostiky na konzolu.\r\n"
    L"Ctrl+Shift+N — nastavenia.\r\n"
    L"Ctrl+Shift+H — toto okno.\r\n"
    L"Ctrl+Shift+Q — uloží disketu a skončí.\r\n"
    L"      Alt+F4 to už nerobí, ten patrí Eureke. Keď ho potrebujete pre\r\n"
    L"      Windows, stlačte najprv F11.\r\n"
    L"\r\n"
    L"Všetko ostatné ide do Eureky:\r\n"
    L"\r\n"
    L"F1 až F10 a kurzorové klávesy vrátane Shiftu a Altu.\r\n"
    L"\r\n"
    L"Alt+F1 až Alt+F10 je rada, ktorá funkciu len pomenuje a nespustí ju:\r\n"
    L"Alt+F4 povie „komunikace“, samotné F4 do komunikácie vojde. Takto sa\r\n"
    L"dá prejsť, čo kde je.\r\n"
    L"\r\n"
    L"Na klávesnici PC rada pokračuje: F11 je ROM operačného systému\r\n"
    L"(Alt+F11 povie „data ROMu“), F12 je nepoužité. Tie dva klávesy si\r\n"
    L"berie okno a posiela ich ponuka Klávesnica.\r\n"
    L"\r\n"
    L"F11 má aj braillovská klávesnica: je to „d-akord“, medzerník a body\r\n"
    L"1, 4, 5, takže sa dá stlačiť aj priamo bodmi. F12 na nej akord nemá\r\n"
    L"a Alt je na nej medzerník, ktorý akord už používa — v braillovskom\r\n"
    L"režime je preto z tých štyroch položiek aktívne len F11.\r\n"
    L"\r\n"
    L"F9 je režim, F10 povie, kde ste; Shift+F9 stav batérie, Shift+F10\r\n"
    L"sebekontrolu, Shift+F7 spustí program z disku.\r\n"
    L"\r\n"
    L"V braillovskom režime sú F D S body 1 2 3, J K L body 4 5 6\r\n"
    L"a medzerník je medzerník. Shift robí veľké písmeno a so samotným\r\n"
    L"medzerníkom je Escape — kláves Esc stlačí presne ten akord, takže\r\n"
    L"funguje aj on. Písmená sa nepíšu, píše sa bodmi, tak ako na stroji.\r\n"
    L"\r\n"
    L"Samotný Shift zastaví reč, tak ako na stroji: takto sa pozastavuje\r\n"
    L"plynulé čítanie v textovom procesore.\r\n"
    L"\r\n"
    L"Eureku vypnete aj tak ako naozaj: v hlavnom menu podržte všetky\r\n"
    L"štyri kurzorové klávesy naraz. Vypne sa aj sama po piatich minútach\r\n"
    L"nečinnosti, tridsať sekúnd vopred to ohlási tónmi.\r\n"
    L"\r\n"
    L"NVDA:\r\n"
    L"\r\n"
    L"S doplnkom z priečinka nvda-addon mlčí NVDA nad týmto oknom, kým\r\n"
    L"klávesnicu vlastní Eureka, ale v ponuke, v dialógoch aj po Shift+F11\r\n"
    L"číta ako inde. NVDA+T povie titulok aj počas spánku a NVDA+Shift+S\r\n"
    L"zostáva ako núdzová brzda. Kláves NVDA (Insert) si čítačka necháva\r\n"
    L"aj v spánku; do Eureky ho pošlete dvoma rýchlymi stlačeniami za sebou.";

}  // namespace

MainWindow::MainWindow(EmulatorThread& emulator, std::wstring romPath,
                       std::wstring diskDescription)
    : emulator_(emulator),
      romPath_(std::move(romPath)),
      diskDescription_(std::move(diskDescription)) {}

bool MainWindow::Create() {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  HMENU menu = LoadMenuW(instance, MAKEINTRESOURCEW(IDR_MAIN_MENU));
  accelerators_ = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDR_ACCELERATORS));
  // Resizable on purpose.  There is nothing in the client area to lay out, but
  // a fixed window cannot be maximised or snapped, and that is something a
  // screen reader user does as much as anyone.
  if (!win::Window::Create(kClassName, L"Eureka A4", WS_OVERLAPPEDWINDOW, 520,
                           260, menu))
    return false;
  RegisterCommands();
  PublishKeyboardState();
  RefreshTitle();
  return true;
}

void MainWindow::PublishKeyboardState() const {
  if (!hwnd_) return;
  SetPropW(hwnd_, kKeyboardReleasedProp,
           reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(released_ ? 1 : 0)));
}

void MainWindow::RegisterCommands() {
  OnCommand(ID_FILE_EXIT, [this] { PostMessageW(hwnd_, WM_CLOSE, 0, 0); });
  OnCommand(ID_MACHINE_RESET, [this] { emulator_.PostReset(); });
  OnCommand(ID_MACHINE_POWEROFF, [this] { emulator_.PostPowerOff(); });
  OnCommand(ID_KEYBOARD_BRAILLE,
            [this] { emulator_.PostSetMode(InputMode::kBraille); });
  OnCommand(ID_KEYBOARD_PC,
            [this] { emulator_.PostSetMode(InputMode::kPc); });
  OnCommand(ID_KEYBOARD_TOGGLE, [this] { emulator_.PostToggleMode(); });
  OnCommand(ID_KEYBOARD_RELEASE, [this] { SetReleased(!released_); });
  // Pressing F11 again changes your mind rather than arming it twice.
  OnCommand(ID_KEYBOARD_PASSONCE, [this] { SetPassOnce(!passOnce_); });
  OnCommand(ID_KEYBOARD_SEND_F11, [this] { SendGuestKey(VK_F11, false); });
  OnCommand(ID_KEYBOARD_SEND_AF11, [this] { SendGuestKey(VK_F11, true); });
  OnCommand(ID_KEYBOARD_SEND_F12, [this] { SendGuestKey(VK_F12, false); });
  OnCommand(ID_KEYBOARD_SEND_AF12, [this] { SendGuestKey(VK_F12, true); });
  OnCommand(ID_TOOLS_DIAGDUMP, [this] { emulator_.PostDumpDiagnostics(); });

  OnCommand(ID_ACTIVATE_MENU, [this] {
    // What Alt and F10 would do, on a key the guest has no use for.  Posted
    // rather than sent, so the menu loop does not start inside the accelerator
    // dispatch that asked for it.
    PostMessageW(hwnd_, WM_SYSCOMMAND, SC_KEYMENU, 0);
  });

  OnCommand(ID_FILE_EXPORT, [this] {
    const std::wstring folder = win::PickFolder(
        hwnd_, L"Vyberte priečinok, do ktorého sa disketa uloží");
    // Cancelling is an answer, not an error.
    if (!folder.empty()) emulator_.PostExportDisk(folder);
  });

  OnCommand(ID_TOOLS_SETTINGS, [this] {
    SettingsDialog dialog(emulator_.mode(), emulator_.diagnostics());
    if (dialog.ShowModal(hwnd_, IDD_SETTINGS) != IDOK) return;
    // Turning diagnostics on is the request for somewhere to read them: this
    // program has no console until something asks for one.  Done here, on the
    // window thread, because the worker must not open windows.
    if (dialog.diagnostics() && !emulator_.diagnostics()) host::OpenConsole();
    // Posted unconditionally; the worker ignores a mode it is already in.
    emulator_.PostSetMode(dialog.mode());
    emulator_.PostSetDiagnostics(dialog.diagnostics());
  });

  OnCommand(ID_HELP_KEYS, [this] {
    MessageBoxW(hwnd_, kShortcutHelp, L"Klávesové skratky",
                MB_OK | MB_ICONINFORMATION);
  });

  OnCommand(ID_HELP_ABOUT, [this] {
    AboutDialog dialog(AboutText());
    dialog.ShowModal(hwnd_, IDD_ABOUT);
  });
}

std::wstring MainWindow::AboutText() const {
  return L"Eureka A4 Emulator\r\n"
         L"\r\n"
         L"Emulátor osobného počítača Robotron Eureka A4 pre nevidiacich.\r\n"
         L"Procesor Hitachi HD64180 na 6,144 MHz, zvuk 48 kHz.\r\n"
         L"\r\n"
         L"ROM: " + romPath_ + L"\r\n" +
         L"Disk: " + diskDescription_ + L"\r\n" +
         L"Klávesnica: " + ModeName(emulator_.mode()) + L"\r\n" +
         L"Diagnostika: " + (emulator_.diagnostics() ? L"zapnutá" : L"vypnutá") +
         L"\r\n";
}

void MainWindow::RefreshTitle() const {
  // The state lives in the title because that is the one thing a screen reader
  // can be asked for at any moment (NVDA+T) and announces on Alt+Tab, without
  // there being anything on screen to explore.  The tones say when it changed;
  // the title answers "what is it now" at any time afterwards.
  // The mode stays in the title even while released, because "which keyboard
  // does it come back as" is a fair question to ask at that moment.
  SetTitle(released_
               ? L"Eureka A4 — klávesnica uvoľnená, vráti ju Shift+F11 — "
                 L"režim: " + std::wstring(ModeName(emulator_.mode())) +
                     L" — disk: " + diskDescription_
               : L"Eureka A4 — klávesnica: " +
                     std::wstring(ModeName(emulator_.mode())) + L" — disk: " +
                     diskDescription_);
}

void MainWindow::RefreshMenu() const {
  const HMENU menu = GetMenu(hwnd_);
  if (!menu) return;
  const InputMode mode = emulator_.mode();
  CheckMenuRadioItem(menu, ID_KEYBOARD_BRAILLE, ID_KEYBOARD_PC,
                     mode == InputMode::kBraille ? ID_KEYBOARD_BRAILLE
                                                 : ID_KEYBOARD_PC,
                     MF_BYCOMMAND);
  // The mode is deliberately *not* greyed out while the keyboard is released.
  // Choosing which keyboard it comes back as is a real thing to want, Ctrl+K
  // goes on working through the accelerator table anyway, and a greyed item
  // beside a working shortcut says two different things.
  CheckMenuItem(menu, ID_KEYBOARD_RELEASE,
                MF_BYCOMMAND | (released_ ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(menu, ID_KEYBOARD_PASSONCE,
                MF_BYCOMMAND | (passOnce_ ? MF_CHECKED : MF_UNCHECKED));
  EnableMenuItem(menu, ID_KEYBOARD_PASSONCE,
                 MF_BYCOMMAND | (released_ ? MF_GRAYED : MF_ENABLED));
  // F11 is the one of the four the braille keyboard can also make: it is the
  // "d" chord, space plus dots 1,4,5 (9Ch), measured to say the same "ROM
  // operacniho systemu" as scan code 57h.  The other three it cannot -- F12
  // has no chord, and Alt means the space bar, which the chord is already
  // using -- so on the twenty keys they would go nowhere and go there in
  // silence.  Greying says it out loud enough for a screen reader to read.
  const UINT pcOnly =
      MF_BYCOMMAND | (mode == InputMode::kPc ? MF_ENABLED : MF_GRAYED);
  for (UINT id : {ID_KEYBOARD_SEND_AF11, ID_KEYBOARD_SEND_F12,
                  ID_KEYBOARD_SEND_AF12})
    EnableMenuItem(menu, id, pcOnly);
  EnableMenuItem(menu, ID_KEYBOARD_SEND_F11, MF_BYCOMMAND | MF_ENABLED);
}

void MainWindow::SetReleased(bool released) {
  if (released_ == released) return;
  released_ = released;
  // Arming one and then the other would leave the one-shot armed behind the
  // sticky mode, where nothing can spend it.
  passOnce_ = false;
  // Whatever the guest believes is held has to go up now: no release for it is
  // ever coming while the keyboard is somewhere else.
  emulator_.PostFocusLost();
  // Before the title, so that a screen reader woken by the property reads a
  // title that already says the same thing.
  PublishKeyboardState();
  RefreshTitle();
  if (released_) ToneLeaving(); else ToneReturning();
}

// Deliberately not published to the NVDA add-on, unlike the released state.
// It looks as though it should be -- for one key the host owns the keyboard --
// but a woken screen reader would then eat that very key as one of its own
// commands, and the one-shot, which is only ever spent by a key this window
// sees, would stay armed for good.  That is the sticky silent mode this file
// already got caught by once; see HostKeepsKey.
void MainWindow::SetPassOnce(bool armed) {
  // Meaningless while the keyboard is released -- every key already goes to
  // Windows -- and saying so beats arming something that can never fire.
  if (released_) return;
  if (passOnce_ == armed) return;
  passOnce_ = armed;
  passOnceUsed_ = false;
  if (passOnce_) ToneArmed(); else ToneDisarmed();
}

bool MainWindow::HostKeepsKey(WPARAM virtualKey, bool down) {
  if (released_) return true;
  if (!passOnce_) return false;
  if (down) {
    passOnceUsed_ = true;
    return true;
  }
  // Spent on the first release whose press was actually seen here.
  //
  // The guard is not defensive tidiness, it is the whole thing: F11 arms this
  // from the accelerator table, so TranslateAccelerator eats the F11 *press*
  // and the F11 *release* is not an accelerator and arrives a moment later.
  // Without the guard the key being armed disarms itself -- measured, F11 then
  // Alt did nothing at all.
  //
  // Modifiers count, and that is deliberate: Alt on its own is a complete act
  // here, it opens the menu on its own release.  An earlier rule excused
  // modifiers from spending the one-shot and the result was that Alt never
  // spent it, so a single F11 armed the pass-through for good -- a sticky mode
  // nobody asked for and, worse, a silent one.
  //
  // A modifier still held when the key goes up is released afterwards and that
  // release does reach the guest, the same way every accelerator's does.  It
  // is a break code for a make the machine never saw, which its decoder
  // ignores.
  if (passOnceUsed_) {
    passOnce_ = false;
    passOnceUsed_ = false;
    ToneDisarmed();
  }
  return true;
}

void MainWindow::ForwardKey(bool down, WPARAM wParam, LPARAM lParam) const {
  emulator_.PostKey(KeyEventFromMessage(down, wParam, lParam));
}

// Hands the guest a key the accelerator table took from it.  F11 and F12 are
// the machine's own CAh and CBh (1DF05), so the host owning them costs the
// guest Alt+F11, its "data ROM"; the menu is the way back and costs no key at
// all.  Press and release both, because nobody is going to let go of a menu
// item: the release matters to the ROM, which repeats a key that stays down.
//
// Not gated on released_.  Choosing this from the menu is asking for it in so
// many words, and it is not the keyboard going anywhere -- it is one key,
// handed over once, which is the whole point of the item.
void MainWindow::SendGuestKey(WORD virtualKey, bool alt) const {
  emulator_.PostKey(SyntheticKey(true, virtualKey, alt));
  emulator_.PostKey(SyntheticKey(false, virtualKey, alt));
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_KEYDOWN:
    case WM_KEYUP:
      // Breaking out hands the key to DefWindowProc, which is what "handled by
      // Windows normally" means for a window with no controls of its own:
      // Alt and F10 open the menu, Alt+Space opens the system menu.
      if (HostKeepsKey(wParam, message == WM_KEYDOWN)) break;
      ForwardKey(message == WM_KEYDOWN, wParam, lParam);
      return 0;

    // Alt is a modifier on this machine's keyboard, so Windows must not take
    // it to mean "open the menu"; F12 does that instead.  Everything Alt is
    // part of therefore arrives here and is forwarded like any other key --
    // unless the keyboard has been let go of, and then Alt means what it means
    // everywhere else.
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
      // Alt+F4 goes to the guest like every other Alt combination.  It was
      // kept for Windows at first, reasoning that the guest could reach F4
      // without Alt anyway -- which is wrong.  Alt is a key of the machine's
      // own here (SpecialKey sets bit 20h), so Alt+F4 is E3h, the Eureka's
      // "komunikace", and Alt+F1..Alt+F10 is one continuous row of functions
      // the user walks along to find out what is where.  A hole in the middle
      // of that row which kills the emulator costs more than the standard
      // close does: Ctrl+Shift+Q is an accelerator and works in every state,
      // and F11 or Shift+F11 hands Alt+F4 back to Windows for as long as it
      // is wanted.
      if (HostKeepsKey(wParam, message == WM_SYSKEYDOWN)) break;
      ForwardKey(message == WM_SYSKEYDOWN, wParam, lParam);
      return 0;

    // Swallowed so that an Alt combination does not end in the system beep --
    // but not while the host owns the keyboard, because that is where the menu
    // mnemonics come from.
    case WM_SYSCHAR:
    case WM_CHAR:
      if (released_ || passOnce_) break;
      return 0;

    case WM_KILLFOCUS:
      // The releases of whatever is held will be delivered to whoever took the
      // focus, so tell the machine to let go now rather than let a key sit
      // down for good.
      emulator_.PostFocusLost();
      // An armed one-shot would otherwise be waiting on the way back, long
      // after the user forgot about it.  The sticky release is a deliberate
      // state and survives; this is not.
      SetPassOnce(false);
      return 0;

    case WM_INITMENUPOPUP:
      RefreshMenu();
      break;

    case WM_NCDESTROY:
      // Windows keeps the property's atom alive until it is removed, so a
      // window that dies with it still set leaks it for the rest of the
      // process.  Nothing else here needs the message, so it goes on to the
      // base class as usual.
      RemovePropW(hwnd_, kKeyboardReleasedProp);
      break;

    case WM_EMU_STATE:
      RefreshTitle();
      return 0;

    case WM_EMU_POWERED_OFF:
      // C45Ah is FFh here: the firmware's own marker that this was a clean
      // power-down, read back at 180CB so the machine resumes where the user
      // was instead of initialising.  Once the host keeps RAM across runs, it
      // is this byte that makes the difference between switching on and a
      // hard reset.
      MessageBoxW(hwnd_,
                  L"Eureka sa vypla.\r\n\r\n"
                  L"Na skutočnom stroji by RAM aj hodiny zostali pod napätím "
                  L"a ďalšie zapnutie by pokračovalo tam, kde ste skončili.",
                  L"Eureka A4", MB_OK | MB_ICONINFORMATION);
      PostMessageW(hwnd_, WM_CLOSE, 0, 0);
      return 0;

    case WM_EMU_DISK_ERROR:
      MessageBoxW(hwnd_,
                  (L"Chyba pri ukladaní disku:\r\n\r\n" +
                   emulator_.TakeDiskError()).c_str(),
                  L"Eureka A4", MB_OK | MB_ICONERROR);
      PostMessageW(hwnd_, WM_CLOSE, 0, 0);
      return 0;

    case WM_EMU_NO_AUDIO:
      MessageBoxW(hwnd_,
                  L"Zvukové zariadenie sa nepodarilo otvoriť.\r\n\r\n"
                  L"Eureka beží, ale nebude ju počuť — a reč je na tomto "
                  L"stroji celé používateľské rozhranie, nie doplnok.",
                  L"Eureka A4", MB_OK | MB_ICONWARNING);
      return 0;

    case WM_EMU_EXPORT_DONE: {
      bool ok = false;
      const std::wstring detail = emulator_.TakeExportResult(ok);
      MessageBoxW(hwnd_,
                  ok ? (L"Disketa bola uložená do:\r\n" + detail).c_str()
                     : (L"Disketu sa nepodarilo uložiť:\r\n" + detail).c_str(),
                  L"Eureka A4",
                  MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
      return 0;
    }

    default:
      break;
  }
  return win::Window::HandleMessage(message, wParam, lParam);
}
