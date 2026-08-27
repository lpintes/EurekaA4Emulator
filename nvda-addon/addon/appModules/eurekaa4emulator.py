# App module for the Eureka A4 emulator, part of the eurekaA4Emulator NVDA add-on
# Copyright (C) 2026 Lubos Pintes
# A plugin is a derivative work of NVDA, so this file may be used under the terms of the
# GNU General Public License, version 2 or later.  See COPYING.txt beside this add-on.

"""Lets NVDA sleep over the emulator's machine window, and only over that window.

The emulator's main window is a machine, not a form: every keystroke on it belongs to
Eureka, which answers in its own synthesised voice.  Two screen readers talking over one
another is the whole problem, and NVDA's own answer -- NVDA+shift+s -- is too coarse,
because it silences the emulator's menu, its dialogs and its message boxes as well.

So this module sleeps per window rather than per application.  It can: ``sleepMode`` is
asked of the object an event or a gesture belongs to, and ``NVDAObject`` sets
``_cache_sleepMode = False``, so an override on the machine window alone is consulted
afresh every time and nothing else in the process is touched.  Dialogs and menus are
other windows, so they keep their ordinary NVDAObject and go on being read.

Which side owns the keyboard is the emulator's business rather than ours.  It publishes
that as a window property (``MainWindow::PublishKeyboardState`` in src/main_window.cpp),
so shift+F11 there wakes NVDA over the machine window too, on the very next keystroke.
"""

import ctypes
from ctypes import wintypes

import addonHandler
import api
import appModuleHandler
import inputCore
import speech
import ui
import winUser
from NVDAObjects import NVDAObject
from scriptHandler import getLastScriptRepeatCount, script

try:
	addonHandler.initTranslation()
except addonHandler.AddonError:
	# Running from the developer scratchpad, which is no add-on and has no translations
	# of its own.  NVDA's builtin _ still works, so this is not worth failing over.
	pass

#: Window class of the emulator's main window; kClassName in src/main_window.cpp.
#: Renaming it there disables this module, and does it silently.
MACHINE_WINDOW_CLASS = "EurekaA4EmulatorWindow"

#: Window property the emulator sets to 1 while it has handed the keyboard back to
#: Windows with shift+F11.  Absent or zero means Eureka owns the keyboard, which is
#: also what an emulator built before this existed answers.
KEYBOARD_RELEASED_PROP = "EurekaA4.KeyboardReleased"

#: Flags that mean a menu of this window's is up, and Windows therefore owns the
#: keyboard.  Measured on the running emulator: opening the menu sets GUI_INMENUMODE
#: and closing it clears it again, while the dropdown adds nothing further.
MENU_MODE_FLAGS = winUser.GUI_INMENUMODE | winUser.GUI_SYSTEMMENUMODE | winUser.GUI_POPUPMENUMODE

#: A private handle on user32, so that the prototype set below cannot disturb the ones
#: NVDA has set on the shared ``ctypes.windll.user32``.
_user32 = ctypes.WinDLL("user32", use_last_error=True)
_user32.GetPropW.argtypes = (wintypes.HWND, wintypes.LPCWSTR)
_user32.GetPropW.restype = wintypes.HANDLE


def _isKeyboardReleased(windowHandle: int) -> bool:
	"""Whether the emulator has handed the keyboard back to the host.

	:param windowHandle: The emulator's main window.
	:returns: True while Windows owns the keyboard, in which case NVDA should behave
		over that window exactly as it does over any other.
	"""
	return bool(_user32.GetPropW(windowHandle, KEYBOARD_RELEASED_PROP))


def _isMenuUp(threadID: int) -> bool:
	"""Whether a menu of this thread's is open, and Windows is therefore taking keys.

	:param threadID: The thread owning the window in question.
	:returns: True while the window is in menu mode.
	"""
	return bool(winUser.getGUIThreadInfo(threadID).flags & MENU_MODE_FLAGS)


class MachineWindow(NVDAObject):
	"""The emulator's main window: Eureka's own keyboard and Eureka's own voice."""

	def _get_sleepMode(self) -> bool:
		if self.appModule and self.appModule.sleepMode:
			# NVDA+shift+s was used on the whole application.  That is an explicit
			# request and outranks anything decided here.
			return True
		if _isKeyboardReleased(self.windowHandle):
			return False
		# A menu bar is not a window: it belongs to the HWND that owns it, so the menu
		# objects NVDA builds when F12 opens the menu carry this window class and land
		# on this overlay too.  Sleeping over them cost the menu bar its announcement --
		# measured: F12 was silent and the first arrow, which drops a real #32768 popup
		# with no overlay on it, was not.  Menu mode is also exactly when the guest is
		# getting no keys anyway, so there is nothing to keep quiet for.
		return not _isMenuUp(self.windowThreadID)


class AppModule(appModuleHandler.AppModule):
	def chooseNVDAObjectOverlayClasses(self, obj: NVDAObject, clsList: list[type]) -> None:
		# getattr rather than the attribute: windowClassName belongs to
		# NVDAObjects.window.Window, and this is asked of every object in the process.
		if getattr(obj, "windowClassName", None) == MACHINE_WINDOW_CLASS:
			clsList.insert(0, MachineWindow)

	@script(
		description=_(
			# Translators: Input help mode message for a command that reports the title of
			# the Eureka A4 emulator window.
			"Reports the title of the emulator window, which carries the keyboard state."
			" Unlike NVDA's own command, this one answers while NVDA sleeps over the"
			" machine window",
		),
		gesture="kb:NVDA+t",
		allowInSleepMode=True,
	)
	def script_title(self, gesture: inputCore.InputGesture) -> None:
		# The window title is where this emulator keeps the state a screen reader user
		# would otherwise have nothing to ask about: which keyboard the guest has, and
		# whether the keys are going to it at all.  NVDA's own NVDA+t is a script like
		# any other and is therefore dropped while asleep, which would leave the state
		# announced by tones at the moment it changes and unreadable at any time after.
		# An app module's scripts are found before globalCommands', so this replaces it
		# inside this application and nowhere else.
		obj = api.getForegroundObject()
		title = obj.name if obj else None
		if not isinstance(title, str) or not title or title.isspace():
			title = winUser.getWindowText(winUser.getForegroundWindow())
		repeatCount = getLastScriptRepeatCount()
		if repeatCount == 0:
			ui.message(title)
		elif repeatCount == 1:
			speech.speakSpelling(title)
		else:
			api.copyToClip(title, notify=True)
