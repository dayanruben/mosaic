#if defined(_WIN32)

#include "mosaic-tty-windows.h"
#include "mosaic-test-tty.h"

#include "cutils.h"
#include <windows.h>

typedef struct MosaicTestTtyImpl {
	MosaicTty *tty;
} MosaicTestTtyImpl;

// A single global input writer into which fake data can be sent. Creating and closing this over
// and over eventually produces a failure, so we only do it once per process (since it's test only).
HANDLE writerConin = NULL;

MosaicTestTtyInitResult testTty_init(MosaicTtyCallback *callback) {
	MosaicTestTtyInitResult result = {};

	MosaicTestTtyImpl *testTty = calloc(1, sizeof(MosaicTestTtyImpl));
	if (unlikely(testTty == NULL)) {
		// result.testTty is set to 0 which will trigger OOM.
		goto ret;
	}

	HANDLE h = writerConin;
	if (h == NULL) {
		// When run as a test, GetStdHandle(STD_INPUT_HANDLE) returns a closed handle which does not
		// work. Open a new console input handle for our non-display testing purposes.
		h = CreateFile(TEXT("CONIN$"), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
		if (unlikely(h == INVALID_HANDLE_VALUE)) {
			result.error = GetLastError();
			goto err;
		}
		if (unlikely(SetConsoleMode(h, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS) == 0)) {
			result.error = GetLastError();
			goto err;
		}
		writerConin = h;
	}

	// Ensure we don't start with existing records in the buffer.
	FlushConsoleInputBuffer(writerConin);

	MosaicTtyInitResult ttyInitResult = tty_initWithHandle(writerConin, callback);
	if (unlikely(ttyInitResult.error)) {
		result.error = ttyInitResult.error;
		goto err;
	}
	testTty->tty = ttyInitResult.tty;

	result.testTty = testTty;

	ret:
	return result;

	err:
	free(testTty);
	goto ret;
}

MosaicTty *testTty_getTty(MosaicTestTty *testTty) {
	return testTty->tty;
}

uint32_t testTty_write(MosaicTestTty *testTty UNUSED, char *buffer, int count) {
	uint32_t result = 0;
	INPUT_RECORD *records = calloc(count, sizeof(INPUT_RECORD));
	if (!records) {
		result = ERROR_NOT_ENOUGH_MEMORY;
		goto ret;
	}
	for (int i = 0; i < count; i++) {
		records[i].EventType = KEY_EVENT;
		records[i].Event.KeyEvent.uChar.AsciiChar = buffer[i];
	}

	INPUT_RECORD *writeRecord = records;
	while (count > 0) {
		DWORD written;
		if (!WriteConsoleInputW(writerConin, writeRecord, count, &written)) {
			goto err;
		}
		count -= (int) written;
		writeRecord += (int) written;
	}

	ret:
	free(records);

	return result;

	err:
	result = GetLastError();
	goto ret;
}

uint32_t writeRecord(INPUT_RECORD *record) {
	DWORD written;
	if (likely(WriteConsoleInputW(writerConin, record, 1, &written))) {
		if (likely(written == 1)) {
			return 0;
		}
		return ERROR_WRITE_FAULT;
	}
	return GetLastError();
}

uint32_t testTty_focusEvent(MosaicTestTty *testTty UNUSED, bool focused) {
	INPUT_RECORD record;
	record.EventType = FOCUS_EVENT;
	record.Event.FocusEvent.bSetFocus = focused;
	return writeRecord(&record);
}

uint32_t testTty_keyEvent(MosaicTestTty *testTty UNUSED) {
	// TODO
	return 0;
}

uint32_t testTty_mouseEvent(MosaicTestTty *testTty UNUSED) {
	// TODO
	return 0;
}

uint32_t testTty_resizeEvent(MosaicTestTty *testTty UNUSED, int columns, int rows, int width UNUSED, int height UNUSED) {
	INPUT_RECORD record;
	record.EventType = WINDOW_BUFFER_SIZE_EVENT;
	record.Event.WindowBufferSizeEvent.dwSize.X = columns;
	record.Event.WindowBufferSizeEvent.dwSize.Y = rows;
	return writeRecord(&record);
}

uint32_t testTty_free(MosaicTestTty *testTty) {
	free(testTty);
	return 0;
}

#endif
