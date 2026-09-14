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

It also says the emulator's two sliders out loud.  Once the keyboard is the host's, Ctrl or
Alt with an arrow moves one of them a step, and Eureka may not speak again for a while --
nor, for the volume, make any sound that would tell the user where it went.
"""

import ctypes
from ctypes import wintypes

import addonHandler
import api
import appModuleHandler
import core
import inputCore
import keyboardHandler
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

#: Window properties holding the positions of the emulator's two sliders, straight from
#: src/sliders.h (``PublishSliders`` in src/main_window.cpp).  Ctrl with an arrow moves the
#: left one, speech rate; Alt with an arrow moves the right one, volume.  Renaming either
#: there silences the announcements, and silently.
SPEECH_RATE_PROP = "EurekaA4.SpeechRate"
VOLUME_PROP = "EurekaA4.Volume"

#: How many positions each slider has.  Fixed in src/sliders.h, and so fixed here.
SPEECH_RATE_POSITIONS = 32
VOLUME_POSITIONS = 21

#: When to look for the new position after a slider key, each in milliseconds after the look
#: before it.  The window moves the slider as soon as its accelerator arrives, which the first
#: look nearly always finds; the later ones allow for a busy desktop.  A key that went to
#: Eureka instead moves nothing, the looks run out, and nothing is said.
SLIDER_LOOK_DELAYS_MS = (30, 70, 150)

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


def _readSlider(windowHandle: int, prop: str) -> int:
	"""The position of one of the emulator's sliders.

	:param windowHandle: The emulator's main window.
	:param prop: ``SPEECH_RATE_PROP`` or ``VOLUME_PROP``.
	:returns: The position, or 0 when the emulator does not publish it.
	"""
	return _user32.GetPropW(windowHandle, prop) or 0


def _sliderMovedBy(gesture: inputCore.InputGesture) -> str | None:
	"""Which slider a gesture moves, provided the keyboard is the host's.

	:param gesture: Any gesture NVDA is about to execute, in any application.
	:returns: The window property of that slider, or None for every other gesture.
	"""
	if not isinstance(gesture, keyboardHandler.KeyboardInputGesture):
		return None
	if gesture.vkCode not in (winUser.VK_LEFT, winUser.VK_RIGHT):
		return None
	modifiers = {vkCode for vkCode, isExtended in gesture.generalizedModifiers}
	if modifiers == {winUser.VK_CONTROL}:
		return SPEECH_RATE_PROP
	if modifiers == {winUser.VK_MENU}:
		return VOLUME_PROP
	return None


def _describeSlider(prop: str, position: int) -> str:
	"""What to say about a slider that has moved.

	:param prop: ``SPEECH_RATE_PROP`` or ``VOLUME_PROP``.
	:param position: The position the emulator published.
	:returns: The announcement.
	"""
	if prop == SPEECH_RATE_PROP:
		# Counted from one, where the volume counts from zero: zero volume is silence and
		# means something, while the lowest rate is merely the slowest.
		return _(
			# Translators: Announced when the speech rate slider of the Eureka A4 emulator
			# moves.  {position} is where it is now, counted from 1, and {count} how many
			# positions the slider has.
			"speech rate {position} of {count}",
		).format(position=position + 1, count=SPEECH_RATE_POSITIONS)
	return _(
		# Translators: Announced when the volume slider of the Eureka A4 emulator moves.
		# {position} is where it is now, 0 being silence, and {count} the loudest position.
		"volume {position} of {count}",
	).format(position=position, count=VOLUME_POSITIONS - 1)


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
	def __init__(self, *args, **kwargs) -> None:
		super().__init__(*args, **kwargs)
		# A decider and not a script.  A script bound to ctrl+arrow here would be found before
		# the focused control's own and take word navigation away from every edit field in the
		# emulator's dialogs.  A decider sees the key and hands it on untouched, and NVDA asks
		# it before it checks sleep mode, so it sees the key while NVDA sleeps as well.
		inputCore.decide_executeGesture.register(self._noticeSliderKey)

	def terminate(self) -> None:
		inputCore.decide_executeGesture.unregister(self._noticeSliderKey)
		super().terminate()

	def _noticeSliderKey(self, gesture: inputCore.InputGesture) -> bool:
		"""Starts watching a slider when one of its keys goes to the machine window.

		Called for every gesture in every application, from the keyboard hook, so it does as
		little as it can until it knows the gesture is one of the four.

		:param gesture: The gesture NVDA is about to execute.
		:returns: Always True: the key is watched, never taken.
		"""
		prop = _sliderMovedBy(gesture)
		if prop is None:
			return True
		windowHandle = winUser.getForegroundWindow()
		if winUser.getClassName(windowHandle) != MACHINE_WINDOW_CLASS:
			return True
		before = _readSlider(windowHandle, prop)
		core.callLater(
			SLIDER_LOOK_DELAYS_MS[0],
			self._lookAtSlider,
			windowHandle,
			prop,
			before,
			0,
		)
		return True

	def _lookAtSlider(self, windowHandle: int, prop: str, before: int, look: int) -> None:
		"""Says where the slider is if the key moved it, or looks again a little later.

		A key at the end of the travel moves nothing and is not announced; the emulator sounds
		its own tone for that.

		:param windowHandle: The emulator's main window.
		:param prop: The slider the key moves.
		:param before: Its position when the key went by.
		:param look: How many looks before this one came up empty.
		"""
		position = _readSlider(windowHandle, prop)
		if position != before:
			# Cancelled first, so that a held key says where the slider is rather than queueing
			# every position it went through.
			speech.cancelSpeech()
			ui.message(_describeSlider(prop, position))
			return
		look += 1
		if look < len(SLIDER_LOOK_DELAYS_MS):
			core.callLater(
				SLIDER_LOOK_DELAYS_MS[look],
				self._lookAtSlider,
				windowHandle,
				prop,
				before,
				look,
			)

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
