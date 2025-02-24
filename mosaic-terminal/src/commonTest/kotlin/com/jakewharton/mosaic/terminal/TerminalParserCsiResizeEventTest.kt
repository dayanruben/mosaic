package com.jakewharton.mosaic.terminal

import assertk.assertThat
import assertk.assertions.isEqualTo
import com.jakewharton.mosaic.terminal.event.ResizeEvent
import com.jakewharton.mosaic.terminal.event.UnknownEvent
import kotlin.test.Test
import kotlinx.coroutines.test.runTest

class TerminalParserCsiResizeEventTest : BaseTerminalParserTest() {
	@Test fun basic() = runTest {
		testTty.writeHex("1b5b34383b313b323b333b3474")
		assertThat(reader.next()).isEqualTo(ResizeEvent(2, 1, 4, 3))
	}

	@Test fun pixelSizeAsZero() = runTest {
		testTty.writeHex("1b5b34383b313b323b303b3074")
		assertThat(reader.next()).isEqualTo(ResizeEvent(2, 1, 0, 0))
	}

	@Test fun subparametersIgnored() = runTest {
		testTty.writeHex("1b5b34383b313a39393b323a39383a39373b333a39393a3a39373b343a39393a74")
		assertThat(reader.next()).isEqualTo(ResizeEvent(2, 1, 4, 3))
	}

	@Test fun emptySubparametersIgnored() = runTest {
		testTty.writeHex("1b5b34383b313a3b323a3b333a3b343a74")
		assertThat(reader.next()).isEqualTo(ResizeEvent(2, 1, 4, 3))
	}

	@Test fun emptyModeFails() = runTest {
		testTty.writeHex("1b5b3b313b323b333b3474")
		assertThat(reader.next()).isEqualTo(
			UnknownEvent("1b5b3b313b323b333b3474".hexToByteArray()),
		)
	}

	@Test fun emptyParameterFails() = runTest {
		testTty.writeHex("1b5b34383b3b323b333b3474")
		assertThat(reader.next()).isEqualTo(
			UnknownEvent("1b5b34383b3b323b333b3474".hexToByteArray()),
		)
		testTty.writeHex("1b5b34383b313b3b333b3474")
		assertThat(reader.next()).isEqualTo(
			UnknownEvent("1b5b34383b313b3b333b3474".hexToByteArray()),
		)
		testTty.writeHex("1b5b34383b313b323b3b3474")
		assertThat(reader.next()).isEqualTo(
			UnknownEvent("1b5b34383b313b323b3b3474".hexToByteArray()),
		)
		testTty.writeHex("1b5b34383b313b323b333b74")
		assertThat(reader.next()).isEqualTo(
			UnknownEvent("1b5b34383b313b323b333b74".hexToByteArray()),
		)
	}

	@Test fun nonDigitParameterFails() = runTest {
		testTty.writeHex("1b5b34383b312e303b323b333b3474")
		assertThat(reader.next()).isEqualTo(
			UnknownEvent("1b5b34383b312e303b323b333b3474".hexToByteArray()),
		)
		testTty.writeHex("1b5b34383b313b322e303b333b3474")
		assertThat(reader.next()).isEqualTo(
			UnknownEvent("1b5b34383b313b322e303b333b3474".hexToByteArray()),
		)
		testTty.writeHex("1b5b34383b313b323b332e303b3474")
		assertThat(reader.next()).isEqualTo(
			UnknownEvent("1b5b34383b313b323b332e303b3474".hexToByteArray()),
		)
		testTty.writeHex("1b5b34383b313b323b333b342e3074")
		assertThat(reader.next()).isEqualTo(
			UnknownEvent("1b5b34383b313b323b333b342e3074".hexToByteArray()),
		)
	}
}
