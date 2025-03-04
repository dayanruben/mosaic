package com.jakewharton.mosaic.tty

import kotlinx.cinterop.CPointer
import kotlinx.cinterop.addressOf
import kotlinx.cinterop.useContents
import kotlinx.cinterop.usePinned

public actual class TestTty private constructor(
	private var ptr: CPointer<MosaicTestTty>?,
	public actual val tty: Tty,
) : AutoCloseable {
	public actual companion object {
		public actual fun create(): TestTty {
			val testTtyPtr = testTty_init().useContents {
				testTty?.let { return@useContents it }

				if (error != 0U) {
					throwIse(error)
				}
				throw OutOfMemoryError()
			}

			val ttyPtr = testTty_getTty(testTtyPtr)!!
			val tty = Tty(ttyPtr)
			return TestTty(testTtyPtr, tty)
		}
	}

	public actual fun write(buffer: ByteArray) {
		val error = buffer.asUByteArray().usePinned {
			testTty_write(ptr, it.addressOf(0), buffer.size)
		}
		if (error == 0U) return
		throwIse(error)
	}

	public actual fun focusEvent(focused: Boolean) {
		testTty_focusEvent(ptr, focused)
	}

	public actual fun keyEvent() {
		testTty_keyEvent(ptr)
	}

	public actual fun mouseEvent() {
		testTty_mouseEvent(ptr)
	}

	public actual fun resizeEvent(columns: Int, rows: Int, width: Int, height: Int) {
		testTty_resizeEvent(ptr, columns, rows, width, height)
	}

	actual override fun close() {
		ptr?.let { ref ->
			this.ptr = null

			tty.close()

			val error = testTty_free(ref)

			if (error == 0U) return
			throwIse(error)
		}
	}
}
