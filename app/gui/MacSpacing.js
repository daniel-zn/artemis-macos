.pragma library

// macOS spacing, shared by the Computers and games lists and the dialogs, so every page
// lines up the same way. The metrics are those of a macOS sidebar or source list:
// rows sit 10 pt in from the window's edges and their rounded selection never touches
// them; on a wide window the list stays a readable width and is centered.

// Space between the window's left/right edges and a row (and its selection)
var edge = 10
// Space above the first row and below the last
var top = 10
var bottom = 10
// The widest a row gets; wider windows center the list
var maxRowWidth = 640
// Corner radius of a row's selection and hover highlight
var rowRadius = 10
// Space between a row's edge and its first/last content (icon, buttons)
var rowInset = 12

// Dialog panel padding (all sides) and the gap above its buttons
var dialogPadding = 20

// The left and right margin of a list in a view this wide
function sideMargin(viewWidth) {
    return Math.max(edge, Math.floor((viewWidth - maxRowWidth) / 2))
}

// The width of a list row in a view this wide
function rowWidth(viewWidth) {
    return Math.max(0, viewWidth - 2 * sideMargin(viewWidth))
}
