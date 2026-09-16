#include "main_window.h"

#include <filesystem>
#include <system_error>
#include <vector>

#include "dialogs.h"
#include "disk_split.h"
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

// The two slider positions, raw from sliders.h, for the same add-on.  It sees
// Ctrl or Alt with an arrow go past, looks again a moment later and says the
// new position -- the only way to hear a step the machine will not voice until
// its next sentence, and for the volume perhaps not at all.  A key that went to
// Eureka instead moves nothing, so the add-on stays quiet without having to
// know about F11.  Absent reads as 0 and never changes, so against an older
// emulator the add-on simply announces nothing.
constexpr wchar_t kSpeechRateProp[] = L"EurekaA4.SpeechRate";
constexpr wchar_t kVolumeProp[] = L"EurekaA4.Volume";

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

// A diskette going in and coming out.  Deliberately a different shape from the
// keyboard pair above -- two short notes rather than two long ones -- because
// these two things happen in the same window and telling them apart by ear is
// the whole point of having a sound at all.  A real drive is audible; this one
// would otherwise change under the user in silence.
void ToneInserted() { Tone(660, 45); Tone(990, 45); }
void ToneEjected() { Tone(330, 90); }

// The write protect notch.  A third shape again: one long note, low for
// locked and high for unlocked, so it is neither the keyboard's pair nor the
// diskette's.  The state also stands in the title, which answers at any time;
// the tone answers at the moment it changed.
void ToneLocked() { Tone(400, 140); }
void ToneUnlocked() { Tone(1000, 140); }

// The power switch.  A fourth shape, and the only three-note one: the machine
// says "konec" in its own voice on the way out, but what follows that is
// silence, and silence is also what a crashed emulator sounds like.  These say
// the host is still there and knows which state it is in.  Three notes down
// for off, three up for on.
void TonePoweredOff() { Tone(700, 100); Tone(500, 100); Tone(300, 150); }
void TonePoweredOn() { Tone(300, 100); Tone(500, 100); Tone(700, 150); }

// A slider at the end of its travel.  One short note, higher than anything
// above, so it cannot be taken for the one-shot being spent -- which, after
// F11, sounds in the same breath.
void ToneSliderEnd() { Tone(1600, 60); }


// Kept in one place so the menu, the help text and this list cannot drift
// apart.  A shortcut a screen reader never reads is a shortcut nobody has.
constexpr wchar_t kShortcutHelp[] =
    L"Okno berie Eureke tri klávesy a nič viac:\r\n"
    L"\r\n"
    L"F12 — otvorí ponuku. Alt ani F10 to nerobia, tie patria Eureke.\r\n"
    L"      Pozor, F11 aj F12 sú na externej klávesnici platné klávesy\r\n"
    L"      Eureky (CAh a CBh) a okno jej ich berie. Vráti ich ponuka\r\n"
    L"      Klávesnica → Poslať Eureke kláves; F11 je ROM operačného\r\n"
    L"      systému.\r\n"
    L"F11 — nasledujúci kláves nepôjde do Eureky, ale do Windows.\r\n"
    L"      Ozve sa vysoký tón, keď je nachystaný, a nižší, keď sa minie.\r\n"
    L"      Hodí sa napríklad na Alt+medzerník, ponuku okna.\r\n"
    L"Shift+F11 — uvoľní klávesnicu úplne: do Eureky nejde nič, okno sa\r\n"
    L"      správa ako hociktoré iné okno Windows. Klesajúca dvojica tónov\r\n"
    L"      znamená, že klávesy Eureku opúšťajú, stúpajúca že sa vracajú.\r\n"
    L"      Kým to platí, je to napísané aj v titulku okna.\r\n"
    L"\r\n"
    L"Ostatné skratky sú dvojhmatové: najprv F11, potom Ctrl alebo Alt\r\n"
    L"s ďalším klávesom.\r\n"
    L"Bez F11 idú tie klávesy Eureke, takže jej okno neberie ani jeden.\r\n"
    L"Po Shift+F11 platia rovno, bez F11, lebo vtedy je klávesnica hosťova\r\n"
    L"aj tak. Všetky sú aj v ponuke a tá ich nepotrebuje.\r\n"
    L"\r\n"
    L"F11, Ctrl+K — prepne klávesnicu medzi braillovskou a externou.\r\n"
    L"F11, Ctrl+R — reset: Eureka začne odznova, ako po prepnutí vypínača\r\n"
    L"      batérie — pamäť aj hodiny sú prázdne.\r\n"
    L"F11, Ctrl+V — vypne Eureku tak, ako to robí ona sama.\r\n"
    L"F11, Ctrl+P — teplý reset: Eureka sa vráti do hlavného menu a pamäť\r\n"
    L"      aj hodiny zostanú, takže napísaný text sa nestratí. Funguje aj\r\n"
    L"      vtedy, keď Eureka zamrzne, a vypnutú Eureku ním zapnete. Na\r\n"
    L"      stroji to bol akord bod 3, F1 a šípka hore, podržaný asi sekundu.\r\n"
    L"F11, Ctrl+šípka vpravo a vľavo — rýchlejšia a pomalšia reč. Je to\r\n"
    L"      ľavý posuvník Eureky: reč sa zrýchli a zároveň zvýši, tak ako\r\n"
    L"      na stroji.\r\n"
    L"F11, Alt+šípka vpravo a vľavo — hlasnejšie a tichšie, pravý\r\n"
    L"      posuvník. Na konci posuvníka sa ozve krátke vysoké pípnutie.\r\n"
    L"      Oba posuvníky naraz nastavíte v ponuke Stroj → Nastaviť\r\n"
    L"      posuvníky a emulátor si ich polohu pamätá.\r\n"
    L"F11, Ctrl+I — vloží disketu z iného priečinka. Vymieňať sa dá za\r\n"
    L"      behu: EurekaDOS si nový disk prihlási sám, tak ako skutočný\r\n"
    L"      stroj.\r\n"
    L"F11, Ctrl+M — vloží novú disketu: uloženú v novom priečinku, alebo\r\n"
    L"      neuloženú. Neuložená sa dá vyrobiť aj nenaformátovaná — Eureka ju\r\n"
    L"      ohlási ako vadný disk, kým ju Shift+F8 nenaformátuje.\r\n"
    L"F11, Ctrl+1 až Ctrl+9 — vloží disketu zo slotu. Čo je v ktorom slote,\r\n"
    L"      je napísané priamo v ponuke Disketa; priraďuje sa tam v položke\r\n"
    L"      Spravovať sloty.\r\n"
    L"F11, Ctrl+0 — vysunie disketu, teda nechá mechaniku prázdnu.\r\n"
    L"F11, Ctrl+Z — zamkne disketu proti zápisu, alebo zámok zruší. Je to\r\n"
    L"      prelepená dierka na diskete: Eureka z nej číta a spúšťa programy\r\n"
    L"      ako inokedy, ale zápis aj formátovanie odmietne slovami „disk je\r\n"
    L"      chráněn proti zápisu“. Nízky tón znamená zamknuté, vysoký\r\n"
    L"      odomknuté, a kým zámok platí, stojí v titulku okna.\r\n"
    L"      Zámok drží tá disketa, nie mechanika: vydrží vysunutie aj výmenu\r\n"
    L"      a zruší ho až toto isté Ctrl+Z. Preto funguje hromadné\r\n"
    L"      kopírovanie, pri ktorom Eureka žiada chránený zdroj a strieda\r\n"
    L"      zdrojovú disketu s cieľovou. Zamknúť sa dá každá disketa vrátane\r\n"
    L"      neuloženej; ukončenie emulátora prežije len zámok diskety, ktorá\r\n"
    L"      má priečinok — zapisuje sa k jeho ceste. Zámok diskety, čo leží\r\n"
    L"      v slote, sa nastavuje v Spravovať sloty.\r\n"
    L"F11, Ctrl+U — uloží disketu do priečinka a ten priečinok je odvtedy\r\n"
    L"      jej: zapisuje sa doň sama a v titulku ju už nájdete pod jeho\r\n"
    L"      menom, nie ako „neuložená“. Priečinok nemusí existovať, stačí ho\r\n"
    L"      v dialógu pomenovať a vytvorí sa. Disketa, ktorá priečinok už\r\n"
    L"      mala, sa takto presťahuje — starý si ponechá to, čo v ňom bolo.\r\n"
    L"F11, Ctrl+D — výpis diagnostiky na konzolu.\r\n"
    L"F11, Ctrl+N — nastavenia.\r\n"
    L"F11, Ctrl+H — toto okno.\r\n"
    L"F11, Ctrl+Q — uloží disketu a skončí. Ak stroj beží a je zapnuté\r\n"
    L"      zachovanie pamäte, najprv sa spýta, či zavrieť bez uloženia\r\n"
    L"      stavu — vypnite ju cez Ctrl+V, ak chcete nadviazať.\r\n"
    L"      Alt+F4 to už nerobí, ten patrí Eureke. Keď ho potrebujete pre\r\n"
    L"      Windows, stlačte najprv F11, tak ako tu.\r\n"
    L"      Cesta von, ktorú Eureke nikdy neberie, je F12 → Súbor → Skončiť.\r\n"
    L"\r\n"
    L"Všetko ostatné ide do Eureky:\r\n"
    L"\r\n"
    L"F1 až F10 a kurzorové klávesy vrátane Shiftu a Altu.\r\n"
    L"\r\n"
    L"Alt+F1 až Alt+F10 je rada, ktorá funkciu len pomenuje a nespustí ju:\r\n"
    L"Alt+F4 povie „komunikace“, samotné F4 do komunikácie vojde. Takto sa\r\n"
    L"dá prejsť, čo kde je.\r\n"
    L"\r\n"
    L"Na externej klávesnici rada pokračuje: F11 je ROM operačného systému\r\n"
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
    L"funguje aj on. Enter stlačí F8 a Backspace F6 — stroj tie dva\r\n"
    L"klávesy nemá a ich prácu na ňom robia práve F8 a F6. Písmená sa\r\n"
    L"nepíšu, píše sa bodmi, tak ako na stroji.\r\n"
    L"\r\n"
    L"Samotný Shift zastaví reč, tak ako na stroji: takto sa pozastavuje\r\n"
    L"plynulé čítanie v textovom procesore.\r\n"
    L"\r\n"
    L"Ťuknutie na Ctrl zastaví reč aj skladbu v hudobnom editore, na\r\n"
    L"externej aj na braillovskej klávesnici.\r\n"
    L"\r\n"
    L"Eureku vypnete aj tak ako naozaj: v hlavnom menu podržte všetky\r\n"
    L"štyri kurzorové klávesy naraz. Vypne sa aj sama po piatich minútach\r\n"
    L"nečinnosti, tridsať sekúnd vopred to ohlási tónmi.\r\n"
    L"\r\n"
    L"Vypnutá zostane v okne, tromi klesajúcimi tónmi a v titulku.\r\n"
    L"Klávesnicu vtedy dostane Windows a Shift+F11 ju nemá komu vrátiť.\r\n"
    L"Späť ju zapne samotné Ctrl+R a klávesnicu dostane zase Eureka.\r\n"
    L"Stroj však začne odznova.\r\n"
    L"\r\n"
    L"NVDA:\r\n"
    L"\r\n"
    L"S doplnkom z priečinka nvda-addon mlčí NVDA nad týmto oknom, kým\r\n"
    L"klávesnicu vlastní Eureka, ale v ponuke, v dialógoch aj po Shift+F11\r\n"
    L"číta ako inde. NVDA+T povie titulok aj počas spánku a NVDA+Shift+S\r\n"
    L"zostáva ako núdzová brzda. Kláves NVDA (Insert) si čítačka necháva\r\n"
    L"aj v spánku; do Eureky ho pošlete dvoma rýchlymi stlačeniami za sebou.";

}  // namespace

MainWindow::MainWindow(EmulatorThread& emulator, Settings& settings,
                       std::wstring romPath, DiskState disk)
    : emulator_(emulator),
      settings_(settings),
      romPath_(std::move(romPath)),
      disk_(std::move(disk)) {}

bool MainWindow::Create() {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  HMENU menu = LoadMenuW(instance, MAKEINTRESOURCEW(IDR_MAIN_MENU));
  accelerators_ = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDR_ACCELERATORS));
  hostAccelerators_ =
      LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDR_ACCELERATORS_HOST));
  // Resizable on purpose.  There is nothing in the client area to lay out, but
  // a fixed window cannot be maximised or snapped, and that is something a
  // screen reader user does as much as anyone.
  if (!win::Window::Create(kClassName, L"Eureka A4", WS_OVERLAPPEDWINDOW, 520,
                           260, menu))
    return false;
  RegisterCommands();
  PublishKeyboardState();
  PublishSliders();
  RefreshTitle();
  return true;
}

void MainWindow::PublishKeyboardState() const {
  if (!hwnd_) return;
  SetPropW(hwnd_, kKeyboardReleasedProp,
           reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(released_ ? 1 : 0)));
}

void MainWindow::PublishSliders() const {
  if (!hwnd_) return;
  SetPropW(hwnd_, kSpeechRateProp,
           reinterpret_cast<HANDLE>(
               static_cast<UINT_PTR>(settings_.speech_rate())));
  SetPropW(hwnd_, kVolumeProp,
           reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(settings_.volume())));
}

void MainWindow::RegisterCommands() {
  OnCommand(ID_FILE_EXIT, [this] { PostMessageW(hwnd_, WM_CLOSE, 0, 0); });
  OnCommand(ID_MACHINE_RESET, [this] { emulator_.PostReset(); });
  OnCommand(ID_MACHINE_POWERON, [this] { emulator_.PostPowerOn(); });
  OnCommand(ID_MACHINE_POWEROFF, [this] { emulator_.PostPowerOff(); });
  // One step per press.  A step that lands on either end of the travel, or
  // finds the slider already there, says so: without it the only way to find
  // the end is to go on pressing and hear nothing change.
  const auto step = [this](int rateDelta, int volumeDelta) {
    SetSliders(settings_.speech_rate() + rateDelta,
               settings_.volume() + volumeDelta);
    const int rate = settings_.speech_rate();
    const int volume = settings_.volume();
    const bool rateEnd =
        rateDelta != 0 && (rate == 0 || rate == sliders::kRatePositions - 1);
    const bool volumeEnd =
        volumeDelta != 0 &&
        (volume == 0 || volume == sliders::kVolumePositions - 1);
    if (rateEnd || volumeEnd) ToneSliderEnd();
  };
  OnCommand(ID_MACHINE_FASTER, [step] { step(+1, 0); });
  OnCommand(ID_MACHINE_SLOWER, [step] { step(-1, 0); });
  OnCommand(ID_MACHINE_LOUDER, [step] { step(0, +1); });
  OnCommand(ID_MACHINE_QUIETER, [step] { step(0, -1); });
  OnCommand(ID_MACHINE_SLIDERS, [this] {
    SlidersDialog dialog(settings_.speech_rate(), settings_.volume());
    if (dialog.ShowModal(hwnd_, IDD_SLIDERS) != IDOK) return;
    SetSliders(dialog.speech_rate(), dialog.volume());
  });
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
  OnCommand(ID_TOOLS_DIAGDUMP, [this] {
    // Decided here rather than on the worker for the same reason the settings
    // dialog opens the console here: the worker must not open windows, and a
    // MessageBox on that thread runs its own message loop -- the machine, and
    // with it the sound that is this program's whole interface, would stand
    // still for as long as the box was up.
    //
    // Off is not an error, but it is the answer to a request the user made by
    // name, so it has to be somewhere they will find it.  It used to go to
    // host::Print and therefore into a console that does not exist yet, which
    // made Ctrl+D look broken instead of switched off.
    if (!emulator_.diagnostics()) {
      MessageBoxW(hwnd_,
                  L"Diagnostika je vypnutá, takže nie je čo vypísať.\r\n"
                  L"\r\n"
                  L"Zapnete ju v Nastaveniach (F11, Ctrl+N) alebo tým, že "
                  L"emulátor spustíte s prepínačom --diag. Výpis potom ide "
                  L"na konzolu.",
                  L"Výpis diagnostiky", MB_OK | MB_ICONINFORMATION);
      return;
    }
    // Belt and braces: turning diagnostics on opens the console, so there
    // normally is one by now.  Costs nothing if there is.
    if (!host::HasConsole()) host::OpenConsole();
    emulator_.PostDumpDiagnostics();
  });

  OnCommand(ID_ACTIVATE_MENU, [this] {
    // What Alt and F10 would do, on a key the guest has no use for.  Posted
    // rather than sent, so the menu loop does not start inside the accelerator
    // dispatch that asked for it.
    PostMessageW(hwnd_, WM_SYSCOMMAND, SC_KEYMENU, 0);
  });

  OnCommand(ID_FILE_EXPORT, [this] {
    // Save As, not "save a copy": the folder becomes the diskette's own from
    // here on.  See VirtualDisk::SaveAs -- an unsaved diskette is what this is
    // mostly for, and leaving it unsaved after saving it would be the sort of
    // half-act this program keeps having to unpick.
    //
    // The name may be one that does not exist yet -- saving a diskette
    // somewhere new is the normal case, and making the user go and create the
    // folder first turned one act into two.  VirtualDisk::SaveAs creates it.
    // The diskette's own name is suggested, because saving it under the name
    // it already has is what is nearly always meant.
    const std::wstring folder = win::PickFolderToCreate(
        hwnd_, L"Kam sa má disketa uložiť",
        disk_.labels.name.empty() ? L"Disketa" : disk_.labels.name.c_str());
    // Cancelling is an answer, not an error.
    if (!folder.empty()) emulator_.PostSaveDiskAs(folder);
  });

  OnCommand(ID_DISK_INSERT, [this] {
    // Asked before the picker, not after: choosing a folder and only then
    // being told the swap cannot happen wastes the choice.
    if (!ConfirmLosingDiskette()) return;
    const std::wstring folder = win::PickFolder(
        hwnd_, L"Vyberte priečinok s disketou, ktorá sa má vložiť");
    if (folder.empty()) return;
    // The worker does the swap, and only between two sector transfers: the
    // machine is its own and a diskette pulled mid-write would tear the image
    // (6.17).  The tone and the title wait for it to report back.
    //
    // A diskette that was locked when it last came out goes back in locked,
    // whichever road it takes -- this one, a slot, or the command line.
    emulator_.PostMountDisk(folder, settings_.disk_locked(folder));
  });

  OnCommand(ID_DISK_EJECT, [this] {
    if (!ConfirmLosingDiskette()) return;
    emulator_.PostEjectDisk();
  });

  OnCommand(ID_DISK_PROTECT, [this] {
    // An empty drive has no notch to move, and the menu item is greyed to say
    // so -- but Ctrl+Z fires from the accelerator table whatever the menu
    // says, so it has to answer here rather than go nowhere in silence.
    if (!disk_.present) {
      MessageBoxW(hwnd_,
                  L"V mechanike nie je disketa, takže nie je čo zamknúť.",
                  L"Eureka A4", MB_OK | MB_ICONINFORMATION);
      return;
    }
    // The worker owns the machine; the tone and the title wait for it to
    // report back through WM_EMU_DISK_CHANGED, the same road a swap takes.
    emulator_.PostSetWriteProtect(!disk_.writeProtected);
  });

  OnCommand(ID_DISK_NEW, [this] {
    if (!ConfirmLosingDiskette()) return;
    NewDiskDialog dialog(CurrentSlots());
    if (dialog.ShowModal(hwnd_, IDD_NEWDISK) != IDOK) return;

    // What a slot would have to hold to bring this diskette back.  Worked out
    // before anything is posted, because for a folder kind it is the folder
    // and for a memory kind it is a marker -- and the slot has to be written
    // even though the worker has not made the diskette yet.
    std::wstring slotValue;
    switch (dialog.kind()) {
      case NewDiskDialog::Kind::kUnsaved:
        // The slot goes with it, so that taking this diskette out later puts
        // it back where the user will look for it rather than destroying it.
        emulator_.PostCreateEmptyDisk(true, dialog.slot());
        slotValue = kSlotUnsaved;
        break;
      case NewDiskDialog::Kind::kUnformatted:
        emulator_.PostCreateEmptyDisk(false, dialog.slot());
        // No slot for this one: unformatted lasts until the first Shift+F8,
        // so a slot would go on offering a state the diskette left behind
        // long ago.  The dialog greys the picker out to say so.
        break;
      case NewDiskDialog::Kind::kNewFolder: {
        // Created here rather than on the worker: making a folder is the
        // host's business, and a failure has to be reported where there is a
        // window to report it in.
        std::error_code ec;
        if (!std::filesystem::create_directory(
                std::filesystem::path(dialog.folder()), ec)) {
          MessageBoxW(hwnd_,
                      (L"Priečinok pre novú disketu sa nepodarilo "
                       L"vytvoriť:\r\n\r\n" + dialog.folder())
                          .c_str(),
                      L"Nová disketa", MB_OK | MB_ICONERROR);
          return;
        }
        // A folder that has just been created cannot carry a lock, so this
        // asks the settings nothing.  Putting a diskette in that may have been
        // locked long ago is Ctrl+I, and that is where the lock is looked up.
        emulator_.PostMountDisk(dialog.folder());
        slotValue = dialog.folder();
        break;
      }
    }
    if (dialog.slot() > 0) SaveSlot(dialog.slot(), slotValue);
  });

  for (int number = 1; number <= Settings::kSlots; ++number)
    OnCommand(ID_DISK_SLOT_FIRST + number - 1,
              [this, number] { InsertSlot(number); });

  OnCommand(ID_DISK_SLOTS, [this] {
    // The slot value and not the folder: an unsaved diskette has no folder but
    // can still be put in a slot, which then makes a fresh empty one.
    const SlotsDialog::Locks before = CurrentLocks();
    const SlotList beforeSlots = CurrentSlots();
    SlotsDialog dialog(beforeSlots, before, CurrentPresent(), disk_.slotValue,
                       disk_.writeProtected, settings_);
    if (dialog.ShowModal(hwnd_, IDD_SLOTS) != IDOK) return;
    for (int number = 1; number <= Settings::kSlots; ++number) {
      const auto index = static_cast<std::size_t>(number - 1);
      const std::wstring& value = dialog.slots()[index];
      const bool locked = dialog.locks()[index];
      settings_.SetSlot(number, value);
      // The lock belongs to the folder, not to the slot, so this is the same
      // list the menu's own Ctrl+Z writes to.  Two slots pointing at one
      // folder are therefore one diskette with one lock, which is what they
      // are on the shelf as well.  For an unsaved diskette this writes
      // nothing, by SetDiskLocked's own rule -- there is no path to key it by.
      settings_.SetDiskLocked(value, locked);
      // A slot marked unsaved gets its diskette now, not at the first insert.
      // The button is called "Sem novú neuloženú" -- it names making one, so
      // it makes one; deferring that left a slot that named a diskette and had
      // none, which is why its lock could not be set.  The worker does nothing
      // if it already has one, so this is safe for every slot every time.
      // Before the lock below, so the lock lands on a diskette that exists.
      if (SlotIsUnsaved(value)) emulator_.PostEnsureSlotDiskette(number);
      // Which is exactly why a real diskette needs telling as well.  The notch
      // is a member of the diskette and the worker holds it, so a lock the
      // user ticked here has to travel to the shelf or to the drive; without
      // that it would be ticked in a dialog and nowhere else, which is the
      // silent half-act 6.26 was about.
      //
      // Two diskettes the worker can reach: an unsaved slot's, which is on the
      // shelf, and the one in the drive.  The drive needs the extra guard --
      // repointing that slot somewhere else during this dialog means the box
      // is now about a different diskette, and the worker, which only compares
      // slot numbers, would put the lock on the one still in the drive.
      //
      // Only when it changed.  Posting the unchanged ones would sound the lock
      // tone for slots nobody touched.
      const bool inDrive = (disk_.slot == number && value == beforeSlots[index]) ||
                           dialog.assigned_current() == number;
      if (locked != before[index] && (inDrive || SlotIsUnsaved(value)))
        emulator_.PostSetSlotWriteProtect(number, locked);
    }
    SaveSettings();

    // "Sem vloženú disketu" on an unsaved diskette does more than
    // write a marker into the settings: it gives that diskette a second
    // reference, so taking it out puts it in that slot instead of destroying
    // it.  Without this the dialog would promise a home it does not provide.
    if (disk_.unsaved && dialog.assigned_current() > 0)
      emulator_.PostAssignSlot(dialog.assigned_current());
  });

  // The diskette in the drive as the collection to split.  It is the folder
  // the user is most likely to mean and it is only a suggestion: the field is
  // editable and Prehľadávať is beside it.
  OnCommand(ID_DISK_SPLIT, [this] { SplitCollection(disk_.home); });

  OnCommand(ID_TOOLS_SETTINGS, [this] {
    SettingsDialog dialog(emulator_.mode(), emulator_.diagnostics(),
                          settings_.keep_ram());
    if (dialog.ShowModal(hwnd_, IDD_SETTINGS) != IDOK) return;
    // Turning diagnostics on is the request for somewhere to read them: this
    // program has no console until something asks for one.  Done here, on the
    // window thread, because the worker must not open windows.
    if (dialog.diagnostics() && !emulator_.diagnostics()) host::OpenConsole();
    // Posted unconditionally; the worker ignores a mode it is already in.
    emulator_.PostSetMode(dialog.mode());
    emulator_.PostSetDiagnostics(dialog.diagnostics());
    // The one setting in this dialog that outlives the run.  Persist it, and
    // if it just went off, drop any snapshot already on disk: turning it off
    // is the user asking for a clean start, and a stale file left behind would
    // resume a month-old state the next time it is switched back on.
    if (dialog.keep_ram() != settings_.keep_ram()) {
      settings_.SetKeepRam(dialog.keep_ram());
      SaveSettings();
      if (!dialog.keep_ram()) {
        std::error_code ec;
        std::filesystem::remove(Settings::SnapshotFile(), ec);
      }
    }
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

// Changing the diskette asks nothing.  An unsaved diskette does get dropped by a
// swap, and this used to offer to save it first -- but a question in front of
// every change of medium is in the way of ordinary work, and swapping is
// ordinary work: putting another diskette in to copy from is the obvious
// reason to have a scratch one at all.  The offer belongs where the last
// chance really is, and that is at exit, where main.cpp makes it.
//
// What keeps this from being a silent loss is that the title says "neuložená"
// the whole time, so what would be dropped is on screen -- and on NVDA+T --
// before anybody reaches for Ctrl+I.

void MainWindow::SetSliders(int speechRate, int volume) {
  const int oldRate = settings_.speech_rate();
  const int oldVolume = settings_.volume();
  // Settings does the clamping, so a step past either end lands back on it
  // and comes out as no change at all.
  settings_.SetSpeechRate(speechRate);
  settings_.SetVolume(volume);
  if (settings_.speech_rate() == oldRate && settings_.volume() == oldVolume)
    return;
  emulator_.PostSetSpeechRate(settings_.speech_rate());
  emulator_.PostSetVolume(settings_.volume());
  // Before the save, which can stop on a message box: the add-on is already
  // looking for the new position.
  PublishSliders();
  // Saved on every step, like a diskette choice: a position the user set is
  // one they expect to find again, and a failed save is best reported now.
  SaveSettings();
}

void MainWindow::InsertSlot(int number) {
  // The diskette already in the drive may be the only copy of itself.
  if (!ConfirmLosingDiskette()) return;
  const std::wstring slot = settings_.slot(number);
  // A slot is a place, not a recipe: it holds the diskette that came out of
  // it, with everything written on it since.  The worker keeps them, because
  // it owns the machine -- so all this decides is what to do the first time,
  // when the slot has nothing put away yet.  For a memory slot that is a
  // fresh empty diskette; for a folder slot it is the folder.
  if (SlotIsUnsaved(slot)) {
    emulator_.PostInsertSlot(number, slot, false);
    return;
  }
  const std::wstring folder = slot;
  // An empty slot answers, rather than doing nothing.  The menu item is left
  // enabled on purpose: greying it would make Ctrl+digit -- which fires from
  // the accelerator table whatever the menu says -- the one thing here that
  // is silent, and silence is the failure mode this program keeps chasing.
  if (folder.empty()) {
    MessageBoxW(hwnd_,
                (L"Slot " + std::to_wstring(number) +
                 L" je prázdny.\r\n\r\nPriradiť mu disketu môžete v ponuke "
                 L"Disketa → Spravovať sloty.")
                    .c_str(),
                L"Sloty s disketami", MB_OK | MB_ICONINFORMATION);
    return;
  }
  std::error_code ec;
  // Checked here rather than left to the mount, so the message can name the
  // slot: "the folder is gone" is far less useful than knowing which of the
  // nine keys has gone stale.
  if (!std::filesystem::is_directory(std::filesystem::path(folder), ec)) {
    MessageBoxW(hwnd_,
                (L"Priečinok zo slotu " + std::to_wstring(number) +
                 L" sa nedá nájsť:\r\n\r\n" + folder +
                 L"\r\n\r\nAk je to odpojený disk, pripojte ho. Inak slotu "
                 L"priraďte inú disketu v ponuke Disketa → Spravovať sloty.")
                    .c_str(),
                L"Sloty s disketami", MB_OK | MB_ICONWARNING);
    return;
  }
  // The lock goes in with the diskette rather than after it: a diskette that
  // arrived writable for a moment and locked afterwards would leave a window
  // in which the guest could write to exactly what the lock is there to
  // protect.
  emulator_.PostInsertSlot(number, folder, settings_.disk_locked(folder));
}

// The whole splitter from the window's side: ask, plan, show, carry out.
//
// Everything that touches a file is in disk_split and everything that decides
// what goes where is in disk_layout; this puts the two dialogs in front of
// them and reports what happened.  The one thing worth watching here is the
// order -- SPOLU.txt is written before the copying starts, because the copy
// takes a while and a failure to write it has to be heard while the user is
// still thinking about the units it came from.
void MainWindow::SplitCollection(const std::wstring& source) {
  SplitDialog dialog(source);
  if (dialog.ShowModal(hwnd_, IDD_SPLIT) != IDOK) return;

  const std::filesystem::path from(dialog.source());
  const std::filesystem::path to(dialog.target());
  disk_split::Collection collection;
  std::wstring error;
  if (!disk_split::Scan(from, collection, error)) {
    MessageBoxW(hwnd_, error.c_str(), L"Rozdeliť kolekciu",
                MB_OK | MB_ICONWARNING);
    return;
  }

  disk_layout::Options options = dialog.options();
  // SPOLU.txt is the collection's own answer to "what belongs together" and
  // outranks both guessing rules, so it arrives from the folder and not from
  // the dialog.
  options.spolu = collection.spolu;
  disk_layout::Plan plan = disk_layout::Build(collection.files, options);

  // The plan and the units it was built from move together.  The dialog
  // refuses to close on units it has not recounted, so `plan` here is always
  // the plan that was on screen.
  const auto rebuild = [&](const std::wstring& units) {
    options.spolu = disk_layout::ParseSpolu(units);
    plan = disk_layout::Build(collection.files, options);
    return disk_split::Describe(plan, collection.files, options, from, to);
  };

  SplitPlanDialog preview(
      disk_split::Describe(plan, collection.files, options, from, to),
      disk_layout::FormatSpolu(
          disk_layout::UnitsAsGroups(plan, collection.files)),
      rebuild);
  if (preview.ShowModal(hwnd_, IDD_SPLITPLAN) != IDOK) return;

  // The only write into the collection there is, and only because the user
  // ticked the box saying so.
  if (preview.save_spolu() &&
      !disk_split::WriteSpolu(from, disk_layout::ParseSpolu(preview.units()),
                              error))
    MessageBoxW(hwnd_, error.c_str(), L"Rozdeliť kolekciu",
                MB_OK | MB_ICONWARNING);

  const disk_split::Outcome outcome =
      disk_split::Execute(plan, collection.files, options, from, to);
  if (!outcome.ok) {
    MessageBoxW(hwnd_,
                (L"Rozdelenie sa nedokončilo:\r\n\r\n" + outcome.error).c_str(),
                L"Rozdeliť kolekciu", MB_OK | MB_ICONERROR);
    return;
  }
  // Counted, not spelled with a plural: Slovak has three shapes and this is
  // read aloud, where a wrong ending is heard rather than skimmed.
  std::wstring done = L"Kolekcia je rozdelená.\r\n\r\nDiskiet: " +
                      std::to_wstring(outcome.diskettes) +
                      L"\r\nSkopírovaných súborov: " +
                      std::to_wstring(outcome.copied);
  if (!plan.rejected.empty())
    done += L"\r\nOdmietnutých súborov: " +
            std::to_wstring(plan.rejected.size());
  done += L"\r\n\r\nSú v priečinku:\r\n" + to.wstring() +
          L"\r\n\r\nKaždý priečinok v ňom je jedna disketa. Celý plán aj "
          L"s dôvodmi odmietnutia je v súbore OBSAH.txt.";
  MessageBoxW(hwnd_, done.c_str(), L"Rozdeliť kolekciu",
              MB_OK | MB_ICONINFORMATION);
}

// The .rc carries only what an empty slot says; the real names come from the
// settings and change under the user's hands.  The keys after the tab are left
// alone -- RefreshShortcutText owns those, and it runs straight after this.
void MainWindow::RefreshSlotItems(HMENU menu) const {
  for (int number = 1; number <= Settings::kSlots; ++number) {
    const UINT id = static_cast<UINT>(ID_DISK_SLOT_FIRST + number - 1);
    wchar_t existing[256];
    if (!GetMenuStringW(menu, id, existing, ARRAYSIZE(existing), MF_BYCOMMAND))
      continue;
    const std::wstring current(existing);
    const std::size_t tab = current.find(L'\t');
    if (tab == std::wstring::npos) continue;

    const std::wstring label = L"&" + std::to_wstring(number) + L" " +
                               SlotDisplayName(settings_.slot(number));
    std::wstring wanted = label + current.substr(tab);
    if (wanted == current) continue;
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING;
    info.dwTypeData = wanted.data();
    SetMenuItemInfoW(menu, id, FALSE, &info);
  }
}

SlotList MainWindow::CurrentSlots() const {
  SlotList slots;
  for (int number = 1; number <= Settings::kSlots; ++number)
    slots[static_cast<std::size_t>(number - 1)] = settings_.slot(number);
  return slots;
}

SlotsDialog::Locks MainWindow::CurrentLocks() const {
  SlotsDialog::Locks locks{};
  for (int number = 1; number <= Settings::kSlots; ++number) {
    const auto index = static_cast<std::size_t>(number - 1);
    const std::wstring& slot = settings_.slot(number);
    // The drive first: the diskette belonging to this slot is in it, so its
    // notch is what the machine says and not what was written down before it
    // went in.  Ctrl+Z during the session changes one and not the other.
    if (disk_.slot == number)
      locks[index] = disk_.writeProtected;
    else if (SlotIsUnsaved(slot))
      locks[index] = (emulator_.stash_locked() & (1u << number)) != 0;
    else
      locks[index] = settings_.disk_locked(slot);
  }
  return locks;
}

SlotsDialog::Present MainWindow::CurrentPresent() const {
  SlotsDialog::Present present{};
  for (int number = 1; number <= Settings::kSlots; ++number) {
    const auto index = static_cast<std::size_t>(number - 1);
    const std::wstring& slot = settings_.slot(number);
    // A folder slot always has a diskette: the folder is one.  An unsaved slot
    // has one only if it exists -- on the shelf, or in the drive -- because
    // nothing makes it until the slot is first inserted.  That is the honest
    // reason its lock can be greyed, and the only one (6.26).
    if (slot.empty())
      present[index] = false;
    else if (!SlotIsUnsaved(slot))
      present[index] = true;
    else
      present[index] = disk_.slot == number ||
                       (emulator_.stash_holds() & (1u << number)) != 0;
  }
  return present;
}

// Reported and not swallowed: the user has just arranged their slots, and
// slots that quietly go back to how they were at the next start are the worst
// of both worlds.
void MainWindow::SaveSettings() {
  std::wstring error;
  if (!settings_.Save(error))
    MessageBoxW(hwnd_, error.c_str(), L"Eureka A4", MB_OK | MB_ICONWARNING);
}

void MainWindow::SaveSlot(int number, std::wstring value) {
  settings_.SetSlot(number, std::move(value));
  SaveSettings();
}

// The lock outlives the diskette being taken out, because that is what a
// notch does: a diskette put back in comes back protected.  Kept by folder --
// an unsaved diskette has none, so its lock lasts as long as it does, which
// SetDiskLocked declines to write down.  That is a limit on remembering it,
// not on having it: the notch itself is set either way (6.26).
void MainWindow::RememberLock(const DiskState& disk) {
  if (disk.home.empty()) return;
  if (settings_.disk_locked(disk.home) == disk.writeProtected) return;
  settings_.SetDiskLocked(disk.home, disk.writeProtected);
  SaveSettings();
}

// The keyboard mode outlives the run (ea4-0vw), so every way of reaching it has
// to write it down.  Here rather than in the four commands that change it --
// the menu's two, Ctrl+K and the Nastavenia dialog -- because WM_EMU_STATE is
// the one place all four arrive at, and the worker posts it only once the mode
// has really changed: one it is already in is dropped before this.
//
// Guarded by the comparison because that message carries a diagnostics change
// as well, and diagnostics is deliberately not remembered.  Without the test
// every Ctrl+D would rewrite the file to say what it already said.
void MainWindow::RememberMode() {
  const bool braille = emulator_.mode() == InputMode::kBraille;
  if (settings_.braille_keyboard() == braille) return;
  settings_.SetBrailleKeyboard(braille);
  SaveSettings();
}

// Asked before anything pushes the diskette out of the drive.
//
// An unsaved diskette exists nowhere but in this process.  If a quick-choice
// slot points at it, taking it out is safe -- it goes to that slot and comes
// back from it.  If nothing points at it, the drive is the only thing holding
// it, and swapping would destroy it.  That has to be the user's decision, not
// a side effect of pressing Ctrl+2: the loss is silent, and silent loss is the
// worst thing this program can do.
//
// Returns false when the user backed out, in which case the caller does
// nothing at all.
bool MainWindow::ConfirmLosingDiskette() {
  if (!disk_.unsaved || disk_.files == 0 || disk_.slot != 0) return true;

  const std::wstring question =
      L"V mechanike je neuložená disketa, na ktorej je " +
      std::to_wstring(disk_.files) +
      (disk_.files == 1 ? L" súbor" : disk_.files < 5 ? L" súbory" : L" súborov") +
      L", a nepatrí žiadnemu slotu.\r\n\r\n"
      L"Nemá priečinok, takže nikde inde neexistuje a vybratím zanikne.\r\n\r\n"
      L"Áno — najprv ju uložím do priečinka.\r\n"
      L"Nie — zahodiť ju.\r\n"
      L"Zrušiť — nechať ju v mechanike.";
  switch (MessageBoxW(hwnd_, question.c_str(), L"Neuložená disketa",
                      MB_YESNOCANCEL | MB_ICONWARNING)) {
    case IDYES: {
      // The save is the worker's job and takes a moment; the caller waits for
      // it rather than swapping underneath it, so this answers "no, not now"
      // and the user repeats the command once it is saved.  After that the
      // diskette has a folder, so the second attempt does not ask again.
      const std::wstring folder = win::PickFolderToCreate(
          hwnd_, L"Kam sa má disketa uložiť", L"Disketa");
      if (folder.empty()) return false;
      emulator_.PostSaveDiskAs(folder);
      return false;
    }
    case IDNO:
      return true;
    default:
      return false;
  }
}

// The RAM counterpart of ConfirmLosingDiskette.  When "keep RAM" is on, the
// user has said they want to come back where they left off -- but that only
// works after a real power-down, which writes the snapshot.  Closing the
// window with the machine still running throws the session away, so this is
// the one path where that is worth a question.  It is deliberately not asked
// when the switch is off (nothing is being kept) or when the machine is
// already off (exit writes the snapshot either way).
bool MainWindow::ConfirmClosingWithoutPowerDown() {
  const int answer = MessageBoxW(
      hwnd_,
      L"Eureka je stále zapnutá, takže sa stav pamäte pri zavretí neuloží "
      L"a ďalšie spustenie začne od začiatku.\r\n\r\n"
      L"Ak chcete pokračovať tam, kde ste skončili, najprv ju vypnite: "
      L"Stroj → Vypnúť Eureku (F11, Ctrl+V).\r\n\r\n"
      L"Zavrieť okno bez uloženia stavu?",
      L"Eureka je zapnutá", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
  return answer == IDYES;
}

void MainWindow::RememberDisk(const std::wstring& folder) {
  // An unsaved diskette and an empty drive are both "nothing to put back":
  // neither is cleared here, so unplugging a diskette for one session does not
  // lose the folder the next start would have opened.
  if (folder.empty() || settings_.last_disk() == folder) return;
  settings_.SetLastDisk(folder);
  std::wstring error;
  if (!settings_.Save(error))
    MessageBoxW(hwnd_, error.c_str(), L"Eureka A4", MB_OK | MB_ICONWARNING);
}

void MainWindow::SetDiskState(DiskState disk) {
  disk_ = std::move(disk);
  RefreshTitle();
}

std::wstring MainWindow::AboutText() const {
  return L"Eureka A4 Emulator\r\n"
         L"\r\n"
         L"Emulátor osobného počítača Robotron Eureka A4 pre nevidiacich.\r\n"
         L"Procesor Hitachi HD64180 na 6,144 MHz, zvuk 48 kHz.\r\n"
         L"\r\n"
         L"ROM: " + romPath_ + L"\r\n" +
         L"Disk: " + disk_.labels.description + L"\r\n" +
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
  // The diskette is named, not spelled out as a path: this whole line is read
  // aloud on every Alt+Tab and every NVDA+T, so it holds the answer and not
  // the paperwork.  The path is in Pomocník -> O programe.
  // The lock is part of what the diskette is right now, so it belongs to the
  // same answer.  Only when it is on: a title that said "odomknutá" about
  // every ordinary diskette would spend a word on the usual case.
  const std::wstring diskette =
      disk_.labels.name + (disk_.writeProtected ? L", zamknutá" : L"");
  // Switched off outranks everything else in the line.  Keys go nowhere and
  // the diskette is not being read, so answering "režim: externá" first would
  // be answering a question that has stopped being the one worth asking.
  //
  // No branch of this line names a key.  It is read aloud on every Alt+Tab and
  // every NVDA+T, so it answers "what is it now" and nothing else; the keys
  // that change it are in the menu and in Pomocník, where they are read once
  // and on purpose.  Both branches gave advice until 9 Sep 2026 -- "zapne ju
  // Reset" and "vráti ju Shift+F11" -- and the first of them had by then
  // stopped being true, because Ctrl+P switches it on.  That is what advice in
  // a state line costs: it is repeated on every glance and it rots quietly.
  if (poweredOff_) {
    SetTitle(L"Eureka A4 — vypnutá — disketa: " + diskette);
    return;
  }
  SetTitle(released_
               ? L"Eureka A4 — klávesnica uvoľnená — režim: " +
                     std::wstring(ModeName(emulator_.mode())) +
                     L" — disketa: " + diskette
               : L"Eureka A4 — klávesnica: " +
                     std::wstring(ModeName(emulator_.mode())) +
                     L" — disketa: " + diskette);
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
  // While the machine is off the keyboard is released and cannot be taken
  // back -- there is nothing on the other side to take it.  Ticked and greyed
  // together is the honest pair: this is the state, and it is not yours to
  // change from here.  Reset is.
  EnableMenuItem(menu, ID_KEYBOARD_RELEASE,
                 MF_BYCOMMAND | (poweredOff_ ? MF_GRAYED : MF_ENABLED));
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
  // Nothing to take out of an empty drive, and nothing to save out of one
  // either.  Greying says so where a screen reader reads it; the alternative
  // is a menu item that answers with a beep or with nothing at all.
  const UINT hasDisk =
      MF_BYCOMMAND | (disk_.present ? MF_ENABLED : MF_GRAYED);
  EnableMenuItem(menu, ID_DISK_EJECT, hasDisk);
  EnableMenuItem(menu, ID_FILE_EXPORT, hasDisk);
  EnableMenuItem(menu, ID_DISK_PROTECT, hasDisk);
  CheckMenuItem(menu, ID_DISK_PROTECT,
                MF_BYCOMMAND |
                    (disk_.writeProtected ? MF_CHECKED : MF_UNCHECKED));
  // A machine that is already off has nothing to switch off, so that one is
  // greyed.  Both resets stay enabled either way.  The warm one switches an
  // off machine on and resets a running one, and the running case is the
  // point: it is the way out of a hang, so greying it while the machine runs
  // would take it away exactly when it is needed.  It was greyed until
  // 16 Sep 2026, and a greyed item's accelerator is swallowed without a
  // WM_COMMAND, so Ctrl+P did nothing at all (ea4-6kz).
  EnableMenuItem(menu, ID_MACHINE_POWEROFF,
                 MF_BYCOMMAND | (poweredOff_ ? MF_GRAYED : MF_ENABLED));
  // Before RefreshShortcutText, which reads the item text back and would
  // otherwise be working on the names this is about to replace.
  RefreshSlotItems(menu);
  RefreshShortcutText(menu);
}

// The Ctrl shortcuts need F11 in front of them only while Eureka has the
// keyboard.  Once it is released they stand on their own, and a menu still
// saying "F11, Ctrl+R" would be telling the user to press a key that does
// nothing at all in that state -- not even a beep, because SetPassOnce
// declines while released_ and the item for it is greyed out just above.
//
// The letters are read back out of the menu rather than repeated here, so the
// .rc stays the only place that names them.  All this does is put the prefix
// on and take it off again.
void MainWindow::RefreshShortcutText(HMENU menu) const {
  static constexpr wchar_t kPrefix[] = L"F11, ";
  std::vector<UINT> ids = {ID_FILE_EXPORT,      ID_FILE_EXIT,
                           ID_DISK_INSERT,      ID_DISK_NEW,
                           ID_DISK_EJECT,       ID_DISK_PROTECT,
                           ID_MACHINE_RESET,    ID_MACHINE_POWERON,
                           ID_MACHINE_POWEROFF, ID_MACHINE_FASTER,
                           ID_MACHINE_SLOWER,   ID_MACHINE_LOUDER,
                           ID_MACHINE_QUIETER,
                           ID_KEYBOARD_TOGGLE,  ID_TOOLS_SETTINGS,
                           ID_TOOLS_DIAGDUMP,   ID_HELP_KEYS};
  for (int number = 1; number <= Settings::kSlots; ++number)
    ids.push_back(static_cast<UINT>(ID_DISK_SLOT_FIRST + number - 1));
  for (UINT id : ids) {
    wchar_t text[128];
    // MF_BYCOMMAND searches the submenus too, so the ids are enough.
    if (!GetMenuStringW(menu, id, text, ARRAYSIZE(text), MF_BYCOMMAND)) continue;
    std::wstring item(text);
    const size_t tab = item.find(L'\t');
    if (tab == std::wstring::npos) continue;
    std::wstring keys = item.substr(tab + 1);
    if (keys.starts_with(kPrefix)) keys.erase(0, ARRAYSIZE(kPrefix) - 1);
    if (!released_) keys.insert(0, kPrefix);
    std::wstring wanted = item.substr(0, tab) + L'\t' + keys;
    if (wanted == item) continue;
    // MIIM_STRING alone: ModifyMenu would take the item's other attributes
    // with it, and the checks and greying above are set by then.
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING;
    info.dwTypeData = wanted.data();
    SetMenuItemInfoW(menu, id, FALSE, &info);
  }
}

void MainWindow::SetReleased(bool released, bool quiet) {
  // A switched-off machine has nothing to give the keyboard to, so the one
  // direction that is refused here is taking it back.  That is what makes
  // Shift+F11 do nothing while the machine is off, and the item for it is
  // greyed out in RefreshMenu so a screen reader says so.  Refusing in
  // silence is safe only because the keyboard is already where the key would
  // put it: the trap this file keeps setting is a key that changes where the
  // keys go without saying so, not one that declines to change nothing.
  if (poweredOff_ && !released) return;
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
  if (quiet) return;
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
      // close does: F12 opens the menu in every state and File -> Quit is
      // right there, and F11 or Shift+F11 hands Alt+F4 back to Windows for as
      // long as it is wanted.
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

    case WM_COMMAND:
      // Spends the one-shot on a shortcut that only fired because it was
      // armed.  It has to happen here: TranslateAccelerator takes the press,
      // so HostKeepsKey never sees it and cannot spend it -- and an unspent
      // one-shot stays armed for good, which is the silent sticky mode this
      // file has been caught by before.  HIWORD 1 means "accelerator", so
      // choosing the same command from the menu does not spend anything.
      //
      // The two keyboard-state commands are excluded because they own that
      // state: F11 would disarm itself, and each would sound its tone twice.
      if (HIWORD(wParam) == 1 && LOWORD(wParam) != ID_KEYBOARD_PASSONCE &&
          LOWORD(wParam) != ID_KEYBOARD_RELEASE)
        SetPassOnce(false);
      break;

    case WM_INITMENUPOPUP:
      RefreshMenu();
      break;

    case WM_CLOSE:
      // Every way out of the program lands here -- the menu's Skončiť posts it,
      // and Alt+F4 or the close box reach it through WM_SYSCOMMAND.  If "keep
      // RAM" is on and the machine is still running, closing now is the battery
      // cut-off: the session is lost and the next start initialises.  Ask
      // first.  Nothing to ask when the switch is off, when the machine is
      // already switched off (exit will write the snapshot then), or when a
      // disk error is taking the window down for its own reasons.
      if (!forceClose_ && settings_.keep_ram() && !poweredOff_ &&
          !ConfirmClosingWithoutPowerDown())
        return 0;
      break;

    case WM_NCDESTROY:
      // Windows keeps the property's atom alive until it is removed, so a
      // window that dies with it still set leaks it for the rest of the
      // process.  Nothing else here needs the message, so it goes on to the
      // base class as usual.
      RemovePropW(hwnd_, kKeyboardReleasedProp);
      RemovePropW(hwnd_, kSpeechRateProp);
      RemovePropW(hwnd_, kVolumeProp);
      break;

    case WM_EMU_STATE:
      RefreshTitle();
      RememberMode();
      return 0;

    case WM_EMU_POWERED_OFF:
      // C45Ah is FFh here: the firmware's own marker that this was a clean
      // power-down.  It is not what makes the next start a resume -- that is
      // RAM surviving with magic 55AAh at C45Bh, and the marker only stops an
      // alarm from waking the machine for good (HANDOFF 6.15).
      //
      // Neither a dialog nor a WM_CLOSE any more.  Switching off is a state
      // the machine sits in, so it is said the way every other state on this
      // machine is said -- a tone at the moment it changes, the title for any
      // moment after that -- and the window stays open around it.  The dialog
      // that stood here described what the host has not implemented yet, which
      // is nothing the user can act on, and it did so in the one breath before
      // taking the window away.
      //
      // Off and released are one state, not two.  A switched-off machine has
      // nowhere to put a keystroke, so holding the keyboard would make every
      // key on it silent -- the trap this whole file is built around -- and it
      // would keep NVDA asleep over a window with nothing to read.  So the
      // keyboard goes back to Windows on the way out and to Eureka on the way
      // in, without a tone of its own: the power tone below is that
      // announcement, and the title carries both halves afterwards.  The order
      // matters, poweredOff_ first: SetReleased asks it before it decides.
      poweredOff_ = wParam != 0;
      SetReleased(poweredOff_, /*quiet=*/true);
      RefreshMenu();
      // lParam is set for both halves of an unanswered alarm: it woke the
      // machine and, once served, put it back to sleep again on its own
      // (HANDOFF 6.32).  That is the Eureka speaking for itself, the way a
      // chime or a diary message would be, not a mode change -- ringing every
      // hour with a rising-then-falling pair around each chime would be
      // exactly the noise this file's tones exist to avoid.  A switch-on or
      // switch-off the host itself commanded, from the menu or a shortcut,
      // still gets the tone; the title carries the state either way, so
      // NVDA+T answers it whenever asked.
      if (lParam == 0) {
        if (poweredOff_) TonePoweredOff(); else TonePoweredOn();
      }
      RefreshTitle();
      return 0;

    case WM_EMU_DISK_ERROR:
      MessageBoxW(hwnd_,
                  (L"Chyba pri ukladaní disku:\r\n\r\n" +
                   emulator_.TakeDiskError()).c_str(),
                  L"Eureka A4", MB_OK | MB_ICONERROR);
      // The worker has already stopped; there is no powering down from here,
      // so WM_CLOSE must not stop to ask about the RAM.
      forceClose_ = true;
      PostMessageW(hwnd_, WM_CLOSE, 0, 0);
      return 0;

    case WM_EMU_NO_AUDIO:
      MessageBoxW(hwnd_,
                  L"Zvukové zariadenie sa nepodarilo otvoriť.\r\n\r\n"
                  L"Eureka beží, ale nebude ju počuť — a reč je na tomto "
                  L"stroji celé používateľské rozhranie, nie doplnok.",
                  L"Eureka A4", MB_OK | MB_ICONWARNING);
      return 0;

    case WM_EMU_DISK_CHANGED: {
      const DiskChange change = emulator_.TakeDiskChange();
      // An empty name means the payload has already been taken by an earlier
      // message, so there is nothing here to act on -- and acting on it would
      // blank the title the earlier one had just set.
      if (change.state.labels.name.empty()) return 0;
      SetDiskState(change.state);
      // The tone says what happened at the moment it happened; the title
      // answers "what is in there now" at any time afterwards.  Same division
      // of labour as the keyboard state, and for the same reason: there is
      // nothing on screen to look at.
      if (!change.ok) {
        // A folder that will not fit on one diskette is the one refusal with
        // something to offer, and offering it here is what makes the splitter
        // the deed that replaces the advice: the folder has just been chosen,
        // so asking the user to find it again in a second dialog would be
        // making them pay twice for one decision (6.22).
        if (change.tooBig && !change.attempted.empty()) {
          if (MessageBoxW(hwnd_,
                          (change.error + L"\r\n\r\nChcete to urobiť teraz?")
                              .c_str(),
                          L"Eureka A4", MB_YESNO | MB_ICONQUESTION) == IDYES)
            SplitCollection(change.attempted);
        } else {
          MessageBoxW(hwnd_, change.error.c_str(), L"Eureka A4",
                      MB_OK | MB_ICONERROR);
        }
      } else if (!change.swapped) {
        // Only the notch moved, so the diskette itself is unchanged and there
        // is nothing to remember about which one is in.  The lock, though, is
        // written down: it has to be back the next time this diskette goes in
        // -- the ROM's bulk copy sends the user to and fro between source and
        // target and refuses a source that is not protected.
        RememberLock(change.state);
        if (change.state.writeProtected) ToneLocked(); else ToneUnlocked();
      } else if (change.state.present) {
        ToneInserted();
        RememberDisk(change.state.home);
      } else {
        ToneEjected();
      }
      return 0;
    }

    case WM_EMU_SAVED: {
      const SaveResult saved = emulator_.TakeSaveResult();
      // The state first, so the title already names the folder by the time the
      // box says the diskette went there.  No tone: nothing went in or out,
      // and the box is modal, which a screen reader announces and reads.
      if (saved.ok) {
        SetDiskState(saved.state);
        RefreshMenu();
        // It has a home now, so this is the diskette the next start should
        // open.  A diskette saved and then left is otherwise saved and lost.
        RememberDisk(saved.state.home);
      }
      MessageBoxW(
          hwnd_,
          saved.ok
              // Said outright, because this is the half of Save As that is
              // easy to miss: the diskette does not merely have a copy in that
              // folder, it *is* that folder from now on and writes itself
              // there.  A message that stopped at "uložená do" would leave the
              // user believing they still had an unsaved diskette.
              ? (L"Disketa bola uložená do:\r\n\r\n" + saved.detail +
                 L"\r\n\r\nOdteraz je to jej priečinok a zapisuje sa doň sama.")
                    .c_str()
              : (L"Disketu sa nepodarilo uložiť:\r\n\r\n" + saved.detail +
                 L"\r\n\r\nZostáva taká, aká bola.")
                    .c_str(),
          L"Eureka A4", MB_OK | (saved.ok ? MB_ICONINFORMATION : MB_ICONERROR));
      return 0;
    }

    default:
      break;
  }
  return win::Window::HandleMessage(message, wParam, lParam);
}
