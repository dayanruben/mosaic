#if defined(_WIN32)

#include "mosaic-tty-windows.h"

#include "cutils.h"
#include <assert.h>
#include <windows.h>

MosaicTtyInitResult tty_initWithHandles(
	HANDLE stdin,
	HANDLE stdout,
	HANDLE stderr,
	MosaicTtyCallback *callback
) {
	MosaicTtyInitResult result = {};

	MosaicTtyImpl *tty = calloc(1, sizeof(MosaicTtyImpl));
	if (unlikely(tty == NULL)) {
		// result.tty is set to 0 which will trigger OOM.
		goto ret;
	}

	if (unlikely(stdin == INVALID_HANDLE_VALUE)) {
		result.error = GetLastError();
		goto err;
	}
	if (unlikely(stdout == INVALID_HANDLE_VALUE)) {
		result.error = GetLastError();
		goto err;
	}
	if (unlikely(stderr == INVALID_HANDLE_VALUE)) {
		result.error = GetLastError();
		goto err;
	}

	HANDLE interruptEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (unlikely(interruptEvent == NULL)) {
		result.error = GetLastError();
		goto err;
	}

	tty->stdin = stdin;
	tty->stdout = stdout;
	tty->stderr = stderr;
	tty->interrupt_event = interruptEvent;
	tty->callback = callback;

	result.tty = tty;

	ret:
	return result;

	err:
	free(tty);
	goto ret;
}

MosaicTtyInitResult tty_init(MosaicTtyCallback *callback) {
	HANDLE stdin = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE stdout = GetStdHandle(STD_OUTPUT_HANDLE);
	HANDLE stderr = GetStdHandle(STD_ERROR_HANDLE);
	return tty_initWithHandles(stdin, stdout, stderr, callback);
}

MosaicTtyIoResult tty_readInput(
	MosaicTty *tty,
	char *buffer,
	int count
) {
	return tty_readInputWithTimeout(tty, buffer, count, INFINITE);
}

MosaicTtyIoResult tty_readInputWithTimeout(
	MosaicTty *tty,
	char *buffer,
	int count,
	int timeoutMillis
) {
	MosaicTtyIoResult result = {};

	DWORD waitResult;
	HANDLE waitHandles[2] = { tty->stdin, tty->interrupt_event };

	loop:
	waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, timeoutMillis);
	if (likely(waitResult == WAIT_OBJECT_0)) {
		INPUT_RECORD *records = tty->records;
		int recordRequest = recordsCount > count ? count : recordsCount;
		DWORD recordsRead = 0;
		if (unlikely(!ReadConsoleInputW(tty->stdin, records, recordRequest, &recordsRead))) {
			goto err;
		}

		MosaicTtyCallback *callback = tty->callback;
		int nextBufferIndex = 0;
		for (int i = 0; i < (int) recordsRead; i++) {
			INPUT_RECORD record = records[i];
			if (record.EventType == KEY_EVENT) {
				if (record.Event.KeyEvent.wVirtualKeyCode == 0) {
					buffer[nextBufferIndex++] = record.Event.KeyEvent.uChar.AsciiChar;
				}
				// TODO else other key shit
			} else if (record.EventType == MOUSE_EVENT) {
				// TODO mouse shit
			} else if (record.EventType == FOCUS_EVENT) {
				callback->onFocus(callback->opaque, record.Event.FocusEvent.bSetFocus);
			} else if (record.EventType == WINDOW_BUFFER_SIZE_EVENT && tty->windowResizeEvents) {
				callback->onResize(
					callback->opaque,
					record.Event.WindowBufferSizeEvent.dwSize.X,
					record.Event.WindowBufferSizeEvent.dwSize.Y,
					0, 0
				);
			}
		}

		// Returning 0 would indicate an interrupt, so loop if we haven't read any raw bytes.
		if (nextBufferIndex == 0) {
			goto loop;
		}
		result.count = nextBufferIndex;
	} else if (unlikely(waitResult == WAIT_FAILED)) {
		goto err;
	}
	// Else return a count of 0 because either:
	// - The interrupt event was selected (which auto resets its state).
	// - The user-supplied, non-infinite timeout ran out.

	ret:
	return result;

	err:
	result.error = GetLastError();
	goto ret;
}

uint32_t tty_interruptRead(MosaicTty *tty) {
	return likely(SetEvent(tty->interrupt_event) != 0)
		? 0
		: GetLastError();
}

MosaicTtyIoResult tty_writeInternal(HANDLE h, char *buffer, int count) {
	MosaicTtyIoResult result = {};

	DWORD written;
	if (WriteFile(h, buffer, count, &written, NULL)) {
		result.count = written;
	} else {
		result.error = GetLastError();
	}

	return result;
}

MosaicTtyIoResult tty_writeOutput(MosaicTty *tty, char *buffer, int count) {
	return tty_writeInternal(tty->stdout, buffer, count);
}

MosaicTtyIoResult tty_writeError(MosaicTty *tty, char *buffer, int count) {
	return tty_writeInternal(tty->stderr, buffer, count);
}

uint32_t tty_enableRawMode(MosaicTty *tty) {
	uint32_t result = 0;

	if (tty->saved_input_mode) {
		goto ret; // Already enabled!
	}

	DWORD input_mode;
	DWORD output_mode;
	UINT output_code_page;
	if (unlikely(GetConsoleMode(tty->stdin, &input_mode) == 0)) {
		result = GetLastError();
		goto ret;
	}
	if (unlikely(GetConsoleMode(tty->stdout, &output_mode) == 0)) {
		result = GetLastError();
		goto ret;
	}
	if (unlikely((output_code_page = GetConsoleOutputCP()) == 0)) {
		result = GetLastError();
		goto ret;
	}

	// https://learn.microsoft.com/en-us/windows/console/setconsolemode
	const int stdinMode = 0
		// Disable quick edit mode.
		| ENABLE_EXTENDED_FLAGS
		// Report changes to the mouse position.
		| ENABLE_MOUSE_INPUT
		// Encode key and mouse events as VT sequences rather than input records.
		| ENABLE_VIRTUAL_TERMINAL_INPUT
		// Report changes to the buffer size.
		| ENABLE_WINDOW_INPUT
		;
	const int stdoutMode = 0
		// Do not wrap cursor to next line automatically when writing final column.
		| DISABLE_NEWLINE_AUTO_RETURN
		// Allow color sequences to affect characters in all locales.
		| ENABLE_LVB_GRID_WORLDWIDE
		// Process outgoing VT sequences for colors, etc.
		| ENABLE_PROCESSED_OUTPUT
		// Process outgoing VT sequences for cursor movement, etc.
		| ENABLE_VIRTUAL_TERMINAL_PROCESSING
		;
	// UTF-8 per https://learn.microsoft.com/en-us/windows/win32/intl/code-page-identifiers.
	const int stdoutCp = 65001;

	if (unlikely(SetConsoleMode(tty->stdin, stdinMode) == 0)) {
		result = GetLastError();
		goto ret;
	}
	if (unlikely(SetConsoleMode(tty->stdout, stdoutMode) == 0)) {
		result = GetLastError();
		SetConsoleMode(tty->stdin, input_mode);
		goto ret;
	}
	if (unlikely(SetConsoleOutputCP(stdoutCp) == 0)) {
		result = GetLastError();
		SetConsoleMode(tty->stdin, input_mode);
		SetConsoleMode(tty->stdout, output_mode);
		goto ret;
	}

	tty->saved_input_mode = input_mode;
	tty->saved_output_mode = output_mode;
	tty->saved_output_code_page = output_code_page;

	ret:
	return result;
}

uint32_t tty_enableWindowResizeEvents(MosaicTty *tty) {
	tty->windowResizeEvents = true;
	return 0;
}

MosaicTtyTerminalSizeResult tty_currentTerminalSize(MosaicTty *tty) {
	MosaicTtyTerminalSizeResult result = {};

	CONSOLE_SCREEN_BUFFER_INFO info;
	if (likely(GetConsoleScreenBufferInfo(tty->stdout, &info))) {
		result.columns = info.dwSize.X;
		result.rows = info.dwSize.Y;
	} else {
		result.error = GetLastError();
	}

	return result;
}

uint32_t tty_free(MosaicTty *tty) {
	uint32_t result = 0;

	if (unlikely(CloseHandle(tty->interrupt_event) == 0)) {
		result = GetLastError();
	}

	if (tty->saved_input_mode) {
		if (unlikely(!SetConsoleMode(tty->stdin, tty->saved_input_mode) && result == 0)) {
			result = GetLastError();
		}
		if (unlikely(!SetConsoleMode(tty->stdout, tty->saved_output_mode) && result == 0)) {
			result = GetLastError();
		}
		if (unlikely(!SetConsoleOutputCP(tty->saved_output_code_page) && result == 0)) {
			result = GetLastError();
		}
	}

	free(tty);
	return result;
}

#endif
