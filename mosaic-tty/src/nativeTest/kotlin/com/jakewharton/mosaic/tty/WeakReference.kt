package com.jakewharton.mosaic.tty

import kotlin.experimental.ExperimentalNativeApi

@OptIn(ExperimentalNativeApi::class)
internal actual typealias WeakReference<T> = kotlin.native.ref.WeakReference<T>
