package com.winpreview.state

data class EditorState(
    val lineCount: Int = 1,
    val cursorLine: Int = 0,
    val cursorCol: Int = 0,
    val isDirty: Boolean = false,
    val tabSpaces: Int = 4,
    val wordWrap: Boolean = false
) {
    val cursorDisplay: String
        get() = "Ln ${cursorLine + 1}, Col ${cursorCol + 1}"

    fun withCursor(line: Int, col: Int): EditorState =
        copy(cursorLine = line, cursorCol = col)

    fun markModified(): EditorState =
        if (isDirty) this else copy(isDirty = true)
}
